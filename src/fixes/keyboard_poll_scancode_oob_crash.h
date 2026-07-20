#pragma once

namespace Fixes::KeyboardPollScancodeOOBCrash
{
    // Vanilla out-of-bounds crash in BSWin32KeyboardDevice::Poll's buffered-event loop.
    //
    // For each buffered event GetDeviceData actually returned (the loop bound itself is correct),
    // the function reads dwOfs straight from the driver-filled DIDEVICEOBJECTDATA entry and uses it,
    // with NO range check, as a byte index into the fixed 256-byte prevState/curState arrays
    // (prevState[dwOfs], curState[dwOfs], ...). This is only safe because the DirectInput API
    // contract promises dwOfs is always a valid DIK_* scancode (<0x100) for a standard keyboard
    // device -- nothing in this function enforces that itself.
    //
    // Observed (crash-2026-07-19-16-03-49.log, AE): dwOfs came back as 0x74186963, nowhere near a
    // valid scancode, producing a wild out-of-bounds byte access -> EXCEPTION_ACCESS_VIOLATION at
    // func+0xF2. Root cause of the garbage dwOfs itself is not established (a third-party
    // input-hooking overlay or heap corruption elsewhere stomping the diObjData buffer between
    // GetDeviceData's fill and this read are the plausible categories).
    //
    // Handler is address library id 67472 (AE id 68782). Patch site = the `mov r14d,[..+dwOfs]`
    // load (flat: func+0xDB/+0xDC, index register rax, 5-byte instr; VR: func+0xE0, index register
    // rcx, 8-byte instr -- VR's extra BSIInputDevice field shifts diObjData from +0x78 to +0x80,
    // pushing the displacement past the imm8 encoding range). The trampoline re-runs the displaced
    // load, bounds-checks it, and either resumes right after the load (valid scancode) or jumps to
    // the loop's `uVar14 = uVar14 + 1; continue` point (flat func+0x232/+0x2F0; VR func+0x1D9),
    // skipping the malformed event entirely instead of trusting it.
    namespace detail
    {
        struct FlatPatch final : Xbyak::CodeGenerator
        {
            FlatPatch(std::uintptr_t a_resume, std::uintptr_t a_skip)
            {
                mov(r14d, dword[rbx + rax * 8 + 0x78]);  // re-run displaced load: r14d = diObjData[i].dwOfs
                cmp(r14d, 0x100);
                jae("invalid");
                jmp(ptr[rip]);
                dq(a_resume);
                L("invalid");
                jmp(ptr[rip]);
                dq(a_skip);
            }
        };

        struct VRPatch final : Xbyak::CodeGenerator
        {
            VRPatch(std::uintptr_t a_resume, std::uintptr_t a_skip)
            {
                mov(r14d, dword[rbx + rcx * 8 + 0x80]);  // re-run displaced load: r14d = diObjData[i].dwOfs
                cmp(r14d, 0x100);
                jae("invalid");
                jmp(ptr[rip]);
                dq(a_resume);
                L("invalid");
                jmp(ptr[rip]);
                dq(a_skip);
            }
        };
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(67472, 68782), VAR_NUM(0xDB, 0xDC, 0xE0) };
        REL::Relocation<std::uintptr_t> skip{ RELOCATION_ID(67472, 68782), VAR_NUM(0x232, 0x2F0, 0x1D9) };

        auto& trampoline = SKSE::GetTrampoline();

        if (REL::Module::IsVR()) {
            detail::VRPatch p(patch.address() + 0x8, skip.address());
            p.ready();
            patch.write_branch<5>(trampoline.allocate(p));
        } else {
            detail::FlatPatch p(patch.address() + 0x5, skip.address());
            p.ready();
            patch.write_branch<5>(trampoline.allocate(p));
        }

        logger::info("installed keyboard poll scancode out-of-bounds crash fix"sv);
    }
}
