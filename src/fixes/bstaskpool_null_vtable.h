#pragma once

// Fix for a BSTaskPool_HandleTask case-0x33 null-vtable crash (SE + AE + VR).
//
// BSTaskPool_HandleTask (SE ID 36016, AE ID 36991) case 0x33 processes a
// pathfinding task that holds an actor pointer in R14 and a BSFadeNode pointer
// in RBX. If the actor is freed between task enqueue and execution its vtable
// pointer ([R14]) will be null. The case executes `MOV RAX,[R14]` +
// `CALL [RAX+0x380]`, crashing with EXCEPTION_ACCESS_VIOLATION (read at 0x380).
//
// The existing null-check (TEST RBX,RBX / JZ) guards only the BSFadeNode, not
// the actor vtable. No vtable-validity guard exists for R14.
//
// Fix: hook at `MOV RAX,[R14]`, check whether [R14] == 0. If null:
//   1. Zero R14 (so the later `TEST R14,R14 / JZ epilogue` skips R14 processing)
//   2. Jump to the XADD.LOCK ref-count decrement block (+0x1E from patch site),
//      which properly releases the BSFadeNode reference and calls its destructor
//      if needed. Nothing in that block reads R14, so zeroing it is safe.
// If valid, replicate the two overwritten instructions and resume normally.
//
// Patch site offsets from function base:
//   SE  1.5.97: +0x1350  (5C6EE0 + 1350 = 5C8230)
//   AE  1.6.x:  +0x14C8  (656F30 + 14C8 = 6583F8)
//   VR  1.4.15: +0x13A7  (5CF480 + 13A7 = 5D0827)
//
// All runtimes overwrite 5 bytes of:
//   49 8B 06    MOV RAX, [R14]   (3 bytes, fully covered)
//   49 8B CE    MOV RCX, R14     (3 bytes, first 2 covered; byte +5 = 0xCE is
//                                 unreachable dead code after the 5-byte JMP)
// Jump-back (valid vtable): patch+0x6   — CALL qword ptr [RAX+0x380]
// Jump-to (null vtable):    patch+0x1E  — XADD.LOCK (same offset in all runtimes)

namespace Fixes::BSTaskPoolNullVtableCrash
{
    namespace detail
    {
        struct Patch : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_target)
            {
                Xbyak::Label vtableNullLbl, callAddrLbl, decrAddrLbl;

                // Replicate first overwritten instruction: MOV RAX, [R14]
                mov(rax, ptr[r14]);

                // Vtable is null: zero R14 and fall through to the ref-count block.
                // Zeroing R14 makes the subsequent TEST R14,R14 take JZ to epilogue,
                // skipping the second vtable call while BSFadeNode is still cleaned up.
                test(rax, rax);
                jz(vtableNullLbl);

                // Vtable is valid — replicate second overwritten instruction and resume
                mov(rcx, r14);
                jmp(ptr[rip + callAddrLbl]);

                L(vtableNullLbl);
                xor_(r14, r14);  // R14 = 0 → TEST R14,R14 will JZ to epilogue
                jmp(ptr[rip + decrAddrLbl]);

                L(callAddrLbl);
                dq(a_target + 0x6);  // CALL qword ptr [RAX+0x380]

                L(decrAddrLbl);
                dq(a_target + 0x1E);  // MOV EAX,ESI / XADD.LOCK ref-count decrement
            }
        };
    }

    inline void Install()
    {
        // SE ID 36016 / AE ID 36991 / VR ID 36016 (SE ID used for VR address library)
        // Patch offsets from function base: SE +0x1350, AE +0x14C8, VR +0x13A7
        const auto func_offset = VAR_NUM(0x1350, 0x14C8, 0x13A7);

        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(36016, 36991), func_offset };

        detail::Patch p{ target.address() };
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(p));
        // Byte at target+5 (0xCE, orphaned tail of MOV RCX,R14) is unreachable.

        logger::info("installed BSTaskPool null vtable crash fix"sv);
    }
}
