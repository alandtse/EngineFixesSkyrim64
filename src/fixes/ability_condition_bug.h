#pragma once

// VR-only fix for the ability condition evaluation bug.
// Faithful port of EngineFixesVR/fixes/miscfixes.cpp FixAbilityConditionBug.
//
// The VR copy of ValueModifierEffect::UpdateConditions (VR 0x541160, the
// function containing the patch site) re-evaluates ability conditions far more
// often than intended. The fix replaces the native throttle gate (the 0x79-byte
// block at VR offset 0x54123D) with a hook that rate-limits evaluation per
// ActiveEffect using the engine's own fActiveEffectConditionUpdateInterval
// game setting, exactly as EngineFixesVR did.
//
// This is a 1:1 behavioural port of the known-good EngineFixesVR implementation:
//   - real-time throttle via GetTickCount64() (NOT game time)
//   - per-effect TimerData accumulator (lastTick / updateTimer / updateDiff /
//     lastCounter)
//   - quantizer derived from the game setting at VR offset 0x1EA23E0
// The only deliberate deviations from the original are non-behavioural:
//   - a value-type concurrent_hash_map instead of a leaking heap TimerData*
//   - the unused third newTimingFunc parameter (elapsedTime) is dropped; the
//     original never read it and mis-passed it in XMM0 anyway
//   - proper shadow-space/stack alignment around the call

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

        // Faithful port of EngineFixesVR::newTimingFunc.
        //   a_rid  = ActiveEffect* (RCX) — used as the per-effect timer key
        //   a_diff = [RDI+0x70]   (XMM1) — the effect's time base; <= 0 means skip
        // Returns:
        //   true  → throttle: skip evaluation → Thunk jumps to returnTrue  (epilogue)
        //   false → due:      run  evaluation → Thunk jumps to returnFalse (condition block)
        inline bool Hook(std::uint64_t a_rid, float a_diff)
        {
            if (a_diff <= 0.0f)
                return true;

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
                // fActiveEffectConditionUpdateInterval (VR 0x1EA23E0, default 1.0).
                // VR-only code path (Install is IsVR-gated), so a raw VR offset is
                // safe here and matches the original EngineFixesVR fix.
                REL::Relocation<float*> intervalSetting{ REL::Offset{ 0x1EA23E0 } };
                const float             interval = (std::max)(0.001f, *intervalSetting);

                td.updateDiff = static_cast<long>(1000.0f / interval);
                if (td.updateDiff <= 0)
                    td.updateDiff = 1;
            }

            const long cur = td.updateTimer / td.updateDiff;
            if (cur != td.lastCounter) {
                td.lastCounter = cur;
                return true;
            }
            return false;
        }

        // Xbyak thunk: set up args, call Hook, branch on the bool return value.
        //   Hook() returns true  → throttled, skip eval → epilogue        (target+0x100 = 0x54133D)
        //   Hook() returns false → due for eval, run it → condition block (target+0x79  = 0x5412B6)
        struct Thunk : Xbyak::CodeGenerator
        {
            explicit Thunk(std::uintptr_t a_returnFalse, std::uintptr_t a_returnTrue)
            {
                Xbyak::Label falseLbl, trueAddrLbl, falseAddrLbl;

                // At the patch site (mid-function) RSP is 0 mod 16 (PUSH RDI + SUB
                // RSP,0x40 in the prologue). sub 0x28 would leave RSP 8 mod 16 before
                // the call, faulting Hook's movaps prologue saves. 0x30 = 32 bytes of
                // shadow space + keeps RSP 0 mod 16.
                //
                // RDI holds the ActiveEffect* (saved from RCX in the prologue at +0x25:
                // MOV RDI,RCX). RCX is clobbered by the preceding CALL, so the args are
                // sourced from RDI:
                //   arg0 (rid, RCX)  = RDI            — effect pointer, used as timer key
                //   arg1 (diff, XMM1) = [RDI+0x70]    — the effect's time base
                sub(rsp, 0x30);
                movss(xmm1, ptr[rdi + 0x70]);  // arg1: diff
                mov(rcx, rdi);                 // arg0: ActiveEffect* (RCX clobbered upstream)
                mov(rax, reinterpret_cast<std::uintptr_t>(Hook));
                call(rax);
                add(rsp, 0x30);

                // al = Hook's bool return value
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

        // VR raw offset for the problematic ability-condition evaluation block.
        // The block is 0x79 bytes long; we patch the entry point (first 5 bytes,
        // MOVSS XMM1,[RDI+0x70]) with a JMP to our throttling thunk.
        //   returnFalse (target+0x79  = 0x5412B6): run  — condition evaluation proceeds
        //   returnTrue  (target+0x100 = 0x54133D): skip — condition evaluation throttled
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
