#pragma once

// BSBatchRenderer keeps a technique-ID -> index map (renderPassMap) alongside
// the array it indexes into (renderPass / a_this->shaderProperty on SE/AE).
// If that backing storage is freed/reallocated while the map still holds a
// populated index for a technique ID, the computed pointer is a small,
// freed-derived value and the write through it corrupts memory or crashes.
// Distinct from renderpass_cache.h (defers individual BSRenderPass frees) and
// culling_freed_object_crash.h (guards NiAVObject vftable reads in cull/detach)
// -- neither covers this array's own backing storage.
//
// A heap pointer can't be range-checked against the module image the way a
// vftable can; the guard instead checks the pointer against a floor no real
// allocation ever returns and skips the write if it looks freed.
//
// SE/AE split this logic into a separate helper (BSBatchRenderer::sub_*,
// called from BSShaderAccumulator::RenderBatches) rather than VR's monolithic
// function, with a different write shape (a 6-field zero-clear vs. VR's
// AND+MOV) -- same underlying bug, same guard, different patch site per
// runtime. AE's site has no address-library ID cataloguing it by name yet;
// its offset was resolved by disassembly and is hardcoded until one exists.

namespace Fixes::BatchRendererRenderPassArrayUAF
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t patchOffset;   // start of the AND+XOR+MOV block (9 bytes)
            std::uintptr_t resumeOffset;  // where both branches converge, right after the block
        };

        // BSBatchRenderer::RenderBatches
        inline constexpr std::array<Site, 1> kSitesVR{ {
            { 0x1349543, 0x134954C },
        } };

        // BSBatchRenderer::sub_1413083B0
        inline constexpr std::array<Site, 1> kSitesSE{ {
            { 0x1308407, 0x130841D },
        } };

        // BSBatchRenderer::sub (static addr 0x1414f3d30); not yet
        // address-library-catalogued, adjacent to the known
        // sub_SE100852_AE107642 at 0x1414f39c0.
        inline constexpr std::array<Site, 1> kSitesAE{ {
            { 0x14F3D87, 0x14F3D9D },
        } };

        // No pointer returned by any real allocator will ever be this low; a
        // computed PassGroup pointer at or below this floor can only be
        // renderPass._data == nullptr plus a small index*sizeof(PassGroup) offset.
        inline constexpr std::uintptr_t kMinPlausiblePointer = 0x10000;

        // 9-byte site (AND [rdx+0x28],ebp; xor ebx,ebx; mov [rdx+rcx*8],rbx)
        // is too small for a call-out, so this reproduces the block inline.
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeAddr;

                // EBX is persisted into 3 globals downstream; every original
                // path zeroes it before reaching them, so this must too.
                xor_(ebx, ebx);

                cmp(rdx, kMinPlausiblePointer);
                jb(skipLbl);

                and_(dword[rdx + 0x28], ebp);
                mov(qword[rdx + rcx * 8], rbx);

                L(skipLbl);
                jmp(ptr[rip + resumeAddr]);

                L(resumeAddr);
                dq(a_resume);
            }
        };

        // AND dword ptr [RDX+0x28],EBP -- guards against offset drift
        // patching an unrelated instruction stream.
        inline bool SiteMatchesVR(std::uintptr_t a_addr)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return p[0] == 0x21 && p[1] == 0x6A && p[2] == 0x28;
        }

        // 22-byte site (5 qword MOVs + 1 dword MOV from RDI), likewise too
        // small for a call-out.
        struct PatchSE final : Xbyak::CodeGenerator
        {
            PatchSE(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeAddr;

                cmp(rcx, kMinPlausiblePointer);
                jb(skipLbl);

                // RDI is zeroed at function entry and never reassigned
                // before this site, and is restored to the caller's value
                // unconditionally before the tail-call -- safe to reuse
                // here without preserving it separately.
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
            detail::PatchSites<detail::PatchSE>(detail::kSitesAE, detail::SiteMatchesSE);
        else
            detail::PatchSites<detail::PatchSE>(detail::kSitesSE, detail::SiteMatchesSE);

        logger::info("installed batchrenderer renderpass array UAF fix"sv);
    }
}
