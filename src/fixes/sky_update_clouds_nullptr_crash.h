#pragma once

namespace Fixes::SkyUpdateCloudsNullPtrCrash
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_target)
            {
                Xbyak::Label skipLbl, resumeLbl;

                // On entry rcx = Sky*. Sky[+0x48] is the current cloud object; Sky::UpdateClouds derefs
                // it (and its +0x880 BSFixedString) with no null-check, even though Sky::Update guards
                // Sky[+0x48] elsewhere. Skip the whole helper when it is null (clouds simply don't blend
                // this frame). Offset 0x48 is identical across SE/AE/VR.
                mov(rax, ptr[rcx + 0x48]);
                test(rax, rax);
                jz(skipLbl);

                // Re-emit the displaced prologue, then resume past it. SE/VR push rsi then rdi (5 bytes);
                // AE pushes rdi then r14 (6 bytes).
                mov(rax, rsp);  // 48 8B C4 (all runtimes)
                if (REL::Module::IsAE())
                {
                    push(rdi);
                    push(r14);
                }
                else
                {
                    push(rsi);
                    push(rdi);
                }
                jmp(ptr[rip + resumeLbl]);

                L(skipLbl);
                ret();  // Sky[+0x48] == null: return (void) cleanly; no prologue/stack has been touched

                L(resumeLbl);
                dq(a_target + VAR_NUM(0x5, 0x6, 0x5));
            }
        };
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(25689, 26236) };

        detail::Patch p(target.address());
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(p));

        logger::info("installed Sky::UpdateClouds null cloud crash fix"sv);
    }
}
