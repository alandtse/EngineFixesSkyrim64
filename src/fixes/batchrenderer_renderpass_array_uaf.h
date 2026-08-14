#pragma once

#include <algorithm>

// Guards BSBatchRenderer::ApplyPassAlphaCullState and GetRenderPassIndex against writing
// through a freed/reallocated PassGroup pointer left in renderPassMap. A heap pointer can't
// be range-checked against the module image like a vftable can, so the guard instead skips
// the write below kMinPlausiblePointer. AE encodes ApplyPassAlphaCullState's index register
// as RAX instead of RCX, hence the separate Patch/matcher pair.

namespace Fixes::BatchRendererRenderPassArrayUAF
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t patchAddress;   // start of the patched block
            std::uintptr_t resumeAddress;  // where both branches converge, right after the block
        };

        // VR reuses SE's ID(100852); verified by disassembly to resolve to the same function.
        inline std::array<Site, 1> SitesVRApplyPassAlphaCullState()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2D3 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2DC }.address() },
            } };
        }

        // Anchored by a raw offset, not REL::ID(100853): the address-library's VR column for
        // 100853 currently duplicates 100852 instead of pointing at GetRenderPassIndex.
        inline std::array<Site, 1> SitesVRGetRenderPassIndex()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x1349647 } }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x134965D } }.address() },
            } };
        }

        inline std::array<Site, 1> SitesSEApplyPassAlphaCullState()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2D3 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2DC }.address() },
            } };
        }

        inline std::array<Site, 1> SitesSEGetRenderPassIndex()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x57 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x6D }.address() },
            } };
        }

        // AE uses a separate numeric ID space from SE (see se_ae.csv).
        inline std::array<Site, 1> SitesAEApplyPassAlphaCullState()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(107642), 0x2CA }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(107642), 0x2D3 }.address() },
            } };
        }

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

        // Validated in full, not just the leading opcode, since the patch overwrites all 9 bytes.
        inline bool SiteMatchesApplyPassRcx(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x21, 0x6A, 0x28, 0x33, 0xDB, 0x48, 0x89, 0x1C, 0xCA };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        // Same shape as PatchApplyPassRcx, but AE encodes the MOV's index register as RAX.
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

        // AE-only encoding (see PatchApplyPassRax).
        inline bool SiteMatchesApplyPassRax(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x21, 0x6A, 0x28, 0x33, 0xDB, 0x48, 0x89, 0x1C, 0xC2 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        struct PatchGetRenderPassIndex final : Xbyak::CodeGenerator
        {
            PatchGetRenderPassIndex(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeAddr;

                cmp(rcx, kMinPlausiblePointer);
                jbe(skipLbl);

                // RDI is zeroed at entry and untouched until here, so it's safe to reuse.
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

        // Validated in full, not just the leading opcode, since the patch overwrites the block.
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
