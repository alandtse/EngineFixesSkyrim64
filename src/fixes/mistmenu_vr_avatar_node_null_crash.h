#pragma once

namespace Fixes::MistMenuVRAvatarNodeNullCrash
{
    // VR-only: guards hmdNode/leftHandNode/rightHandNode (this+0x20/0x30/0x38), unchecked
    // by UpdateVRControllerTransforms's existing this+0x8 "active" flag. Patch site = func+0x19.
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_earlyReturn, std::uintptr_t a_resume)
            {
                Xbyak::Label bail;
                jz(bail);  // re-run displaced check (ZF survives the intervening mov)
                cmp(qword[rbx + 0x20], 0);
                jz(bail);  // hmdNode null
                cmp(qword[rbx + 0x30], 0);
                jz(bail);  // leftHandNode null
                cmp(qword[rbx + 0x38], 0);
                jz(bail);  // rightHandNode null
                jmp(ptr[rip]);
                dq(a_resume);  // all guards passed; resume at func+0x1F, the real write blocks
                L(bail);
                jmp(ptr[rip]);
                dq(a_earlyReturn);  // same target the original this+0x8 == 0 check used
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;  // VR-only function, no SE/AE equivalent

        REL::Relocation<std::uintptr_t> target{ REL::Offset(0x534CF9) };  // MistMenu::UpdateVRControllerTransforms+0x19

        detail::Patch p(target.address() - 0x19 + 0x17F, target.address() + 0x6);  // early-return / resume at func+0x1F
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<6>(trampoline.allocate(p));

        logger::info("installed MistMenu VR avatar node null crash fix (VR)"sv);
    }
}
