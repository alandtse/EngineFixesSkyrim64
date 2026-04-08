#pragma once

namespace Fixes::EquipShoutEventSpam
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_target)
            {
                Xbyak::Label exitLbl;
                Xbyak::Label exitIP;
                Xbyak::Label sendEvent;

                // rbp = Actor* (AE), r14 = Actor* (SE/VR)
                // rdi = TESShout*
                if (REL::Module::IsAE())
                    cmp(ptr[rbp + 0x1E8], rdi);
                else
                    cmp(ptr[r14 + 0x1E0], rdi);
                je(exitLbl);
                if (REL::Module::IsAE())
                    mov(ptr[rbp + 0x1E8], rdi);  // actor->equippedShout = shout;
                else
                    mov(ptr[r14 + 0x1E0], rdi);
                test(rdi, rdi);  // if (shout)
                jz(exitLbl);
                jmp(ptr[rip + sendEvent]);

                L(exitLbl);
                jmp(ptr[rip + exitIP]);

                L(exitIP);
                dq(a_target + VAR_NUM(0xBC, 0x8A));  // SendEvent end

                L(sendEvent);
                dq(a_target + VAR_NUM(0x10, 0xC));  // SendEvent begin
            }
        };
    }

    inline void Install()
    {
        REL::Relocation target{ RELOCATION_ID(37821, 38770), VAR_NUM(0x17A, 0x13D) };

        detail::Patch p(target.address());
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(p));

        REL::Relocation<std::uintptr_t>{ target.address() + 5 }.write_fill(REL::NOP, 7);

        logger::info("installed equip shout event spam fix"sv);
    }
}
