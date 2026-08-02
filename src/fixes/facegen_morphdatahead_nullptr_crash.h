#pragma once

namespace Fixes::FaceGenMorphDataHeadNullPtrCrash
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_target, std::uintptr_t a_constant)
            {
                Xbyak::Label zeroLbl;

                // new nullcheck
                test(rbx, rbx);
                jz(zeroLbl);

                // original code
                mov(eax, ptr[rbx + 0x24]);
                push(rbx);
                mov(rbx, a_constant);
                comiss(xmm6, ptr[rbx]);
                pop(rbx);

                jmp(ptr[rip]);
                dq(a_target + 0xA);  // return to regular execution

                L(zeroLbl);
                jmp(ptr[rip]);
                dq(a_target + 0x3F3);  // skip code
            }
        };

        struct PatchClearRbx final : Xbyak::CodeGenerator
        {
            PatchClearRbx()
            {
                xor_(ebx, ebx);  // 2 bytes
                nop();           // 1 byte
            }
        };
    }

    inline void Install()
    {
        // fix null ptr
        REL::Relocation target{ RELOCATION_ID(26343, 26918), 0x4C };
        REL::Relocation constant{ RELOCATION_ID(228611, 186426) };

        detail::Patch p(target.address(), constant.address());
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        // Hash verified 2026-08-01: 0 code xrefs, 0 stored/vtable pointers into the
        // clobbered+orphan range; line below immediately NOP-fills the rest of it anyway.
        // Differs per runtime -- bytes at the orphan address aren't identical SE/AE vs VR.
        target.write_branch<5>(trampoline.allocate(p), false, REL::Module::IsVR() ? 0x7FE6E02F02EBCEAULL : 0x348F214DD731814DULL);

        REL::Relocation<std::uintptr_t> nopTarget{ RELOCATION_ID(26343, 26918), 0x51 };
        nopTarget.write_fill(REL::NOP, 0x5);

        // fix clearing rbx
        REL::Relocation       targetRbx{ RELOCATION_ID(26343, 26918), 0x49 };
        detail::PatchClearRbx pRbx;
        pRbx.ready();

        targetRbx.write(std::span{ pRbx.getCode<const std::byte*>(), pRbx.getSize() });

        logger::info("installed facegen morphdatahead nullptr crash fix"sv);
    }
}
