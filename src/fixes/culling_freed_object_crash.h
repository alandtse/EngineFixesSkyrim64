#pragma once

// Fix for use-after-free crashes in the cull traversal's per-object OnVisible
// dispatch, exposed by Community Shaders background shader compilation.
//
// Background-compile removes the blocking precompile screen, which also gated world
// rendering/culling during cell streaming. The cull traversal then runs while the
// cell loader frees scene objects and calls object->OnVisible (NiAVObject vfunc 0x34)
// on a freed-and-reused NiAVObject -> the vftable is heap garbage (a real vftable is
// in .rdata) and the slot is null/garbage -> CTD. Debugger-confirmed (dbgeng attach
// to live VR): object->vftable held a heap pointer ~0x1C0 bytes from the object.
//
// The dispatch is duplicated across the cull processes (BSCullingProcess,
// NiCullingProcess, BSParabolicCullingProcess). Every site matches CALL [RAX+slot]
// (RAX = object->vftable) immediately followed by CMP byte [reg+0x11D] (cull flag),
// then a conditional OR/AND [object+0x10C] write. Each is caved to a guard that
// validates the vftable lies inside the main module image: valid -> original call +
// resume; freed -> jump to the converge point (the post-CMP JZ target), skipping both
// the virtual call AND the [object+0x10C] write (a second UAF).
//
// Cross-runtime: OnVisible is NiAVObject vfunc 0x34. VR (1.4.15) inserts one extra
// vfunc before it (SKYRIM_REL_VR_VIRTUAL), so its byte offset is 0x1A8 on VR vs 0x1A0
// on SE/AE; the site addresses and converge targets were resolved per runtime. VR is
// where this actually bites (stereo doubles cull traversals -> wider race window); the
// SE/AE sites are covered for completeness (the renderpass-cache fix already let SE
// survive the same storm, so the cull crash is rarer there).
//
// The 5-byte branch's trailing byte is dead code (the cave only jumps to absolute
// targets), so no NOP fill is needed. R10 is volatile and not an argument register
// (RCX/RDX/R8/R9), so it is safe to clobber for the range check.

namespace Fixes::CullingFreedObjectCrash
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t callOffset;       // offset of CALL [RAX+slot]
            std::uintptr_t convergeOffset;   // resume offset when the object is freed
        };

        // (callOffset, convergeOffset) per runtime. See the investigation notes.
        inline constexpr std::array<Site, 6> kSitesVR{ {
            { 0xCBFD24, 0xCBFD52 }, { 0xD99D3D, 0xD99D6B }, { 0xD99E30, 0xD99E63 },
            { 0x136F23E, 0x136F26C }, { 0x136F2BD, 0x136F2EB }, { 0x136F5CA, 0x136F5E3 },
        } };
        inline constexpr std::array<Site, 7> kSitesAE{ {
            { 0xD3FFDD, 0xD4000B }, { 0xD40151, 0xD40193 }, { 0xE2853C, 0xE2856A },
            { 0xE2862B, 0xE2865E }, { 0x1519BBE, 0x1519BEC }, { 0x1519C38, 0x1519C66 },
            { 0x151A01A, 0x151A033 },
        } };
        inline constexpr std::array<Site, 6> kSitesSE{ {
            { 0xC794D4, 0xC79502 }, { 0xD50E37, 0xD50E65 }, { 0xD50F2A, 0xD50F5D },
            { 0x132C3DE, 0x132C40C }, { 0x132C45D, 0x132C48B }, { 0x132C78A, 0x132C7A3 },
        } };

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd, std::uint32_t a_slot,
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
                call(ptr[rax + a_slot]);
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
        inline bool SiteMatches(std::uintptr_t a_addr, std::uint32_t a_slot)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return p[0] == 0xFF && p[1] == 0x90 &&
                   p[2] == static_cast<std::uint8_t>(a_slot) &&
                   p[3] == static_cast<std::uint8_t>(a_slot >> 8) &&
                   p[4] == static_cast<std::uint8_t>(a_slot >> 16) &&
                   p[5] == static_cast<std::uint8_t>(a_slot >> 24);
        }

        inline void PatchSites(std::span<const Site> a_sites, std::uint32_t a_slot,
            std::uintptr_t a_base, std::uintptr_t a_end)
        {
            auto& trampoline = SKSE::GetTrampoline();
            for (const auto& site : a_sites) {
                REL::Relocation<std::uintptr_t> call{ REL::Offset{ site.callOffset } };
                if (!SiteMatches(call.address(), a_slot)) {
                    logger::warn("culling crash fix: unexpected bytes at {:X}, skipping site"sv, site.callOffset);
                    continue;
                }
                Patch p{ a_base, a_end, a_slot, call.address() + 0x6,
                    REL::Relocation<std::uintptr_t>{ REL::Offset{ site.convergeOffset } }.address() };
                p.ready();
                call.write_branch<5>(trampoline.allocate(p));
            }
        }
    }

    inline void Install()
    {
        const auto [moduleBase, moduleEnd] = util::GetModuleImageBounds();

        // OnVisible (NiAVObject vfunc 0x34) byte offset differs on VR (+1 vfunc).
        if (REL::Module::IsVR())
            detail::PatchSites(detail::kSitesVR, 0x1A8, moduleBase, moduleEnd);
        else if (REL::Module::IsAE())
            detail::PatchSites(detail::kSitesAE, 0x1A0, moduleBase, moduleEnd);
        else
            detail::PatchSites(detail::kSitesSE, 0x1A0, moduleBase, moduleEnd);

        logger::info("installed culling freed-object crash fix"sv);
    }
}
