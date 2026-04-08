#pragma once

// VR-only fix for the ability condition evaluation bug.
// Ported from EngineFixesVR/fixes/miscfixes.cpp FixAbilityConditionBug.
//
// The VR engine re-evaluates ability conditions every frame, causing incorrect
// behaviour. The fix patches the 0x79-byte block at VR offset 0x54123D and
// replaces it with a throttled hook that evaluates at most once per ~0.1
// in-game seconds per ActiveEffect.

namespace Fixes::AbilityConditionBug
{
    namespace detail
    {
        struct TimerData
        {
            float lastChecked{ 0.0f };
        };

        struct PtrHash
        {
            static std::size_t hash(const void* p) noexcept { return std::hash<const void*>{}(p); }
            static bool        equal(const void* a, const void* b) noexcept { return a == b; }
        };

        using TimerMap = tbb::concurrent_hash_map<void*, TimerData, PtrHash>;
        inline TimerMap g_timers;

        // Hook function. rcx = ActiveEffect* at the VR call site (verify against binary).
        inline bool Hook(RE::ActiveEffect* a_effect)
        {
            if (!a_effect)
                return false;

            const auto cal = RE::Calendar::GetSingleton();
            const auto now = cal ? cal->GetCurrentGameTime() : 0.0f;

            TimerMap::accessor acc;
            g_timers.insert(acc, static_cast<void*>(a_effect));
            auto& data = acc->second;

            if (now - data.lastChecked < 0.1f)
                return true;   // throttled → Thunk jumps to returnTrue = epilogue (skip)

            data.lastChecked = now;
            return false;      // due for evaluation → Thunk jumps to returnFalse = condition block (run)
        }

        // Xbyak thunk: set up shadow space, call Hook, branch on return value.
        //   Hook() returns true  → throttled, skip eval  → epilogue         (target+0x100 = 0x14054133D)
        //   Hook() returns false → due for eval, run it  → condition block  (target+0x79  = 0x1405412B6)
        struct Thunk : Xbyak::CodeGenerator
        {
            explicit Thunk(std::uintptr_t a_returnFalse, std::uintptr_t a_returnTrue)
            {
                Xbyak::Label falseLbl, trueAddrLbl, falseAddrLbl;

                // At the patch site (mid-function), RSP is 0 mod 16 (standard Windows x64
                // mid-function state). sub 0x28 (40 = 8 mod 16) would leave RSP at 8 mod 16
                // before the call, causing Hook's movaps prologue saves to fault on the
                // misaligned address. Use 0x30 (48 = 0 mod 16) to keep RSP at 0 mod 16.
                sub(rsp, 0x30);
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
        // The block is 0x79 bytes long; we patch the entry point (first 5 bytes)
        // with a JMP to our throttling thunk.
        //   returnFalse (target+0x79  = 0x5412B6): skip — condition evaluation throttled
        //   returnTrue  (target+0x100 = 0x54133D): run — condition evaluation proceeds
        REL::Relocation<std::uintptr_t> target{ REL::Offset{ 0x54123D } };
        const auto returnFalse = target.address() + 0x79;
        const auto returnTrue  = target.address() + 0x100;

        detail::Thunk thunk{ returnFalse, returnTrue };
        thunk.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(thunk));

        logger::info("installed ability condition bug fix (VR)"sv);
    }
}
