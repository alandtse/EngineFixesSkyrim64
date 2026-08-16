#pragma once

#include <algorithm>

// Guards the cull traversal's OnVisible dispatch (NiAVObject vfunc 0x34, CALL [RAX+slot])
// against a freed/reused vftable: validates it lies inside the main module image before
// calling, else jumps past both the call and the following [object+0x10C] write. VR
// inserts one extra vfunc before OnVisible, so the site byte offset is 0x1A8 vs 0x1A0 on
// SE/AE. VR also has an ObjectLOD render/cull call (vfunc +0x80) with the same freed-object
// signature, installed by a separately validated site below.

namespace Fixes::CullingFreedObjectCrash
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t                callOffset;      // offset of CALL [RAX+slot]
            std::uintptr_t                convergeOffset;  // resume offset when the object is freed
            std::span<const std::uint8_t> postCall;        // expected bytes from callOffset+0x6 to convergeOffset
        };

        // Displaced post-call bytes per site, validated in full so a convergeOffset
        // pointing at the wrong basic block can't silently skip the wrong code.
        inline constexpr std::uint8_t kPostVR0[] = { 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8F, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA7, 0x0C, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostVR1[] = { 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8F, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA7, 0x0C, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostVR2[] = { 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x24, 0x81, 0x8F, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x18, 0x40, 0x84, 0xED, 0x75, 0x13, 0x40, 0x38, 0xA9, 0x1D, 0x01, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA7, 0x0C, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostVR3[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0x0C, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostVR4[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xB9, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0x0C, 0x01, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostVR5[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0x8B, 0x0C, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 };

        inline constexpr std::uint8_t kPostAE0[] = { 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8F, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA7, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostAE1[] = { 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x33, 0x81, 0x8F, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x89, 0xB3, 0x9C, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x74, 0x24, 0x30, 0x48, 0x8B, 0x5C, 0x24, 0x38, 0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3, 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA7, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostAE2[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostAE3[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x24, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x18, 0x40, 0x84, 0xED, 0x74, 0x13, 0x80, 0xB9, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostAE4[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostAE5[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xB9, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostAE6[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 };

        inline constexpr std::uint8_t kPostSE0[] = { 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8F, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBB, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA7, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostSE1[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostSE2[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x24, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x18, 0x40, 0x84, 0xED, 0x75, 0x13, 0x40, 0x38, 0xA9, 0x1D, 0x01, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostSE3[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostSE4[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x1F, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xEB, 0x13, 0x80, 0xB9, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0xA3, 0xF4, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFB };
        inline constexpr std::uint8_t kPostSE5[] = { 0x80, 0xBF, 0x1D, 0x01, 0x00, 0x00, 0x00, 0x74, 0x0A, 0x81, 0x8B, 0xF4, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 };

        inline constexpr std::array<Site, 6> kSitesVR{ {
            { 0xCBFD24, 0xCBFD52, kPostVR0 },
            { 0xD99D3D, 0xD99D6B, kPostVR1 },
            { 0xD99E30, 0xD99E63, kPostVR2 },
            { 0x136F23E, 0x136F26C, kPostVR3 },
            { 0x136F2BD, 0x136F2EB, kPostVR4 },
            { 0x136F5CA, 0x136F5E3, kPostVR5 },
        } };
        inline constexpr std::array<Site, 7> kSitesAE{ {
            { 0xD3FFDD, 0xD4000B, kPostAE0 },
            { 0xD40151, 0xD40193, kPostAE1 },
            { 0xE2853C, 0xE2856A, kPostAE2 },
            { 0xE2862B, 0xE2865E, kPostAE3 },
            { 0x1519BBE, 0x1519BEC, kPostAE4 },
            { 0x1519C38, 0x1519C66, kPostAE5 },
            { 0x151A01A, 0x151A033, kPostAE6 },
        } };
        inline constexpr std::array<Site, 6> kSitesSE{ {
            { 0xC794D4, 0xC79502, kPostSE0 },
            { 0xD50E37, 0xD50E65, kPostSE1 },
            { 0xD50F2A, 0xD50F5D, kPostSE2 },
            { 0x132C3DE, 0x132C40C, kPostSE3 },
            { 0x132C45D, 0x132C48B, kPostSE4 },
            { 0x132C78A, 0x132C7A3, kPostSE5 },
        } };

        template <std::size_t N>
        consteval bool SitesConsistent(const std::array<Site, N>& a_sites)
        {
            for (const auto& site : a_sites) {
                if (site.convergeOffset != site.callOffset + 0x6 + site.postCall.size())
                    return false;
            }
            return true;
        }

        static_assert(SitesConsistent(kSitesVR), "VR culling site converge offsets must match validated block lengths");
        static_assert(SitesConsistent(kSitesAE), "AE culling site converge offsets must match validated block lengths");
        static_assert(SitesConsistent(kSitesSE), "SE culling site converge offsets must match validated block lengths");

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd, std::uint32_t a_slot,
                std::uintptr_t a_postCall, std::uintptr_t a_converge)
            {
                Xbyak::Label skipLbl, postAddr, convAddr;

                // R10/R11 are volatile and not argument registers, so they are safe to clobber here.
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

                // An in-module vftable can still have had one slot clobbered with heap
                // garbage; validate the loaded slot value too, not just the vftable base.
                util::EmitLoadedSlotGuard(*this, ptr[rax + a_slot], skipLbl);

                // Valid vftable + valid slot: perform the original call, resume after it.
                call(r11);
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

        // Expected bytes at a site: CALL [RAX+disp32] == FF 90 <slot little-endian>.
        // Guards against offset drift corrupting an unrelated instruction stream.
        inline bool CallMatches(std::uintptr_t a_addr, std::uint32_t a_slot)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return p[0] == 0xFF && p[1] == 0x90 &&
                   p[2] == static_cast<std::uint8_t>(a_slot) &&
                   p[3] == static_cast<std::uint8_t>(a_slot >> 8) &&
                   p[4] == static_cast<std::uint8_t>(a_slot >> 16) &&
                   p[5] == static_cast<std::uint8_t>(a_slot >> 24);
        }

        // A stale convergeOffset could still pass CallMatches, so validate the displaced
        // block through convergeOffset too before patching.
        inline bool PostCallMatches(std::uintptr_t a_postCallAddr, std::span<const std::uint8_t> a_expected)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_postCallAddr);
            return std::equal(a_expected.begin(), a_expected.end(), p);
        }

        // A separate ObjectLOD render/cull path calls a property-like object at vfunc +0x80.
        // Unlike the OnVisible sites above, this function's clean freed-object path is an
        // earlier common-success exit, so validate the complete call and post-call decision
        // block explicitly before installing the cave.
        inline constexpr std::uint8_t kObjectLODRenderPostVR[] = {
            0x48, 0x85, 0xC0, 0x75, 0x09, 0x80, 0xBB, 0x90, 0x01, 0x00, 0x00,
            0x0B, 0x75, 0xBF
        };

        inline std::size_t PatchObjectLODRenderSiteVR(std::uintptr_t a_base, std::uintptr_t a_end)
        {
            constexpr std::uintptr_t kCallOffset = 0x13085EE;
            constexpr std::uintptr_t kConvergeOffset = 0x13085C1;

            REL::Relocation<std::uintptr_t> call{ REL::Offset{ kCallOffset } };
            if (!CallMatches(call.address(), 0x80) ||
                !PostCallMatches(call.address() + 0x6, kObjectLODRenderPostVR)) {
                logger::warn("ObjectLOD render crash fix: unexpected bytes at {:X}, skipping site"sv,
                    kCallOffset);
                return 0;
            }

            Patch p{ a_base, a_end, 0x80, call.address() + 0x6,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kConvergeOffset } }.address() };
            p.ready();
            call.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed ObjectLOD render freed-object crash fix"sv);
            return 1;
        }

        // The same ObjectLOD path first asks an auxiliary property at object+0x170 for its
        // runtime type through vfunc +0x10. Invalid properties follow the function's existing
        // null-property fallback; valid properties resume after the displaced call.
        struct ObjectLODPropertyPatch final : Xbyak::CodeGenerator
        {
            ObjectLODPropertyPatch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_fallback)
            {
                Xbyak::Label fallbackLbl, resumeAddr, fallbackAddr;

                mov(rax, ptr[rdi]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(fallbackLbl);
                // Reject vtables too close to the module end -- the eight-byte slot read at
                // [rax + 0x10] must itself land fully inside the module, not just rax.
                mov(r10, a_moduleEnd - 0x17);
                cmp(rax, r10);
                jae(fallbackLbl);

                util::EmitLoadedSlotGuard(*this, ptr[rax + 0x10], fallbackLbl);

                mov(rcx, rdi);
                call(r11);
                jmp(ptr[rip + resumeAddr]);

                L(fallbackLbl);
                jmp(ptr[rip + fallbackAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(fallbackAddr);
                dq(a_fallback);
            }
        };

        inline std::size_t PatchObjectLODPropertySiteVR(std::uintptr_t a_base, std::uintptr_t a_end)
        {
            constexpr std::uintptr_t      kPatchOffset = 0x13085A1;
            constexpr std::uintptr_t      kResumeOffset = 0x13085AA;
            constexpr std::uintptr_t      kFallbackOffset = 0x13085C8;
            static constexpr std::uint8_t kExpected[] = {
                0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x10,
                0x48, 0x8D, 0x0D, 0xAF, 0x38, 0xE6, 0x01,
                0x48, 0x3B, 0xC8, 0x0F, 0x94, 0xC0, 0x84, 0xC0, 0x74, 0x0D,
                0x80, 0x7F, 0x78, 0x00, 0x75, 0x07,
                0xB0, 0x01, 0xE9, 0x20, 0x01, 0x00, 0x00
            };
            static_assert(kPatchOffset + std::size(kExpected) == kFallbackOffset);

            REL::Relocation<std::uintptr_t> patch{ REL::Offset{ kPatchOffset } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(patch.address());
            if (!std::equal(std::begin(kExpected), std::end(kExpected), bytes)) {
                logger::warn("ObjectLOD property crash fix: unexpected bytes at {:X}, skipping site"sv,
                    kPatchOffset);
                return 0;
            }

            ObjectLODPropertyPatch p{ a_base, a_end,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kResumeOffset } }.address(),
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kFallbackOffset } }.address() };
            p.ready();
            patch.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed ObjectLOD property freed-object crash fix"sv);
            return 1;
        }

        inline std::size_t PatchSites(std::span<const Site> a_sites, std::uint32_t a_slot,
            std::uintptr_t a_base, std::uintptr_t a_end)
        {
            auto&       trampoline = SKSE::GetTrampoline();
            std::size_t installed = 0;
            for (const auto& site : a_sites) {
                REL::Relocation<std::uintptr_t> call{ REL::Offset{ site.callOffset } };
                if (!CallMatches(call.address(), a_slot)) {
                    logger::warn("culling crash fix: unexpected bytes at {:X}, skipping site"sv, site.callOffset);
                    continue;
                }
                if (!PostCallMatches(call.address() + 0x6, site.postCall)) {
                    logger::warn("culling crash fix: unexpected bytes after call at {:X}, skipping site"sv, site.callOffset);
                    continue;
                }
                Patch p{ a_base, a_end, a_slot, call.address() + 0x6,
                    REL::Relocation<std::uintptr_t>{ REL::Offset{ site.convergeOffset } }.address() };
                p.ready();
                call.write_branch<5>(trampoline.allocate(p));
                ++installed;
            }
            return installed;
        }
    }

    inline void Install()
    {
        const auto [moduleBase, moduleEnd] = util::GetModuleImageBounds();

        std::size_t installed = 0;

        // OnVisible (NiAVObject vfunc 0x34) byte offset differs on VR (+1 vfunc).
        if (REL::Module::IsVR()) {
            installed += detail::PatchSites(detail::kSitesVR, 0x1A8, moduleBase, moduleEnd);
            installed += detail::PatchObjectLODPropertySiteVR(moduleBase, moduleEnd);
            installed += detail::PatchObjectLODRenderSiteVR(moduleBase, moduleEnd);
        } else if (REL::Module::IsAE())
            installed += detail::PatchSites(detail::kSitesAE, 0x1A0, moduleBase, moduleEnd);
        else
            installed += detail::PatchSites(detail::kSitesSE, 0x1A0, moduleBase, moduleEnd);

        if (installed > 0) {
            logger::info("installed culling freed-object crash fix ({} site(s))"sv, installed);
        } else {
            logger::warn("culling freed-object crash fix: no sites matched, not installed"sv);
        }
    }
}
