#pragma once

namespace Fixes::VerticalLookSensitivity
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_hookTarget, std::uintptr_t a_frameTimer)
            {
                Xbyak::Label retnLabel;
                Xbyak::Label magicLabel;

                if (REL::Module::IsAE()) {
                    mulss(xmm3, dword[rip + magicLabel]);
                    jmp(ptr[rip + retnLabel]);

                    L(retnLabel);
                    dq(a_hookTarget + 0x8);

                    L(magicLabel);
                    dd(0x3CC0C0C0);  // 1 / 42.5
                } else {
                    Xbyak::Label timerLabel;

                    // enter 850D81
                    // r8 is unused
                    //.text:0000000140850D81                 movss   xmm4, cs:frame_timer_without_slow
                    // use magic instead
                    movss(xmm4, dword[rip + magicLabel]);
                    //.text:0000000140850D89                 movaps  xmm3, xmm4
                    // use timer
                    mov(r8, ptr[rip + timerLabel]);
                    movss(xmm3, dword[r8]);

                    // exit 850D8C
                    jmp(ptr[rip + retnLabel]);

                    L(retnLabel);
                    dq(a_hookTarget + 0xB);

                    L(magicLabel);
                    dd(0x3CC0C0C0);

                    L(timerLabel);
                    dq(a_frameTimer);
                }
            }
        };
    }

    inline void Install()
    {
        using PairT = std::pair<REL::RelocationID, std::ptrdiff_t>;
        const std::array<PairT, 3> todo = {
            PairT{ RELOCATION_ID(49978, 50914), VAR_NUM(0x71, 0x65) },
            PairT{ RELOCATION_ID(32370, 33119), VAR_NUM(0x5F, 0x53) },
            PairT{ RELOCATION_ID(49839, 50770), VAR_NUM(0x5F, 0x53) },
        };

        auto& trampoline = SKSE::GetTrampoline();
        // AE uses a constant multiplier in the patch (no frame timer needed);
        // pass 0 as AE ID so the lookup is skipped on AE — value is never used there
        const REL::Relocation secondsSinceLastFrameRealTime{ RELOCATION_ID(523661, 0) };

        for (auto& [id, offset] : todo) {
            REL::Relocation target{ id, offset };
            detail::Patch p(target.address(), secondsSinceLastFrameRealTime.address());
            p.ready();

            target.write_branch<6>(trampoline.allocate(p));

            REL::Relocation<std::uintptr_t>{ target.address() + 6 }.write_fill(REL::NOP, VAR_NUM(5u, 2u));
        }

        logger::info("installed vertical look sensitivity fix"sv);
    }
}
