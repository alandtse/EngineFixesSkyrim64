#pragma once

// Fix for a use-after-free crash in BSBatchRenderer::RenderBatches, exposed by
// Community Shaders background shader compilation (same storm as the renderpass
// cache and cull-traversal fixes -- background-compile removes the blocking
// precompile stall that previously narrowed this race window).
//
// BSBatchRenderer holds a technique-ID -> index map (renderPassMap) alongside a
// BSTArray<PassGroup*> (renderPass) that the index refers into. The crash site
// computes `renderPass._data + index*sizeof(PassGroup)` and writes through it
// (AND [ptr+0x28],EBP -- clearing a bit in PassGroup::validPassBits) without
// checking that renderPass._data is still valid. If the pass-group storage is
// freed/reallocated (a heap free of the array's backing storage was seen racing
// in flight during a cell transition -- RtlFreeHeap/_free_base frames near the
// top of the faulting stack) while renderPassMap still holds a populated index
// for that technique ID, renderPass._data is NULL and the computed pointer is
// just `index * sizeof(PassGroup)` -- a small, obviously-invalid address.
// Debugger-confirmed (minidump .cxr): at fault, RDX (the computed pointer) was
// 0x30, i.e. renderPass._data was NULL and index was 1 (sizeof(PassGroup) ==
// 0x30). This is a map-populated / backing-array-freed desync, not an
// out-of-bounds garbage index -- the map itself is intact and correctly
// resolved a real technique ID.
//
// This is a distinct site from the two other UAF fixes already covering this
// same background-compile storm:
//   - renderpass_cache.h: defers the free of individual BSRenderPass objects.
//   - culling_freed_object_crash.h: guards NiAVObject vftable dereferences in
//     the cull/detach traversal.
// Neither touches this array-of-PassGroup allocation.
//
// The guard validates the computed pointer is not a small, obviously-freed-NULL-
// derived value before performing the write, and skips straight to the next
// safe instruction (which does not depend on the pointer) if it looks invalid.
// A heap pointer can't be range-checked against the module image the way a
// vftable can (culling_freed_object_crash.h's technique); instead this checks
// the pointer is above a floor no real allocation will ever return, which
// exactly matches the confirmed failure signature (RDX == 0x30).
//
// VR's crash was reproduced and confirmed via minidump. VR's stereo rendering
// doubles batch-render traversals (wider race window, same reasoning as
// culling_freed_object_crash.h), so VR is where this reliably bites; SE/AE
// likely share the same underlying bug at a lower frequency.
//
// SE: RenderBatches is split differently than VR's monolithic function -- the
// vulnerable technique-map-lookup-then-array-write lives in a separate helper,
// BSBatchRenderer::sub_1413083B0 (called from BSShaderAccumulator::RenderBatches).
// Ghidra-confirmed via raw disassembly: the helper computes a
// NiShadeProperty-shaped pointer as `a_this->shaderProperty + index*sizeof(NiShadeProperty)`
// (RCX, finalized by `ADD RCX,[R10+8]`) and unconditionally zero-clears 6
// fields through it (~40 bytes: 5 qword MOVs + 1 dword MOV, all from RDI,
// which is zeroed at function entry and never reassigned before this point)
// with no validity check on the computed pointer at all -- same failure mode
// as VR: if the backing array's storage is freed/reallocated while the map
// still holds a stale index, this pointer is a small freed-derived value and
// the write corrupts/crashes. Unlike VR, the index register (EBX) is NOT
// persisted anywhere past this function -- it's restored to the caller's
// original RBX from the stack, and RDI is POP'd back to the caller's value
// too, both unconditionally regardless of which branch is taken -- so there's
// no hidden side-effect to preserve here, just the guard.
//
// AE: not yet resolved -- TODO, not yet installed.

namespace Fixes::BatchRendererRenderPassArrayUAF
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t patchOffset;   // start of the AND+XOR+MOV block (9 bytes)
            std::uintptr_t resumeOffset;  // where both branches converge, right after the block
        };

        // (patchOffset, resumeOffset). VR-confirmed via Ghidra disassembly of
        // BSBatchRenderer::RenderBatches.
        inline constexpr std::array<Site, 1> kSitesVR{ {
            { 0x1349543, 0x134954C },
        } };

        // (patchOffset, resumeOffset). SE-confirmed via Ghidra disassembly of
        // BSBatchRenderer::sub_1413083B0. AE not yet resolved.
        inline constexpr std::array<Site, 1> kSitesSE{ {
            { 0x1308407, 0x130841D },
        } };

        // No pointer returned by any real allocator will ever be this low; a
        // computed PassGroup pointer at or below this floor can only be
        // renderPass._data == nullptr plus a small index*sizeof(PassGroup) offset.
        inline constexpr std::uintptr_t kMinPlausiblePointer = 0x10000;

        // The site is a 9-byte block (AND [rdx+0x28],ebp; xor ebx,ebx;
        // mov [rdx+rcx*8],rbx) -- too small for a naive call-out, so this patch
        // consumes the whole block and reproduces the original instructions
        // itself in the valid-pointer branch, rather than patching just the
        // first instruction and calling back in (which wouldn't fit in 3 bytes).
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeAddr;

                // `xor ebx,ebx` is NOT just scratch setup for the write below --
                // RBX/EBX gets persisted into three globals a few instructions
                // after the resume point (a "last processed pass" cache), and
                // the original code guarantees it's always zero here on EVERY
                // path through this region (this block's own xor, or the
                // xor on the function's other pre-existing branch that also
                // converges before those global writes). Zero it unconditionally
                // so the guard preserves that invariant instead of leaking
                // whatever stale value RBX held before this site.
                xor_(ebx, ebx);

                cmp(rdx, kMinPlausiblePointer);
                jb(skipLbl);

                // Valid pointer: reproduce the original write (already zeroed
                // ebx above, so just the AND and the MOV remain).
                and_(dword[rdx + 0x28], ebp);
                mov(qword[rdx + rcx * 8], rbx);

                // renderPass._data was freed: skip straight here either way,
                // resuming at the next instruction -- ebx is already zeroed,
                // and rdx is not touched again.
                L(skipLbl);
                jmp(ptr[rip + resumeAddr]);

                L(resumeAddr);
                dq(a_resume);
            }
        };

        // Expected bytes at the site: AND dword ptr [RDX+0x28],EBP == 21 6A 28
        // (the start of the 9-byte block). Guards against offset drift
        // corrupting an unrelated instruction stream.
        inline bool SiteMatchesVR(std::uintptr_t a_addr)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return p[0] == 0x21 && p[1] == 0x6A && p[2] == 0x28;
        }

        // The SE site is a 22-byte block (mov [rcx],rdi; mov [rcx+8],rdi;
        // mov [rcx+0x10],rdi; mov [rcx+0x18],rdi; mov [rcx+0x20],rdi;
        // mov dword ptr [rcx+0x28],edi) -- likewise too small for a call-out,
        // consumed and reproduced the same way as VR's block.
        struct PatchSE final : Xbyak::CodeGenerator
        {
            PatchSE(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeAddr;

                cmp(rcx, kMinPlausiblePointer);
                jb(skipLbl);

                // Valid pointer: reproduce the original zero-clear. RDI is
                // known zero here (zeroed at function entry, never
                // reassigned before this site) and is POP'd back to the
                // caller's value unconditionally before the tail-call at
                // the resume point, so using it as the zero source here
                // doesn't need any special preservation.
                mov(qword[rcx], rdi);
                mov(qword[rcx + 0x8], rdi);
                mov(qword[rcx + 0x10], rdi);
                mov(qword[rcx + 0x18], rdi);
                mov(qword[rcx + 0x20], rdi);
                mov(dword[rcx + 0x28], edi);

                // Backing storage was freed: skip straight here either way.
                L(skipLbl);
                jmp(ptr[rip + resumeAddr]);

                L(resumeAddr);
                dq(a_resume);
            }
        };

        // Expected bytes at the SE site: MOV [RCX],RDI == 48 89 39.
        inline bool SiteMatchesSE(std::uintptr_t a_addr)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return p[0] == 0x48 && p[1] == 0x89 && p[2] == 0x39;
        }

        template <class PatchT>
        inline void PatchSites(std::span<const Site> a_sites, bool (*a_matches)(std::uintptr_t))
        {
            auto& trampoline = SKSE::GetTrampoline();
            for (const auto& site : a_sites) {
                REL::Relocation<std::uintptr_t> patch{ REL::Offset{ site.patchOffset } };
                if (!a_matches(patch.address())) {
                    logger::warn("batchrenderer renderpass array UAF fix: unexpected bytes at {:X}, skipping site"sv, site.patchOffset);
                    continue;
                }
                PatchT p{ REL::Relocation<std::uintptr_t>{ REL::Offset{ site.resumeOffset } }.address() };
                p.ready();
                patch.write_branch<5>(trampoline.allocate(p));
            }
        }
    }

    inline void Install()
    {
        if (REL::Module::IsVR())
            detail::PatchSites<detail::Patch>(detail::kSitesVR, detail::SiteMatchesVR);
        else if (REL::Module::IsAE())
            logger::info("batchrenderer renderpass array UAF fix: AE site not yet resolved, skipping"sv);
        else
            detail::PatchSites<detail::PatchSE>(detail::kSitesSE, detail::SiteMatchesSE);

        logger::info("installed batchrenderer renderpass array UAF fix"sv);
    }
}
