#pragma once

// VR-only fix: buying/selling a stack of items only grants speech XP for one item.
// Ported from EngineFixesVR/fixes/miscfixes.cpp FixBuySellStackSpeechGain.
//
// At VR offsets 0x87CE5E (buy) and 0x87CD71 (sell), the engine calls a per-item
// XP calculation function (0x1E7200) and the result flows downstream to be awarded.
// The fix intercepts after that call, reads the stack count from the outer function's
// local at RSP+0xD0, and multiplies the XP result before continuing.
//
// No explicit RSP adjustment is needed — the outer function's frame already provides
// shadow space. Modifying RSP here would break the RSP+0xD0 stack read.

namespace Fixes::BuySellStackSpeechGain
{
    namespace detail
    {
        // Multiply per-item XP by the stack count.
        // x64 args: XMM0 (ignored), RDX = count (int), XMM2 = per-item XP (float).
        // Returns multiplied XP in XMM0.
        static float MultExp(float /*unused*/, int a_count, float a_xp)
        {
            return a_count > 1 ? a_xp * static_cast<float>(a_count) : a_xp;
        }

        // Cave: replaces the original CALL to the XP calculation function.
        //   1. Calls the original XP function     → XMM0 = per-item XP
        //   2. Reads stack count from RSP+0xD0    → RDX  = count
        //   3. Calls MultExp(_, count, xp)        → XMM0 = multiplied XP
        //   4. Jumps to a_continuation (target+5) so the game awards the result
        struct Cave : Xbyak::CodeGenerator
        {
            explicit Cave(std::uintptr_t a_gainXP, std::uintptr_t a_continuation)
            {
                Xbyak::Label contLbl;

                mov(rax, a_gainXP);
                call(rax);

                mov(rdx, ptr[rsp + 0xd0]);
                movss(xmm2, xmm0);
                mov(rax, reinterpret_cast<std::uintptr_t>(MultExp));
                call(rax);

                jmp(ptr[rip + contLbl]);
                L(contLbl);
                dq(a_continuation);
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;

        const auto base   = REL::Module::get().base();
        auto&      tramp  = SKSE::GetTrampoline();
        const auto gainXP = base + 0x1E7200;

        // Buy path
        {
            const auto target = base + 0x87CE5E;
            detail::Cave cave{ gainXP, target + 5 };
            cave.ready();
            tramp.write_branch<5>(target, tramp.allocate(cave));
        }

        // Sell path
        {
            const auto target = base + 0x87CD71;
            detail::Cave cave{ gainXP, target + 5 };
            cave.ready();
            tramp.write_branch<5>(target, tramp.allocate(cave));
        }

        logger::info("installed buy/sell stack speech gain fix (VR)"sv);
    }
}
