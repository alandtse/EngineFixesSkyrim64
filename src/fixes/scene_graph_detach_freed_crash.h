#pragma once

// Fix for a use-after-free crash in the scene-graph downward-visit traversal,
// exposed by Community Shaders background shader compilation.
//
// Background-compile removes the blocking precompile screen, which also gated world
// rendering/streaming. With it gone, cell teardown (coc/cow -> GridArray::DetachAll ->
// TESObjectREFR::DetachHavok) walks the scene graph via the recursive NiAVObject visitor
// while the cell loader frees nodes. The visitor does `MOV RAX,[RDI]; CALL [RAX+0x18]`
// (RDI->vfunc[3], the get-children dispatch) on a freed-and-zeroed node -> RAX (vftable)
// is null -> AV read @0x18 -> CTD. Debugger/crashlog-confirmed on VR (TreePineForest02
// node hierarchy during a coc storm). Same UAF class as the BSCullingProcess OnVisible
// crashes (culling_freed_object_crash.h) and the renderpass-cache UAF, different subsystem.
//
// The visitor is a single recursive function (every node, at every depth, re-enters it),
// so ONE guard at its entry covers the whole subtree walk and both internal virtual
// dispatches (the get-children CALL [RAX+0x18] and the earlier CALL [RAX+0x90]). The entry
// already begins `TEST RCX,RCX; JZ <exit>` BEFORE any stack setup, so <exit> is a
// proven-safe return from the pre-prologue state. The guard reuses that exact exit: it
// replicates the null check, then validates the node's vftable lies inside the main module
// image (a live vftable is in .rdata; a freed node's is null or heap garbage). Valid ->
// resume the original prologue; null/freed -> jump to the same clean <exit>, skipping the
// entire (recursive) walk.
//
// RAX and R10 are volatile and not argument registers (RCX/RDX/R8/R9), so they are safe to
// clobber at entry; RCX (the node) is preserved. Cross-runtime: the function and its 9-byte
// `TEST RCX,RCX (3) + JZ rel32 (6)` prologue are identical on SE/AE/VR; only the addresses
// differ (resolved per runtime). VR is where this bites (stereo widens the streaming race),
// but the traversal is shared, so SE/AE are covered for completeness.

namespace Fixes::SceneGraphDetachFreedCrash
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t entryOffset;  // function entry: TEST RCX,RCX; JZ exit
            std::uintptr_t exitOffset;   // the JZ target (clean pre-prologue return)
        };

        // sizeof(TEST RCX,RCX) + sizeof(JZ rel32) = 3 + 6; resume = entry + 9.
        inline constexpr std::uintptr_t kPrologueLen = 9;

        inline constexpr Site kSiteVR{ 0xDFDCF0, 0xDFDDBA };
        inline constexpr Site kSiteAE{ 0xE87DF0, 0xE87EBA };
        inline constexpr Site kSiteSE{ 0xDA8D70, 0xDA8E3A };

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_exit)
            {
                Xbyak::Label exitLbl, resumeAddr, exitAddr;

                // Replicate the displaced null check.
                test(rcx, rcx);
                jz(exitLbl);

                // Validate the node's vftable lies inside the main module image.
                mov(rax, ptr[rcx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(exitLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(exitLbl);

                // Live node: resume the original prologue after the displaced bytes.
                jmp(ptr[rip + resumeAddr]);

                // Null or freed node: take the original clean exit (nothing pushed yet).
                L(exitLbl);
                jmp(ptr[rip + exitAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(exitAddr);
                dq(a_exit);
            }
        };
    }

    inline void Install()
    {
        const auto [moduleBase, moduleEnd] = util::GetModuleImageBounds();

        const auto& site = REL::Module::IsVR() ? detail::kSiteVR :
                           REL::Module::IsAE() ? detail::kSiteAE :
                                                 detail::kSiteSE;

        REL::Relocation<std::uintptr_t> entry{ REL::Offset{ site.entryOffset } };

        // Verify the displaced prologue is TEST RCX,RCX; JZ rel32 (48 85 C9 0F 84 ..)
        // before caving it; guards against offset drift corrupting the function entry.
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(entry.address());
        if (!(bytes[0] == 0x48 && bytes[1] == 0x85 && bytes[2] == 0xC9 &&
                bytes[3] == 0x0F && bytes[4] == 0x84)) {
            logger::warn("scene-graph detach crash fix: unexpected prologue at {:X}, skipping"sv,
                site.entryOffset);
            return;
        }

        detail::Patch p{ moduleBase, moduleEnd,
            entry.address() + detail::kPrologueLen,
            REL::Relocation<std::uintptr_t>{ REL::Offset{ site.exitOffset } }.address() };
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        entry.write_branch<5>(trampoline.allocate(p));

        logger::info("installed scene-graph detach freed-object crash fix"sv);
    }
}
