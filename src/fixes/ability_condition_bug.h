#pragma once

// VR-only FixAbilityConditionBug port.
// Canonical source: EngineFixesVR/fixes/miscfixes.cpp::FixAbilityConditionBug.
//
// Root cause: ActiveEffect::EvaluateConditions(this, float deltaTime, bool forced).
// When forced=false the native gate at 0x54123D re-evaluates conditions only when
//   floor(elapsedSeconds * rate) != floor((elapsedSeconds + deltaTime) * rate).
// elapsedSeconds is frozen while a conditioned effect is inactive (flags bit 18
// halts accumulation) and restarts at 0 on save load, so an effect restored
// inactive -- e.g. worn/wielded-gear conditions are false mid-load, before
// equipment re-equips -- can never cross a bucket boundary: its conditions are
// never re-checked and it is stuck inactive forever.  SE recovers via forced
// sweeps (deltaTime==0 calls take the forced path at +0x1FC, upstream of the
// gate); in VR the gate is empirically the only reactivation path (issue #19).
//
// Fix: replace the gate (0x54123D, length 0x79) with a wall-clock bucket
// throttle keyed per ActiveEffect* that runs on first sight of an effect and
// once per interval after, regardless of elapsedSeconds.  Buckets are sized by
// fActiveEffectConditionUpdateInterval at 0x1EA23E0 (default 1.0 s).
//
// Return semantics:
//   true  -> jump to 0x54133D (skip path / epilogue)
//   false -> jump to 0x5412B6 (run condition-eval path)
//
// v7.4.0-7.4.6 also carried over EngineFixesVR's "elapsedSeconds <= 0 -> skip"
// early-out; combined with the frozen/reset elapsedSeconds above it permanently
// deadlocked conditioned abilities after a save load (issue #19: Vokrii/Adamant
// perks, dual casting, Experimenter).  Do not reintroduce it.

namespace Fixes::AbilityConditionBug
{
    namespace detail
    {
        // Mirrors EngineFixesVR's TimerData (all fields long, same defaults).
        struct TimerData
        {
            long lastCounter{ -1 };
            long updateDiff{ -1 };
            long updateTimer{ 0 };
            long lastTick{ 0 };
        };

        struct U64Hash
        {
            static std::size_t hash(std::uint64_t k) noexcept { return std::hash<std::uint64_t>{}(k); }
            static bool        equal(std::uint64_t a, std::uint64_t b) noexcept { return a == b; }
        };

        using TimerMap = tbb::concurrent_hash_map<std::uint64_t, TimerData, U64Hash>;
        inline TimerMap g_timers;

        // Mirrors EngineFixesVR::newTimingFunc bucket logic, minus its
        // "elapsedSeconds <= 0 -> skip" early-out (see header note).
        // a_rid = ActiveEffect* (RCX), used as per-effect timer key
        // Returns true to SKIP, false to RUN.
        inline bool Hook(std::uint64_t a_rid)
        {
            const long now = static_cast<long>(GetTickCount64());

            TimerMap::accessor acc;
            const bool         inserted = g_timers.insert(acc, a_rid);
            auto&              td = acc->second;

            if (!inserted) {
                const long delta = now - td.lastTick;
                if (delta == 0)
                    return true;
                td.lastTick = now;
                td.updateTimer += delta;
            } else {
                td.lastTick = now;
            }

            if (td.updateDiff < 0) {
                // fActiveEffectConditionUpdateInterval (VR 0x1EA23E0, default 1.0 s).
                // VR-only code path (Install is IsVR-gated), so a raw VR offset is
                // safe here and matches the original EngineFixesVR fix.
                REL::Relocation<float*> intervalSetting{ REL::Offset{ 0x1EA23E0 } };
                const float             interval = (std::max)(0.001f, *intervalSetting);

                td.updateDiff = static_cast<long>(1000.0f / interval);
                if (td.updateDiff <= 0)
                    td.updateDiff = 1;
            }

            // false = bucket boundary crossed = run eval; true = same bucket = skip.
            // First sight of an effect runs immediately (lastCounter starts at -1),
            // which is what re-activates conditioned abilities right after a load.
            const long cur = td.updateTimer / td.updateDiff;
            if (cur != td.lastCounter) {
                td.lastCounter = cur;
                return false;
            }
            return true;
        }

        // Xbyak thunk: call Hook, branch to run or skip target.
        //   Hook() false -> al==0 -> jz falseLbl -> a_returnFalse = 0x5412B6 (run eval)
        //   Hook() true  -> al!=0 -> fall through -> a_returnTrue  = 0x54133D (skip)
        struct Thunk : Xbyak::CodeGenerator
        {
            explicit Thunk(std::uintptr_t a_returnFalse, std::uintptr_t a_returnTrue)
            {
                Xbyak::Label falseLbl, trueAddrLbl, falseAddrLbl;

                // At the patch site RSP is 0 mod 16 (PUSH RDI + SUB RSP,0x40 in prologue).
                // 0x30 = 32 bytes shadow space + 8 bytes padding to keep RSP 0 mod 16
                // before the call (otherwise Hook's movaps saves fault on misalignment).
                //
                // RDI = ActiveEffect* saved in prologue (MOV RDI,RCX at +0x25).
                // RCX is clobbered by the preceding CALL so arg0 is sourced from RDI:
                //   arg0 (RCX) = RDI -- ActiveEffect*, timer map key
                sub(rsp, 0x30);
                mov(rcx, rdi);
                mov(rax, reinterpret_cast<std::uintptr_t>(Hook));
                call(rax);
                add(rsp, 0x30);

                test(al, al);
                jz(falseLbl);
                jmp(ptr[rip + trueAddrLbl]);

                L(falseLbl);
                jmp(ptr[rip + falseAddrLbl]);

                L(trueAddrLbl);
                dq(a_returnTrue);

                L(falseAddrLbl);
                dq(a_returnFalse);
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;

        // Patch the gate block (0x54123D, length 0x79) entry point with a JMP.
        // returnFalse = target+0x79  = 0x5412B6  run condition-eval
        // returnTrue  = target+0x100 = 0x54133D  skip / epilogue
        REL::Relocation<std::uintptr_t> target{ REL::Offset{ 0x54123D } };
        const auto                      returnFalse = target.address() + 0x79;
        const auto                      returnTrue = target.address() + 0x100;

        detail::Thunk thunk{ returnFalse, returnTrue };
        thunk.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(thunk));

        logger::info("installed ability condition bug fix (VR)"sv);
    }
}
