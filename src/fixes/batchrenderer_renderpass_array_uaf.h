#pragma once

#include <algorithm>

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
// SE/AE split this logic into a separate helper (BSBatchRenderer::
// GetRenderPassIndex, called from BSShaderAccumulator::RenderBatches) rather
// than VR's monolithic function, with a different write shape (a 6-field
// zero-clear vs. VR's AND+MOV) -- same underlying bug, same guard, different
// patch site per runtime.
//
// Address-library coverage is uneven across runtimes for this function: SE
// and AE both anchor off catalogued IDs (100853 and 107643 respectively --
// separate numeric spaces per runtime); VR still uses a raw, disassembly-
// resolved offset (see the comment at that array below for why).

namespace Fixes::BatchRendererRenderPassArrayUAF
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t patchAddress;   // start of the AND+XOR+MOV block (9 bytes)
            std::uintptr_t resumeAddress;  // where both branches converge, right after the block
        };

        // BSBatchRenderer::RenderBatches -- address-library IDs 99963 and
        // 100853 both exist, but their catalogued VR addresses resolve to a
        // different function than the one actually containing this patch
        // site (confirmed by disassembly), so neither is a safe anchor here;
        // offset is resolved by disassembly and hardcoded until a correct ID
        // is found.
        inline std::array<Site, 1> SitesVR()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x1349543 } }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x134954C } }.address() },
            } };
        }

        // BSBatchRenderer::GetRenderPassIndex -- address-library ID 100853
        // resolves to this exact function's start on SE; patch/resume sites
        // are fixed deltas (0x57/0x6D) from that start.
        inline std::array<Site, 1> SitesSE()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x57 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x6D }.address() },
            } };
        }

        // BSBatchRenderer::GetRenderPassIndex -- AE uses a separate numeric ID
        // space from SE (see se_ae.csv), so SE's 100853 doesn't carry over;
        // ID 107643 resolves to this exact function's start on the 1.6.1170
        // target. Patch/resume are the same fixed deltas (0x57/0x6D) as SE,
        // since the two are byte-identical.
        inline std::array<Site, 1> SitesAE()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(107643), 0x57 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(107643), 0x6D }.address() },
            } };
        }

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
                jbe(skipLbl);

                and_(dword[rdx + 0x28], ebp);
                mov(qword[rdx + rcx * 8], rbx);

                L(skipLbl);
                jmp(ptr[rip + resumeAddr]);

                L(resumeAddr);
                dq(a_resume);
            }
        };

        // Full 9-byte block: AND [RDX+0x28],EBP; XOR EBX,EBX; MOV [RDX+RCX*8],RBX.
        // Validated in full (not just the leading opcode) since the patch
        // overwrites all 9 bytes and the skip path relies on nothing past
        // them having drifted.
        inline bool SiteMatchesVR(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x21, 0x6A, 0x28, 0x33, 0xDB, 0x48, 0x89, 0x1C, 0xCA };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        // 22-byte site (5 qword MOVs + 1 dword MOV from RDI), likewise too
        // small for a call-out.
        struct PatchSE final : Xbyak::CodeGenerator
        {
            PatchSE(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeAddr;

                cmp(rcx, kMinPlausiblePointer);
                jbe(skipLbl);

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

        // Full 22-byte block: 5x MOV qword [RCX+n],RDI, then MOV dword
        // [RCX+0x28],EDI. Validated in full for the same reason as
        // SiteMatchesVR -- the patch overwrites the whole block.
        inline bool SiteMatchesSE(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = {
                0x48, 0x89, 0x39, 0x48, 0x89, 0x79, 0x08, 0x48, 0x89, 0x79, 0x10,
                0x48, 0x89, 0x79, 0x18, 0x48, 0x89, 0x79, 0x20, 0x89, 0x79, 0x28
            };
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        template <class PatchT>
        inline std::size_t PatchSites(std::span<const Site> a_sites, bool (*a_matches)(std::uintptr_t))
        {
            auto&       trampoline = SKSE::GetTrampoline();
            std::size_t installed = 0;
            for (const auto& site : a_sites) {
                REL::Relocation<std::uintptr_t> patch{ site.patchAddress };
                if (!a_matches(patch.address())) {
                    logger::warn("batchrenderer renderpass array UAF fix: unexpected bytes at {:X}, skipping site"sv, site.patchAddress);
                    continue;
                }
                PatchT p{ site.resumeAddress };
                p.ready();
                patch.write_branch<5>(trampoline.allocate(p));
                ++installed;
            }
            return installed;
        }
    }

    inline void Install()
    {
        std::size_t installed = 0;
        if (REL::Module::IsVR()) {
            const auto sites = detail::SitesVR();
            installed = detail::PatchSites<detail::Patch>(sites, detail::SiteMatchesVR);
        } else if (REL::Module::IsAE()) {
            const auto sites = detail::SitesAE();
            installed = detail::PatchSites<detail::PatchSE>(sites, detail::SiteMatchesSE);
        } else {
            const auto sites = detail::SitesSE();
            installed = detail::PatchSites<detail::PatchSE>(sites, detail::SiteMatchesSE);
        }

        if (installed > 0) {
            logger::info("installed batchrenderer renderpass array UAF fix"sv);
        } else {
            logger::warn("batchrenderer renderpass array UAF fix: no sites matched, not installed"sv);
        }
    }
}
