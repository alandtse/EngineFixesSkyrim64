#pragma once

namespace Fixes::CopyBoneTransformNullCrash
{
    // VR-only null-pointer crash in a bone-transform copy helper (CopyBoneTransformByName),
    // called by Actor::UpdateAnimation. The function does:
    //     node = NiAVObject::LookupBoneNodeByName(root, name, 1);
    //     ... = *(node + 0x30); ...                     // crash if node == nullptr
    // and copies the node's local transform into a matching sub-node. When the requested bone
    // name is absent from the actor's skeleton (modded/atypical/mismatched skeleton),
    // LookupBoneNodeByName returns null and the unguarded `*(node + 0x30)` faults.
    // Observed online (pastebin/HjpTb61H-class logs): VR 0x1406E5447 `mov r10, [rax+0x30]`.
    //
    // The lookup result is used only locally and the function returns void, so an early-return
    // on a missing bone is safe degradation (that bone just isn't updated this frame) rather
    // than a CTD. This is a VR-specific function with no SE/AE counterpart, so it is bound by
    // raw VR offset (the VR address library is hand-curated) and gated on IsVR().
    //
    // Patch site = func+0x20 (0x6E5440), right after the CALL returns. The 5 overwritten bytes
    // (`xor edx,edx; mov rbx,rax`) are re-run in the trampoline; normal path resumes at func+0x25.
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_resume)
            {
                Xbyak::Label earlyReturn;
                test(rax, rax);   // rax = LookupBoneNodeByName result
                jz(earlyReturn);  // bone not found -> early-return (skip this bone's update)
                xor_(edx, edx);   // re-run original overwritten bytes...
                mov(rbx, rax);    // ...
                jmp(ptr[rip]);
                dq(a_resume);  // resume at func+0x25 (mov ecx,edx); the +0x30 deref now only runs when non-null
                L(earlyReturn);
                // clean epilogue (mirrors the function's own): restore frame and return (void fn)
                mov(rbx, ptr[rsp + 0x30]);
                mov(rsi, ptr[rsp + 0x38]);
                add(rsp, 0x20);
                pop(rdi);
                ret();
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;  // VR-specific function (raw hand-curated offset)

        REL::Relocation<std::uintptr_t> target{ REL::Offset(0x6E5440) };  // CopyBoneTransformByName + 0x20

        detail::Patch p(target.address() + 0x5);  // resume at func+0x25
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(p));

        logger::info("installed copybonetransform null bone crash fix (VR)"sv);
    }
}
