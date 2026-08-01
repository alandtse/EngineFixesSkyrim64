#pragma once

namespace Fixes::AcousticSpaceListenerNullRigidBodyCrash
{
    // Vanilla out-of-bounds crash in BGSAcousticSpaceListener::EntityRemovedCallback
    // (RE::hkpEntityListener slot 02), fired by hkpWorld::FireEntityRemovedCallback whenever a
    // Havok entity is removed from the physics world.
    //
    // When the preceding PlayerCharacter vfunc check is true, the handler compares the removed
    // entity against gPlayerCamera->rigidBody->referencedObject -- the camera's own Havok
    // collision body, used for camera-collision avoidance -- but dereferences
    // gPlayerCamera->rigidBody unconditionally, with no null check on rigidBody itself:
    //     mov rcx, [rax + 0x128]  ; rcx = gPlayerCamera->rigidBody (VR: +0x138)
    //     mov rax, [rcx + 0x10]   ; crashes when rigidBody is null
    // The code one step further down (`test rax,rax` / `cmovz`) already null-handles a missing
    // referencedObject; it just never guards the rigidBody pointer that field is read from.
    // rigidBody is null whenever the active camera state has no collision body attached (e.g.
    // during a camera-state transition), which is unrelated to whether the removed entity is the
    // player -- so on a null rigidBody the correct, engine-consistent answer is simply "not a
    // match" (the same outcome the null-safe referencedObject check would already produce), not a
    // crash.
    //
    // BGSAcousticSpaceListener::Unk_07 (an added virtual beyond hkpEntityListener's interface,
    // fired when something touches/overlaps the acoustic space) runs the same identity check and
    // has the identical unguarded rigidBody dereference; it gets the same treatment below.
    //
    // The patch site for both is the `mov reg,[rax+0x128/0x138]` load, not the shorter
    // `mov reg,[reg+0x10]` load that actually faults: that load is only 4 bytes, one short of the
    // 5-byte jmp a hotpatch trampoline needs, and the borrowed byte from the following
    // instruction (`test`) turned out to be independently reachable -- a first attempt at this
    // fix patched there and corrupted that shared byte, producing a *worse* crash on live
    // players. The load patched here is a full 7-byte instruction, so the trampoline's 5 bytes
    // never touch anything outside it. Each site's leading opcode bytes are verified against the
    // exact bytes read from the binary before patching; a mismatch skips that site rather than
    // risking a corrupt write.
    namespace detail
    {
        // MOV RCX,[RAX+imm32] (EntityRemovedCallback) or MOV RDX,[RAX+imm32] (Unk_07): both
        // identical across SE/AE/VR except the trailing 4-byte displacement (0x128 flat / 0x138 VR).
        inline bool SignatureMatches(std::uintptr_t a_addr, std::uint8_t a_modrm, std::uint32_t a_disp)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return p[0] == 0x48 && p[1] == 0x8B && p[2] == a_modrm &&
                   p[3] == static_cast<std::uint8_t>(a_disp) &&
                   p[4] == static_cast<std::uint8_t>(a_disp >> 8) &&
                   p[5] == static_cast<std::uint8_t>(a_disp >> 16) &&
                   p[6] == static_cast<std::uint8_t>(a_disp >> 24);
        }

        struct EntityRemovedPatch final : Xbyak::CodeGenerator
        {
            EntityRemovedPatch(std::uintptr_t a_resume, std::uint32_t a_rigidBodyOffset)
            {
                mov(rcx, qword[rax + a_rigidBodyOffset]);  // re-run displaced load: rcx = gPlayerCamera->rigidBody
                test(rcx, rcx);
                jz("null");
                mov(rax, qword[rcx + 0x10]);  // rax = rigidBody->referencedObject
                jmp(ptr[rip]);
                dq(a_resume);
                L("null");
                xor_(eax, eax);
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };

        // AE: no intervening instruction between the rigidBody load and the faulting load.
        struct Unk07PatchAE final : Xbyak::CodeGenerator
        {
            Unk07PatchAE(std::uintptr_t a_resume, std::uint32_t a_rigidBodyOffset)
            {
                mov(rdx, qword[rax + a_rigidBodyOffset]);
                test(rdx, rdx);
                jz("null");
                mov(rax, qword[rdx + 0x10]);
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

        // Resume offsets = patch site + length of every original instruction re-run in the
        // trampoline (the 7-byte rigidBody load, plus whatever sits between it and the faulting
        // load in each case) -- i.e. the address of the first instruction NOT re-run (`test`).
        REL::Relocation<std::uintptr_t> entityRemoved{ RELOCATION_ID(25154, 25676), VAR_NUM(0x2B, 0x2F, 0x2B) };
        if (detail::SignatureMatches(entityRemoved.address(), 0x88, rigidBodyOffset)) {
            detail::EntityRemovedPatch p(entityRemoved.address() + 0xB, rigidBodyOffset);  // +7 (load) +4 (faulting load)
            p.ready();
            entityRemoved.write_branch<5>(trampoline.allocate(p));
        } else {
            logger::warn("acoustic space listener crash fix: unexpected bytes at EntityRemovedCallback patch site, skipping"sv);
        }

        REL::Relocation<std::uintptr_t> unk07{ RELOCATION_ID(25155, 25677), VAR_NUM(0x40, 0x48, 0x40) };
        if (detail::SignatureMatches(unk07.address(), 0x90, rigidBodyOffset)) {
            if (REL::Module::IsAE()) {
                detail::Unk07PatchAE p(unk07.address() + 0xB, rigidBodyOffset);  // +7 (load) +4 (faulting load)
                p.ready();
                unk07.write_branch<5>(trampoline.allocate(p));
            } else {
                detail::Unk07PatchSEVR p(unk07.address() + 0xD, rigidBodyOffset);  // +7 (load) +2 (mov eax,ebp) +4 (faulting load)
                p.ready();
                unk07.write_branch<5>(trampoline.allocate(p));
            }
        } else {
            logger::warn("acoustic space listener crash fix: unexpected bytes at Unk_07 patch site, skipping"sv);
        }

        logger::info("installed acoustic space listener null rigidbody crash fix"sv);
    }
}
