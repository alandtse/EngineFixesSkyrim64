#pragma once

// VR-only fix for a shadow scene null-pointer crash at VR offset 0x12F86DD.
// This is distinct from the SE/AE shadowscenenode_nullptr_crash fix.
//
// Context (from Ghidra disassembly of SkyrimVR.exe):
//
//   1412f86da: MOV RAX, [RDX]          ; rax = vtable of BSLight*  (before patch site)
//   1412f86dd: MOV RDI, RCX            ; ← patch site (5-byte JMP here)
//   1412f86e0: MOV RCX, RDX            ; set up call arg
//   1412f86e3: MOV RBX, RDX            ; rbx = BSLight* (used later at +0x48)
//   1412f86e6: CALL [RAX + 0x18]       ; vtable call — crashes if RAX (vtable) is null
//   1412f86e9: TEST AL, AL
//   1412f86eb: JZ  0x1412f8703         ; if false → block that uses RBX
//   ...
//   1412f8703: MOV RAX, [RBX + 0x48]   ; uses RBX (BSLight*)
//   ...
//   1412f8780: MOV RBX, [RSP+0x38]     ; function epilogue / clean return
//              ADD RSP, 0x20
//              POP RDI
//              RET
//
// The 5-byte JMP skips the 3 MOVs at +0, +3, +6 (9 bytes total before the CALL).
// The cave MUST replicate those MOVs — otherwise RBX is never set to RDX and
// any code path that reaches 0x1412f8703 faults on [RBX+0x48] with RBX=0.
//
// Offsets relative to a_target (0x1412f86DD):
//   +0x9  = 0x1412f86E6 : CALL [RAX+0x18]  (resume here when RAX is valid)
//   +0xA3 = 0x1412f8780 : function epilogue (skip-to when RAX is null)

namespace Fixes::ShadowSceneCrash
{
    namespace detail
    {
        struct Patch : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_target)
            {
                Xbyak::Label callAddrLbl, skipLbl, skipAddrLbl;

                // Replicate the 3 MOV instructions overwritten by the 5-byte JMP.
                mov(rdi, rcx);  // MOV RDI, RCX  (save ShadowSceneNode*)
                mov(rcx, rdx);  // MOV RCX, RDX  (call arg = BSLight*)
                mov(rbx, rdx);  // MOV RBX, RDX  (save BSLight* for use after call)

                // RAX = vtable (loaded from [RDX] before the patch site).
                // Load the specific function pointer and null-check it, not just the vtable.
                // R8 is volatile (caller-saved), safe to clobber here.
                mov(r8, ptr[rax + 0x18]);
                test(r8, r8);
                jz(skipLbl);  // conditional branch must target a local Label

                // Function pointer is valid: resume at the original CALL instruction.
                jmp(ptr[rip + callAddrLbl]);

                L(skipLbl);
                jmp(ptr[rip + skipAddrLbl]);

                L(callAddrLbl);
                dq(a_target + 0x9);   // CALL qword ptr [RAX+0x18] at 0x1412f86E6

                L(skipAddrLbl);
                dq(a_target + 0xA3);  // function epilogue at 0x1412f8780 (safe return)
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;

        // VR raw offset — distinct from the SE/AE shadow scene nullptr crash
        REL::Relocation<std::uintptr_t> target{ REL::Offset{ 0x12F86DD } };

        detail::Patch p{ target.address() };
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        // write_branch writes a 5-byte JMP at the target site.
        // Bytes 5-8 become unreachable dead code (not executed), so no explicit NOP fill needed.
        target.write_branch<5>(trampoline.allocate(p));

        logger::info("installed shadow scene crash fix (VR)"sv);
    }
}
