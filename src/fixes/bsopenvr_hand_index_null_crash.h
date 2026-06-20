#pragma once

namespace Fixes::BSOpenVRHandIndexNullCrash
{
    // VR-only null-pointer crash in BSOpenVR::GetTrackedDeviceIndexForHand. The function loads the
    // OpenVR system interface and tail-calls into it:
    //     rcx = this->IVRSystem;            // this+0x208
    //     rax = *rcx; jmp [rax+0x90];       // IVRSystem->GetTrackedDeviceIndexForControllerRole
    // When the HMD is asleep (standby / proximity sensor) and no SteamVR null driver is loaded,
    // IVRSystem* is transiently null, so the unguarded `mov rax,[rcx]` faults. Recurs at startup /
    // menu render (MistMenu / VRMenuScene) when hand device indices are queried before VR is ready.
    // Observed: VR 0x140C53E1B `mov rax,[rcx]`, rcx == 0, VRInitError_Init_NotInitialized (109).
    //
    // GetTrackedDeviceIndexForHand returns a tracked-device index; vr::k_unTrackedDeviceIndexInvalid
    // (0xFFFFFFFF) is its normal "no device for this hand" result, which callers already handle (a
    // hand legitimately has no device when its controller is off). Returning that sentinel while
    // IVRSystem is null is correct degradation -- the hand simply has no device until the HMD wakes
    // -- not a crash band-aid. VR-specific function with no SE/AE counterpart, so it is bound by raw
    // VR offset (the VR address library is hand-curated) and gated on IsVR().
    //
    // Patch site = func+0x00 (0xC53DF0). The trampoline re-runs the displaced load
    // (mov rcx,[rcx+0x208]), null-checks it, and either resumes at func+0x07 (non-null) or returns
    // the invalid index (null). The load is 7 bytes; the 2 bytes past the 5-byte branch are
    // unreachable (the trampoline resumes at func+0x07).
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_resume)
            {
                Xbyak::Label invalid;
                mov(rcx, ptr[rcx + 0x208]);  // re-run displaced load: rcx = this->IVRSystem
                test(rcx, rcx);
                jz(invalid);  // IVRSystem null (HMD asleep / OpenVR not initialized)
                jmp(ptr[rip]);
                dq(a_resume);  // resume at func+0x07 (TEST EDX,EDX); the *rcx derefs only run when non-null
                L(invalid);
                mov(eax, 0xFFFFFFFF);  // vr::k_unTrackedDeviceIndexInvalid
                ret();
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;  // VR-specific function (raw hand-curated offset)

        REL::Relocation<std::uintptr_t> target{ REL::Offset(0xC53DF0) };  // BSOpenVR::GetTrackedDeviceIndexForHand

        detail::Patch p(target.address() + 0x7);  // resume at func+0x07
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(p));

        logger::info("installed BSOpenVR hand-index null crash fix (VR)"sv);
    }
}
