#pragma once

namespace Fixes::AcousticSpaceListenerNullRigidBodyCrash
{
    // EntityRemovedCallback and Unk_07 both deref gPlayerCamera->rigidBody unconditionally
    // before the null-safe referencedObject check; a null rigidBody must evaluate to
    // "not a match", not crash.
    namespace detail
    {
        inline constexpr std::uint32_t kRigidBodyLoadLen = 7;
        inline constexpr std::uint32_t kFaultingLoadLen = 4;
        inline constexpr std::uint32_t kMovEaxEbpLen = 2;

        inline bool SignatureMatches(std::uintptr_t a_addr, std::uint8_t a_modrm, std::uint32_t a_disp)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return p[0] == 0x48 && p[1] == 0x8B && p[2] == a_modrm &&
                   p[3] == static_cast<std::uint8_t>(a_disp) &&
                   p[4] == static_cast<std::uint8_t>(a_disp >> 8) &&
                   p[5] == static_cast<std::uint8_t>(a_disp >> 16) &&
                   p[6] == static_cast<std::uint8_t>(a_disp >> 24);
        }

        // a_useRdx selects Unk_07(AE)'s rdx-based shape; false selects EntityRemovedCallback's
        // rcx-based shape. Both are otherwise identical.
        struct RigidBodyNullGuardPatch final : Xbyak::CodeGenerator
        {
            RigidBodyNullGuardPatch(bool a_useRdx, std::uintptr_t a_resume, std::uint32_t a_rigidBodyOffset)
            {
                if (a_useRdx) {
                    mov(rdx, qword[rax + a_rigidBodyOffset]);
                    test(rdx, rdx);
                    jz("null");
                    mov(rax, qword[rdx + 0x10]);
                } else {
                    mov(rcx, qword[rax + a_rigidBodyOffset]);
                    test(rcx, rcx);
                    jz("null");
                    mov(rax, qword[rcx + 0x10]);
                }
                jmp(ptr[rip]);
                dq(a_resume);
                L("null");
                xor_(eax, eax);
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };

        // SE/VR: an unrelated `mov eax,ebp` sits between the rigidBody load and the faulting
        // load; it doesn't depend on rigidBody and is replicated here unchanged.
        struct Unk07PatchSEVR final : Xbyak::CodeGenerator
        {
            Unk07PatchSEVR(std::uintptr_t a_resume, std::uint32_t a_rigidBodyOffset)
            {
                mov(rdx, qword[rax + a_rigidBodyOffset]);
                mov(eax, ebp);
                test(rdx, rdx);
                jz("null");
                mov(rcx, qword[rdx + 0x10]);
                jmp(ptr[rip]);
                dq(a_resume);
                L("null");
                xor_(ecx, ecx);
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };
    }

    inline void Install()
    {
        auto&               trampoline = SKSE::GetTrampoline();
        const std::uint32_t rigidBodyOffset = REL::Module::IsVR() ? 0x138 : 0x128;

        REL::Relocation<std::uintptr_t> entityRemoved{ RELOCATION_ID(25154, 25676), VAR_NUM(0x2B, 0x2F, 0x2B) };
        if (detail::SignatureMatches(entityRemoved.address(), 0x88, rigidBodyOffset)) {
            detail::RigidBodyNullGuardPatch p(
                false, entityRemoved.address() + detail::kRigidBodyLoadLen + detail::kFaultingLoadLen, rigidBodyOffset);
            p.ready();
            entityRemoved.write_branch<5>(trampoline.allocate(p));
        } else {
            logger::warn("acoustic space listener crash fix: unexpected bytes at EntityRemovedCallback patch site, skipping"sv);
        }

        REL::Relocation<std::uintptr_t> unk07{ RELOCATION_ID(25155, 25677), VAR_NUM(0x40, 0x48, 0x40) };
        if (detail::SignatureMatches(unk07.address(), 0x90, rigidBodyOffset)) {
            if (REL::Module::IsAE()) {
                detail::RigidBodyNullGuardPatch p(
                    true, unk07.address() + detail::kRigidBodyLoadLen + detail::kFaultingLoadLen, rigidBodyOffset);
                p.ready();
                unk07.write_branch<5>(trampoline.allocate(p));
            } else {
                detail::Unk07PatchSEVR p(unk07.address() + detail::kRigidBodyLoadLen + detail::kMovEaxEbpLen +
                                             detail::kFaultingLoadLen,
                    rigidBodyOffset);
                p.ready();
                unk07.write_branch<5>(trampoline.allocate(p));
            }
        } else {
            logger::warn("acoustic space listener crash fix: unexpected bytes at Unk_07 patch site, skipping"sv);
        }

        logger::info("installed acoustic space listener null rigidbody crash fix"sv);
    }
}
