#pragma once

#include <algorithm>

// Guards BSBatchRenderer against accessing a cleared/null-derived renderPass array. A heap
// pointer can't be range-checked against the module image like a vftable can, so the guard
// instead skips accesses below kMinPlausiblePointer. AE's compiled output differs enough at
// every site (register allocation, sometimes instruction order) to need its own Patch type
// per site; SE shares VR's byte-identical code and reuses its Patch types.

namespace Fixes::BatchRendererRenderPassArrayUAF
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t patchAddress;   // start of the patched block
            std::uintptr_t resumeAddress;  // where both branches converge, right after the block
        };

        struct ReadSite
        {
            std::uintptr_t patchAddress;
            std::uintptr_t resumeAddress;
            std::uintptr_t emptyAddress;
        };

        // VR reuses SE's ID(100852); verified by disassembly to resolve to the same function.
        inline std::array<Site, 1> SitesVRApplyPassAlphaCullState()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2D3 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2DC }.address() },
            } };
        }

        // VR's duplicate-id bug (100853 pointed at 100852's address) was fixed upstream
        // in skyrim_vr_address_library's database.csv on 2026-09-01; safe to use the id directly now.
        inline std::array<Site, 1> SitesVRGetRenderPassIndex()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x57 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100853), 0x6D }.address() },
            } };
        }

        // Inside BSBatchRenderer::GetNextPassSlotInGroup: selects the next occupied
        // pass after rendering. Requires address-library id 100851's VR mapping
        // (alandtse/skyrim_vr_address_library#203).
        inline std::array<ReadSite, 1> SitesVRFindNextPass()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100851), 0x30 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100851), 0x38 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100851), 0x5C }.address() },
            } };
        }

        // Inside BSBatchRenderer::ApplyPassAlphaCullState (same function as
        // SitesVRApplyPassAlphaCullState above): loads the selected pass.
        inline std::array<ReadSite, 1> SitesVRLoadPass()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x27B }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x283 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2BA }.address() },
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

        // SE shares VR's byte-identical GetNextPassSlotInGroup and ApplyPassAlphaCullState
        // (verified by disassembly), so these reuse the VR guards' Patch types below.
        inline std::array<ReadSite, 1> SitesSEFindNextPass()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100851), 0x30 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100851), 0x38 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100851), 0x5C }.address() },
            } };
        }

        inline std::array<ReadSite, 1> SitesSELoadPass()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x27B }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x283 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(100852), 0x2BA }.address() },
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

        // AE's GetNextPassSlotInGroup uses different register allocation than SE/VR
        // (R10/R11 instead of R11/RBX, an immediate 0 compare instead of a zeroed
        // register), so it needs its own Patch type below.
        inline std::array<ReadSite, 1> SitesAEFindNextPass()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(107641), 0x30 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(107641), 0x38 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(107641), 0x5D }.address() },
            } };
        }

        // AE's ApplyPassAlphaCullState computes the pass index between the pointer
        // load and the dereference (RDI/R10/RCX instead of RSI/RDX), so the guarded
        // block is 3 instructions here instead of 2.
        inline std::array<ReadSite, 1> SitesAELoadPass()
        {
            return { {
                { REL::Relocation<std::uintptr_t>{ REL::ID(107642), 0x270 }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(107642), 0x27C }.address(),
                    REL::Relocation<std::uintptr_t>{ REL::ID(107642), 0x2AE }.address() },
            } };
        }

        // No pointer returned by any real allocator will ever be this low; a
        // computed PassGroup pointer at or below this floor can only be
        // renderPass._data == nullptr plus a small index*sizeof(PassGroup) offset.
        inline constexpr std::uintptr_t kMinPlausiblePointer = 0x10000;

        // Too small for a call-out, so this reproduces the block inline.
        struct PatchApplyPassRcx final : Xbyak::CodeGenerator
        {
            PatchApplyPassRcx(std::uintptr_t a_resume)
            {
                Xbyak::Label skipLbl, resumeLbl, resumeAddr;

                cmp(rdx, kMinPlausiblePointer);
                jbe(skipLbl);

                // Preserve the displaced block's instruction order and final
                // EFLAGS state (from XOR), as well as its EBX side effect.
                and_(dword[rdx + 0x28], ebp);
                xor_(ebx, ebx);
                mov(qword[rdx + rcx * 8], rbx);
                jmp(resumeLbl);

                L(skipLbl);
                // The exceptional path cannot execute the memory AND, but it
                // must retain the original block's zeroed EBX and XOR flags.
                xor_(ebx, ebx);

                L(resumeLbl);
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
                Xbyak::Label skipLbl, resumeLbl, resumeAddr;

                cmp(rdx, kMinPlausiblePointer);
                jbe(skipLbl);

                and_(dword[rdx + 0x28], ebp);
                xor_(ebx, ebx);
                mov(qword[rdx + rax * 8], rbx);
                jmp(resumeLbl);

                L(skipLbl);
                xor_(ebx, ebx);

                L(resumeLbl);
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

                // The displaced block contains only MOVs and therefore
                // preserves incoming EFLAGS. Save them around the guard.
                pushfq();
                cmp(rcx, kMinPlausiblePointer);
                jbe(skipLbl);

                // RDI is zeroed at entry and untouched until here, so it's safe to reuse.
                mov(qword[rcx], rdi);
                mov(qword[rcx + 0x8], rdi);
                mov(qword[rcx + 0x10], rdi);
                mov(qword[rcx + 0x18], rdi);
                mov(qword[rcx + 0x20], rdi);
                mov(dword[rcx + 0x28], edi);

                // Null-derived storage: skip straight here either way.
                L(skipLbl);
                popfq();
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

        // Reload renderPass._data for every probe because rendering a pass can clear
        // the array before the iterator asks for the next occupied slot.
        struct PatchFindNextPass final : Xbyak::CodeGenerator
        {
            PatchFindNextPass(std::uintptr_t a_resume, std::uintptr_t a_empty)
            {
                Xbyak::Label emptyLbl, resumeAddr, emptyAddr;

                mov(rcx, qword[r11 + 0x8]);
                cmp(rcx, kMinPlausiblePointer);
                jbe(emptyLbl);

                lea(rdx, qword[rbx + r8]);
                jmp(ptr[rip + resumeAddr]);

                L(emptyLbl);
                mov(dword[r9], r10d);
                mov(rbx, qword[rsp]);
                mov(eax, 0x5);
                jmp(ptr[rip + emptyAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(emptyAddr);
                dq(a_empty);
            }
        };

        inline bool SiteMatchesFindNextPass(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x49, 0x8B, 0x4B, 0x08, 0x4A, 0x8D, 0x14, 0x03 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        struct PatchLoadPass final : Xbyak::CodeGenerator
        {
            PatchLoadPass(std::uintptr_t a_resume, std::uintptr_t a_empty)
            {
                Xbyak::Label emptyLbl, resumeAddr, emptyAddr;

                mov(rax, qword[rsi + 0x8]);
                cmp(rax, kMinPlausiblePointer);
                jbe(emptyLbl);

                mov(rbx, qword[rax + rdx * 8]);
                jmp(ptr[rip + resumeAddr]);

                L(emptyLbl);
                xor_(ebx, ebx);
                jmp(ptr[rip + emptyAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(emptyAddr);
                dq(a_empty);
            }
        };

        inline bool SiteMatchesLoadPass(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x48, 0x8B, 0x46, 0x08, 0x48, 0x8B, 0x1C, 0xD0 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        // AE-only encoding of PatchFindNextPass: the pointer is in R10 (not R11), the
        // index in R11+R8 (not RBX+R8), the "not found" sentinel is an immediate 0
        // (not a zeroed register), and there's no RBX to save/restore -- AE's compiler
        // used the volatile R11 here instead of the callee-saved RBX.
        struct PatchFindNextPassAE final : Xbyak::CodeGenerator
        {
            PatchFindNextPassAE(std::uintptr_t a_resume, std::uintptr_t a_empty)
            {
                Xbyak::Label emptyLbl, resumeAddr, emptyAddr;

                mov(rcx, qword[r10 + 0x8]);
                cmp(rcx, kMinPlausiblePointer);
                jbe(emptyLbl);

                lea(rdx, qword[r11 + r8]);
                jmp(ptr[rip + resumeAddr]);

                L(emptyLbl);
                mov(dword[r9], 0);
                mov(eax, 0x5);
                jmp(ptr[rip + emptyAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(emptyAddr);
                dq(a_empty);
            }
        };

        inline bool SiteMatchesFindNextPassAE(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x49, 0x8B, 0x4A, 0x08, 0x4B, 0x8D, 0x14, 0x03 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        // AE-only encoding of PatchLoadPass: the pointer is in RDI (not RSI), and the
        // index (R10+RCX) is computed between the pointer load and the dereference
        // rather than beforehand, so the guarded block spans 3 instructions, not 2.
        struct PatchLoadPassAE final : Xbyak::CodeGenerator
        {
            PatchLoadPassAE(std::uintptr_t a_resume, std::uintptr_t a_empty)
            {
                Xbyak::Label emptyLbl, resumeAddr, emptyAddr;

                mov(rax, qword[rdi + 0x8]);
                cmp(rax, kMinPlausiblePointer);
                jbe(emptyLbl);

                lea(rdx, qword[r10 + rcx * 2]);
                mov(rbx, qword[rax + rdx * 8]);
                jmp(ptr[rip + resumeAddr]);

                L(emptyLbl);
                xor_(ebx, ebx);
                jmp(ptr[rip + emptyAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(emptyAddr);
                dq(a_empty);
            }
        };

        inline bool SiteMatchesLoadPassAE(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x48, 0x8B, 0x47, 0x08, 0x49, 0x8D, 0x14, 0x4A, 0x48, 0x8B, 0x1C, 0xD0 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
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

        template <class PatchT>
        inline std::size_t PatchReadSites(std::span<const ReadSite> a_sites, bool (*a_matches)(std::uintptr_t))
        {
            auto&       trampoline = SKSE::GetTrampoline();
            std::size_t installed = 0;
            for (const auto& site : a_sites) {
                REL::Relocation<std::uintptr_t> patch{ site.patchAddress };
                if (!a_matches(patch.address())) {
                    logger::warn("batchrenderer renderpass array UAF fix: unexpected bytes at {:X}, skipping site"sv, site.patchAddress);
                    continue;
                }
                PatchT p{ site.resumeAddress, site.emptyAddress };
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
            installed += detail::PatchReadSites<detail::PatchFindNextPass>(detail::SitesVRFindNextPass(), detail::SiteMatchesFindNextPass);
            installed += detail::PatchReadSites<detail::PatchLoadPass>(detail::SitesVRLoadPass(), detail::SiteMatchesLoadPass);
            installed += detail::PatchSites<detail::PatchApplyPassRcx>(detail::SitesVRApplyPassAlphaCullState(), detail::SiteMatchesApplyPassRcx);
            installed += detail::PatchSites<detail::PatchGetRenderPassIndex>(detail::SitesVRGetRenderPassIndex(), detail::SiteMatchesGetRenderPassIndex);
        } else if (REL::Module::IsAE()) {
            installed += detail::PatchReadSites<detail::PatchFindNextPassAE>(detail::SitesAEFindNextPass(), detail::SiteMatchesFindNextPassAE);
            installed += detail::PatchReadSites<detail::PatchLoadPassAE>(detail::SitesAELoadPass(), detail::SiteMatchesLoadPassAE);
            installed += detail::PatchSites<detail::PatchApplyPassRax>(detail::SitesAEApplyPassAlphaCullState(), detail::SiteMatchesApplyPassRax);
            installed += detail::PatchSites<detail::PatchGetRenderPassIndex>(detail::SitesAEGetRenderPassIndex(), detail::SiteMatchesGetRenderPassIndex);
        } else {
            installed += detail::PatchReadSites<detail::PatchFindNextPass>(detail::SitesSEFindNextPass(), detail::SiteMatchesFindNextPass);
            installed += detail::PatchReadSites<detail::PatchLoadPass>(detail::SitesSELoadPass(), detail::SiteMatchesLoadPass);
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
