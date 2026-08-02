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
// All 3 runtimes have TWO vulnerable functions that write through this
// pointer, and both are patched on all 3 runtimes:
//   - BSBatchRenderer::ApplyPassAlphaCullState -- a 9-byte
//     AND+XOR+MOV block. Byte-identical on SE and VR; AE encodes the MOV's
//     index register differently (RAX instead of RCX), so it gets its own
//     Patch/matcher pair.
//   - BSBatchRenderer::GetRenderPassIndex -- a 22-byte 6-field zero-clear.
//     Byte-identical across all 3 runtimes.
//
// Address-library coverage: SE/AE anchor both sites off catalogued IDs
// (100852/100853 on SE, 107642/107643 on AE). VR's ApplyPassAlphaCullState
// anchors off SE's 100852 (verified to resolve to the correct VR function
// start via disassembly); VR's GetRenderPassIndex still uses a raw,
// disassembly-resolved offset because the address-library's VR column for
// 100853 currently duplicates 100852's address instead of pointing at
// GetRenderPassIndex (data bug, fix pending upstream release).

namespace Fixes::BatchRendererRenderPassArrayUAF
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t patchAddress;   // start of the patched block
            std::uintptr_t resumeAddress;  // where both branches converge, right after the block
        };

        // BSBatchRenderer::ApplyPassAlphaCullState -- address-library ID
        // 100852 resolves to this function's start on both SE and VR;
        // patch/resume are fixed deltas (0x2D3/0x2DC) from that start.
        inline std::array<Site, 1> SitesVRApplyPassAlphaCullState()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2D3 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2DC }.address() },
            } };
        }

        // BSBatchRenderer::GetRenderPassIndex -- database.csv's ID 100853
        // currently maps to the wrong VR address (a duplicate of 100852);
        // until that's corrected and released, this is anchored by a raw,
        // disassembly-resolved offset. Function start 0x13495F0, patch/
        // resume at the same +0x57/+0x6D deltas used on SE/AE (the block is
        // byte-identical to theirs).
        inline std::array<Site, 1> SitesVRGetRenderPassIndex()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x1349647 } }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x134965D } }.address() },
            } };
        }

        // BSBatchRenderer::ApplyPassAlphaCullState -- address-library ID
        // 100852 resolves to this function's start on SE; patch/resume are
        // fixed deltas (0x2D3/0x2DC) from that start.
        inline std::array<Site, 1> SitesSEApplyPassAlphaCullState()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2D3 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2DC }.address() },
            } };
        }

        // BSBatchRenderer::GetRenderPassIndex -- address-library ID 100853
        // resolves to this exact function's start on SE; patch/resume sites
        // are fixed deltas (0x57/0x6D) from that start.
        inline std::array<Site, 1> SitesSEGetRenderPassIndex()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x57 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x6D }.address() },
            } };
        }

        // BSBatchRenderer::ApplyPassAlphaCullState -- AE uses a separate
        // numeric ID space from SE (see se_ae.csv); ID 107642 resolves to
        // this function's start on the 1.6.1170 target. Patch/resume are the
        // same fixed deltas (0x2CA/0x2D3) as the block's offset within the
        // function.
        inline std::array<Site, 1> SitesAEApplyPassAlphaCullState()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(107642), 0x2CA }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(107642), 0x2D3 }.address() },
            } };
        }

        // BSBatchRenderer::GetRenderPassIndex -- AE uses a separate numeric ID
        // space from SE (see se_ae.csv), so SE's 100853 doesn't carry over;
        // ID 107643 resolves to this exact function's start on the 1.6.1170
        // target. Patch/resume are the same fixed deltas (0x57/0x6D) as SE,
        // since the two are byte-identical.
        inline std::array<Site, 1> SitesAEGetRenderPassIndex()
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

        // ApplyPassAlphaCullState's 9-byte site (AND [rdx+0x28],ebp; xor
        // ebx,ebx; mov [rdx+rcx*8],rbx) is too small for a call-out, so this
        // reproduces the block inline. Byte-identical on SE and VR.
        struct PatchApplyPassRcx final : Xbyak::CodeGenerator
        {
            PatchApplyPassRcx(std::uintptr_t a_resume)
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

        // Full 9-byte block: AND [RDX+0x28],EBP; XOR EBX,EBX; MOV
        // [RDX+RCX*8],RBX. Validated in full (not just the leading opcode)
        // since the patch overwrites all 9 bytes and the skip path relies on
        // nothing past them having drifted. Matches SE and VR.
        inline bool SiteMatchesApplyPassRcx(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x21, 0x6A, 0x28, 0x33, 0xDB, 0x48, 0x89, 0x1C, 0xCA };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        // Same 9-byte shape as PatchApplyPassRcx, but AE encodes the MOV's
        // index register as RAX instead of RCX (mov [rdx+rax*8],rbx) --
        // confirmed by disassembly, not assumed identical to SE/VR.
        struct PatchApplyPassRax final : Xbyak::CodeGenerator
        {
            PatchApplyPassRax(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeAddr;

                xor_(ebx, ebx);

                cmp(rdx, kMinPlausiblePointer);
                jbe(skipLbl);

                and_(dword[rdx + 0x28], ebp);
                mov(qword[rdx + rax * 8], rbx);

                L(skipLbl);
                jmp(ptr[rip + resumeAddr]);

                L(resumeAddr);
                dq(a_resume);
            }
        };

        // Full 9-byte block: AND [RDX+0x28],EBP; XOR EBX,EBX; MOV
        // [RDX+RAX*8],RBX. AE-only encoding (see PatchApplyPassRax).
        inline bool SiteMatchesApplyPassRax(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x21, 0x6A, 0x28, 0x33, 0xDB, 0x48, 0x89, 0x1C, 0xC2 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        // GetRenderPassIndex's 22-byte site (5 qword MOVs + 1 dword MOV from
        // RDI), likewise too small for a call-out. Byte-identical on all 3
        // runtimes.
        struct PatchGetRenderPassIndex final : Xbyak::CodeGenerator
        {
            PatchGetRenderPassIndex(std::uintptr_t a_resume)
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
        // SiteMatchesApplyPassRcx -- the patch overwrites the whole block.
        // Matches SE, AE, and VR.
        inline bool SiteMatchesGetRenderPassIndex(std::uintptr_t a_addr)
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
                // Hash verified 2026-08-01 for whichever of the 2 sites overlaps (only one
                // does): 0 code xrefs, 0 stored/vtable pointers, identical on SE/AE and VR.
                patch.write_branch<5>(trampoline.allocate(p), false, 0xDC4AC3DA72A38D83ULL);
                ++installed;
            }
            return installed;
        }
    }

    inline void Install()
    {
        std::size_t installed = 0;
        if (REL::Module::IsVR()) {
            installed += detail::PatchSites<detail::PatchApplyPassRcx>(detail::SitesVRApplyPassAlphaCullState(), detail::SiteMatchesApplyPassRcx);
            installed += detail::PatchSites<detail::PatchGetRenderPassIndex>(detail::SitesVRGetRenderPassIndex(), detail::SiteMatchesGetRenderPassIndex);
        } else if (REL::Module::IsAE()) {
            installed += detail::PatchSites<detail::PatchApplyPassRax>(detail::SitesAEApplyPassAlphaCullState(), detail::SiteMatchesApplyPassRax);
            installed += detail::PatchSites<detail::PatchGetRenderPassIndex>(detail::SitesAEGetRenderPassIndex(), detail::SiteMatchesGetRenderPassIndex);
        } else {
            installed += detail::PatchSites<detail::PatchApplyPassRcx>(detail::SitesSEApplyPassAlphaCullState(), detail::SiteMatchesApplyPassRcx);
            installed += detail::PatchSites<detail::PatchGetRenderPassIndex>(detail::SitesSEGetRenderPassIndex(), detail::SiteMatchesGetRenderPassIndex);
        }

        if (installed > 0) {
            logger::info("installed batchrenderer renderpass array UAF fix ({} site(s))"sv, installed);
        } else {
            logger::warn("batchrenderer renderpass array UAF fix: no sites matched, not installed"sv);
        }
    }
}
