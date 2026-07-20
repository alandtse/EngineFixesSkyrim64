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
    //     mov rax, [rcx + 0x10]   ; rcx = gPlayerCamera->rigidBody, crashes when null
    // The code one step further down (`test rax,rax` / `cmovz`) already null-handles a missing
    // referencedObject; it just never guards the rigidBody pointer that field is read from.
    // rigidBody is null whenever the active camera state has no collision body attached (e.g.
    // during a camera-state transition), which is unrelated to whether the removed entity is the
    // player -- so on a null rigidBody the correct, engine-consistent answer is simply "not a
    // match" (the same outcome the null-safe referencedObject check would already produce), not a
    // crash.
    //
    // Handler is address library id 25154 (AE id 25676). The faulting instruction sits at:
    //   SE  func+0x32   AE  func+0x36   VR  func+0x32
    // The trampoline re-runs the displaced load only when rigidBody is non-null; otherwise it
    // substitutes 0 (as if referencedObject were also null) and resumes at the following
    // `test rax,rax`, which then takes its existing null path unchanged.
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume)
            {
                test(rcx, rcx);
                jz("null");
                mov(rax, qword[rcx + 0x10]);  // re-run displaced load: rax = rigidBody->referencedObject
                jmp(ptr[rip]);
                dq(a_resume);
                L("null");
                xor_(eax, eax);
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(25154, 25676), VAR_NUM(0x32, 0x36, 0x32) };

        auto& trampoline = SKSE::GetTrampoline();

        detail::Patch p(patch.address() + 0x4);
        p.ready();
        patch.write_branch<5>(trampoline.allocate(p));

        logger::info("installed acoustic space listener null rigidbody crash fix"sv);
    }
}
