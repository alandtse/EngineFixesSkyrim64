#pragma once

// Defensive guard against EXCEPTION_ACCESS_VIOLATION inside
// BSGraphics::TriShape::Release when its indexBuffer / vertexBuffer fields
// have been corrupted (overwritten with non-pointer data) by an upstream bug.
//
// The function is the per-mesh ID3D11 buffer wrapper destructor; it runs from
// NiSkinPartition::Partition::dtor → BSTriShape dtor chains during normal
// scene cleanup. Layout (matches CommonLib RE/N/NiSkinPartition.h:14-25):
//
//   struct BSGraphics::TriShape {  // sizeof 0x30
//       ID3D11Buffer*      vertexBuffer;   // 0x00
//       ID3D11Buffer*      indexBuffer;    // 0x08
//       BSGraphics::VertexDesc vertexDesc; // 0x10
//       volatile uint32_t  refCount;       // 0x18
//       uint32_t           pad1C;          // 0x1C
//       uint8_t*           rawVertexData;  // 0x20
//       uint16_t*          rawIndexData;   // 0x28
//   };
//
// Disasm of the relevant range. SE 0x140D6C320 and VR 0x140DBE0D0 use a 3-byte
// `OR EAX, 0xFFFFFFFF` for the refcount-decrement seed; AE 0x140E46810 uses
// the 5-byte `MOV EAX, 0xFFFFFFFF`, which shifts every following instruction
// by +0x02. The patch-relative jump-back delta is the same +0x1D in all three.
//
// SE/VR layout (patch site +0x1B):
//   +0x1B  MOV RCX, [RDX+0x8]    ; this->indexBuffer            ← patch site
//   +0x1F  TEST RCX, RCX
//   +0x22  JZ +0x2A
//   +0x24  MOV RAX, [RCX]        ; vtable
//   +0x27  CALL [RAX+0x10]       ; IUnknown::Release  ← OBSERVED CRASH (VR)
//   +0x2A  MOV RCX, [RBX]        ; this->vertexBuffer
//   +0x2D  TEST RCX, RCX
//   +0x30  JZ +0x38
//   +0x32  MOV RAX, [RCX]
//   +0x35  CALL [RAX+0x10]       ; same crash class
//   +0x38  MOV RCX, [RBX+0x20]   ; rawVertexData free path      ← jump back
//
// AE layout (patch site +0x1D, jump-back +0x3A): identical structure shifted
// by +0x02 from SE/VR.
//
// Observed failure (VR, 2026-04-28): indexBuffer = 0x29376EECA7C
// (8-byte-misaligned), [indexBuffer] = 0x3F800000 (= float 1.0), so the
// CALL at +0x27 dereferenced 0x3F800010 and faulted. RBX (this) was a valid
// NiSkinPartition::Partition::buffData pointer; the indexBuffer field of that
// TriShape was corrupted in-place by an upstream writer. Neither Community
// Shaders nor hdtSMP touch this field; the corruption originates elsewhere
// (engine race, other mod, or use-after-free).
//
// Mitigation: hook +0x1B with a 5-byte JMP into a cave that re-implements
// the indexBuffer + vertexBuffer release with two cheap ABI-grounded checks:
//   1. Buffer pointer is 8-byte aligned (heap allocations always are).
//   2. The dereferenced vtable pointer is 8-byte aligned (vtables in PE
//      .rdata are pointer-aligned).
// On either check failing the COM pointer is silently skipped; the rest of
// the destructor (raw data free, operator delete) still runs.
//
// Net effect of a triggered guard: at most a single leaked ID3D11 reference
// (the buffer was already disowned by the partition; without the original
// COM pointer there is no safe way to release it). Process survives.
//
// Bytes +0x20..+0x37 of the original function become unreachable after the
// JMP at +0x1B; no NOP fill required since no external branch lands inside
// that span (the only inbound branches there originate from instructions
// inside the very block we are replacing).

namespace Fixes::TriShapeReleaseBufferGuard
{
    namespace detail
    {
        struct Patch : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_target)
            {
                Xbyak::Label backLbl, backAddrLbl;
                Xbyak::Label idxSkip, vtxSkip, vtxLoad;

                // --- indexBuffer (this->indexBuffer @ rbx+0x8) ---
                mov(rcx, ptr[rbx + 0x8]);
                test(rcx, rcx);
                jz(idxSkip);
                test(cl, 0x7);          // 8-byte alignment check (low 3 bits)
                jnz(idxSkip);
                mov(rax, ptr[rcx]);     // vtable
                test(rax, rax);
                jz(idxSkip);
                test(al, 0x7);          // vtable alignment check
                jnz(idxSkip);
                call(ptr[rax + 0x10]);  // IUnknown::Release
                L(idxSkip);

                // --- vertexBuffer (this->vertexBuffer @ rbx+0x0) ---
                mov(rcx, ptr[rbx]);
                test(rcx, rcx);
                jz(vtxSkip);
                test(cl, 0x7);
                jnz(vtxSkip);
                mov(rax, ptr[rcx]);
                test(rax, rax);
                jz(vtxSkip);
                test(al, 0x7);
                jnz(vtxSkip);
                call(ptr[rax + 0x10]);
                L(vtxSkip);

                // Resume at the rawVertexData free path (+0x38 from func base
                // = +0x1D from a_target).
                jmp(ptr[rip + backAddrLbl]);

                L(backAddrLbl);
                dq(a_target + 0x1D);
            }
        };
    }

    inline void Install()
    {
        // BSGraphics::TriShape::Release function:
        //   SE 1.5.97 ID 75480 -> 0x140D6C320
        //   AE 1.6.x  ID 77267 -> 0x140E46810
        //   VR 1.4.15         -> 0x140DBE0D0  (VR uses SE id 75480 via address library)
        //
        // Patch site offset within the function:
        //   SE +0x1B, AE +0x1D (AE has 5-byte MOV EAX,-1 vs 3-byte OR EAX,-1), VR +0x1B
        // Same +0x1D patch-relative jump-back delta in all three -> single cave works.
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(75480, 77267),
            VAR_NUM(0x1B, 0x1D, 0x1B) };

        detail::Patch p{ target.address() };
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<5>(trampoline.allocate(p));

        logger::info("installed trishape release buffer guard"sv);
    }
}
