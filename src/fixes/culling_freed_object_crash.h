#pragma once

// VR-only fix for use-after-free crashes in the cull traversal's per-object
// OnVisible dispatch, exposed by Community Shaders background shader compilation.
//
// Background-compile removes the blocking precompile screen, which also gated world
// rendering/culling during cell streaming. The cull traversal then runs while the
// cell loader frees scene objects, so it reaches a freed-and-reused NiAVObject and
// calls object->vftable[0x1A8] (OnVisible) on it -> the vftable is heap garbage
// (a real vftable is in .rdata) and the slot is null/garbage -> CTD. Debugger-
// confirmed (dbgeng attach to live VR): object->vftable held a heap pointer ~0x1C0
// bytes from the object itself, slot null -> RIP=0.
//
// The same dispatch is duplicated across several cull processes. All matching sites
// share the byte signature `CALL [RAX+0x1A8]` (RAX = object->vftable) immediately
// followed by `CMP byte [reg+0x11D]` (the cull accumulate flag), then a conditional
// `OR/AND [object+0x10C]` write -- so each is found and patched uniformly:
//
//   +XX+0:  CALL [RAX+0x1A8]              ; object->OnVisible(object, ...)   <-- patched
//   +XX+6:  CMP  byte [a_this+0x11D], 0   ; "post-call" resume for valid objects
//   ...     OR/AND dword [object+0x10C]   ; writes the OBJECT (2nd UAF if freed)
//   converge:                            ; rejoin that touches a_this only (safe)
//
// Each patch caves the 6-byte CALL to a guard: validate RAX (the vftable) lies
// inside the main module image. Valid -> perform the original call and resume at the
// post-call instruction. Freed -> jump to the converge point, skipping both the
// virtual call AND the [object+0x10C] write (the second UAF). The 5-byte branch's
// trailing byte is dead code (the cave only jumps to absolute targets), so no NOP
// fill is needed. R10 is volatile and not an argument register (RCX/RDX/R8/R9), so
// it is safe to clobber for the range check.

namespace Fixes::CullingFreedObjectCrash
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t callOffset;       // VR offset of the CALL [RAX+0x1A8]
            std::uintptr_t convergeOffset;   // VR offset to resume at when the object is freed
        };

        // BSCullingProcess / BSParabolicCullingProcess per-object cull + recursion sites.
        // (callOffset, convergeOffset) — see the table in the investigation notes.
        inline constexpr std::array<Site, 6> kSites{ {
            { 0xCBFD24, 0xCBFD52 },
            { 0xD99D3D, 0xD99D6B },
            { 0xD99E30, 0xD99E63 },
            { 0x136F23E, 0x136F26C },
            { 0x136F2BD, 0x136F2EB },
            { 0x136F5CA, 0x136F5E3 },
        } };

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_postCall, std::uintptr_t a_converge)
            {
                Xbyak::Label skipLbl, postAddr, convAddr;

                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

                // Valid vftable: perform the original call, resume after it.
                call(ptr[rax + 0x1A8]);
                jmp(ptr[rip + postAddr]);

                // Freed object: skip the call AND the [object+0x10C] write.
                L(skipLbl);
                jmp(ptr[rip + convAddr]);

                L(postAddr);
                dq(a_postCall);
                L(convAddr);
                dq(a_converge);
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;

        const auto moduleBase = REL::Module::get().base();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleBase);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(moduleBase + dos->e_lfanew);
        const auto moduleEnd = moduleBase + nt->OptionalHeader.SizeOfImage;

        auto& trampoline = SKSE::GetTrampoline();
        for (const auto& site : detail::kSites) {
            REL::Relocation<std::uintptr_t> call{ REL::Offset{ site.callOffset } };
            detail::Patch p{ moduleBase, moduleEnd, call.address() + 0x6,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ site.convergeOffset } }.address() };
            p.ready();
            call.write_branch<5>(trampoline.allocate(p));
        }

        logger::info("installed culling freed-object crash fix (VR, {} sites)"sv, detail::kSites.size());
    }
}
