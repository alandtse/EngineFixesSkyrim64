#pragma once

// VR-only crash: the kUpdate init block in ProcessMessage calls Init3DElements
// and UpdateAngle every frame until init3DElements==true.  Init3DElements fires
// async RequestModel2 calls then immediately dereferences the resulting _entry
// pointers.  UpdateAngle later in the same block does the same for pickModel.
// In VR, AppResourceCaching::Pause does not flush the load queue, so both
// functions crash with null _entry on the first frame.
//
//   Init3DElements crash site: 0x1408C49A1  (lockpickShiv._entry == null)
//   UpdateAngle crash site:    0x1408C68E9  (pickModel._entry    == null)
//
// Two call-site guards share one readiness predicate:
//  (1) The CALL AppResourceCaching::Pause inside Init3DElements is replaced by
//      a thunk: if entries are not yet loaded, jump to the function epilogue so
//      RequestModel2 has already run (async load continues) but no dereference
//      follows.  init3DElements stays false so the block retries next frame.
//  (2) The CALL UpdateAngle inside ProcessMessage's init block is replaced by a
//      wrapper that skips the call until pickModel._entry is populated, covering
//      the first frame before (1) can ever succeed.

namespace Fixes::LockpickingMenuInitCrash
{
    namespace detail
    {
        [[nodiscard]] inline bool ModelsReady(RE::LockpickingMenu* a_this) noexcept
        {
            auto& rtd = a_this->GetRuntimeData();
            return rtd.lockpickShiv && rtd.pickModel &&
                   rtd.lockpickShiv->data && rtd.pickModel->data;
        }

        // Replaces CALL AppResourceCaching::Pause inside Init3DElements.
        // Entered via JMP (write_branch replaced CALL); RSP is 16-aligned with
        // no return address on the stack.
        struct PauseGuardThunk : Xbyak::CodeGenerator
        {
            explicit PauseGuardThunk(std::uintptr_t a_pause, std::uintptr_t a_resume, std::uintptr_t a_epilogue)
            {
                Xbyak::Label readyLbl, pauseAddrLbl, resumeAddrLbl, epilogueAddrLbl;

                // 0x20 preserves 16-byte alignment (0x20 % 16 == 0); no padding needed
                // because the JMP entry did not push a return address.
                sub(rsp, 0x20);
                mov(rcx, rdi);
                mov(rax, reinterpret_cast<std::uintptr_t>(ModelsReady));
                call(rax);
                add(rsp, 0x20);

                test(al, al);
                jnz(readyLbl);
                jmp(ptr[rip + epilogueAddrLbl]);  // entries not ready: skip body, allow retry

                L(readyLbl);
                // Simulate the original CALL: push return address so Pause's RET
                // resumes Init3DElements normally rather than returning to the thunk.
                mov(rax, ptr[rip + resumeAddrLbl]);
                push(rax);  // RSP to 8 mod 16, matching callee ABI expectation
                jmp(ptr[rip + pauseAddrLbl]);

                L(pauseAddrLbl);
                dq(a_pause);
                L(resumeAddrLbl);
                dq(a_resume);
                L(epilogueAddrLbl);
                dq(a_epilogue);
            }
        };

        inline REL::Relocation<void(RE::LockpickingMenu*, float)> origUpdateAngle;

        // Replaces CALL UpdateAngle in ProcessMessage's init block.
        inline void guardedUpdateAngle(RE::LockpickingMenu* a_this, float a_angle)
        {
            if (ModelsReady(a_this))
                origUpdateAngle(a_this, a_angle);
        }
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;

        auto& trampoline = SKSE::GetTrampoline();

        // (1) Replace CALL AppResourceCaching::Pause inside Init3DElements.
        //     VR: patch site 0x8C4830, Pause fn 0x5A3230, epilogue 0x8C4C56.
        {
            REL::Relocation<std::uintptr_t> patchSite{ REL::Offset{ 0x8C4830 } };
            const auto                      resumeAddr = patchSite.address() + 5;
            const auto                      epilogueAddr = REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x8C4C56 } }.address();
            const auto                      pauseAddr = REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x5A3230 } }.address();

            detail::PauseGuardThunk thunk{ pauseAddr, resumeAddr, epilogueAddr };
            thunk.ready();
            patchSite.write_branch<5>(trampoline.allocate(thunk));
        }

        // (2) Replace CALL UpdateAngle inside ProcessMessage's init block.
        //     VR: patch site 0x8C3F2C.
        {
            REL::Relocation<std::uintptr_t> patchSite{ REL::Offset{ 0x8C3F2C } };
            detail::origUpdateAngle = patchSite.write_call<5>(detail::guardedUpdateAngle);
        }

        logger::info("installed lockpicking menu init crash fix (VR)"sv);
    }
}
