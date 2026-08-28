#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

// Guards BSBatchRenderer's per-pass shader-technique dispatch (BSShader vfunc 0x10,
// SetupTechnique) against a freed/reused shader whose vtable pointer or SetupTechnique slot
// got reused by TBB's allocator (bOverrideMemoryManager routes through TBB, removing
// vanilla's incidental same-type-reuse protection; CommunityShaders' background shader
// compilation removes the blocking gate that used to serialize this race). Confirmed via a
// live 1.7.104 crash: wild jump through BSSkyShader's vtable during initial shadow-scene
// shader setup, matching this exact call site. AE inlines the dispatch directly into
// BSBatchRenderer::SetupAndDrawPass; SE/VR call it through a small BeginPass helper instead
// -- same bug, different host function, so each gets its own Site/Patch shape.

namespace Fixes::BatchRendererShaderTechniqueUAF
{
    namespace detail
    {
        // AE (all tiers): SetupAndDrawPass inlines "MOV RAX,[RDI]; MOV EDX,EBX" (5 bytes) at
        // entry+0x82, immediately followed by "MOV RCX,RDI; CALL [RAX+0x10]" at entry+0x87.
        // The function's own existing "technique setup failed" bail-out sits at entry+0x332
        // in every tier (resets the last-shader/last-technique cache, then returns) -- reused
        // here instead of inventing a new exit path.
        struct SiteAE
        {
            std::uintptr_t entry;
        };

        inline constexpr std::uintptr_t kAECallDelta = 0x82;
        inline constexpr std::uintptr_t kAEBailDelta = 0x332;
        inline constexpr std::uint8_t   kExpectedAE[] = { 0x48, 0x8B, 0x07, 0x8B, 0xD3 };
        static_assert(std::size(kExpectedAE) == 5, "AE displaced block must be exactly 5 bytes for the E9 patch");

        inline constexpr SiteAE kSetupAndDrawPassAE{ 0x14F3DC0 };      // 1.6.353 - 1.6.1170
        inline constexpr SiteAE kSetupAndDrawPassAE1799{ 0x15600E0 };  // 1.7.99+
        inline constexpr SiteAE kSetupAndDrawPassAE1104{ 0x1560340 };  // 1.7.104+

        // See kSitesAE_ByVersion in culling_freed_object_crash.h for why this is a table:
        // a future recompile is a new kSetupAndDrawPassAEx.y.z + one row here.
        inline constexpr std::array<util::VersionedValue<SiteAE>, 3> kSetupAndDrawPassAE_ByVersion{ {
            { REL::Version{ 1, 6, 353, 0 }, kSetupAndDrawPassAE },
            { REL::Version{ 1, 7, 99, 0 }, kSetupAndDrawPassAE1799 },
            { REL::Version{ 1, 7, 104, 0 }, kSetupAndDrawPassAE1104 },
        } };

        // SE/VR: BeginPass calls through the shader vtable directly -- "MOV RAX,[RBX]; CALL
        // [RAX+0x10]" (6 bytes) -- rather than inlining it into SetupAndDrawPass. Resume is
        // right after the call (TEST AL,AL); bail is the function's own null-technique reset.
        struct SiteSEVR
        {
            std::uintptr_t callOffset;
            std::uintptr_t bailOffset;
        };

        inline constexpr std::uint8_t kExpectedSEVR[] = { 0x48, 0x8B, 0x03, 0xFF, 0x50, 0x10 };

        inline constexpr SiteSEVR kBeginPassSE{ 0x1308707, 0x130872E };
        inline constexpr SiteSEVR kBeginPassVR{ 0x1349962, 0x134997B };

        // Validates the shader's vtable pointer lies inside the main module image, and that
        // the loaded SetupTechnique slot itself is a plausible code pointer, before calling
        // through it. AE re-executes the displaced MOV pair and resumes at the original call
        // site (which reloads RAX itself); SE/VR execute the call directly since the resume
        // point is the post-call TEST AL,AL.
        struct PatchAE final : Xbyak::CodeGenerator
        {
            PatchAE(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_bail)
            {
                Xbyak::Label skipLbl, resumeAddr, bailAddr;

                mov(rax, ptr[rdi]);
                mov(edx, ebx);

                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);
                util::EmitLoadedSlotGuard(*this, ptr[rax + 0x10], skipLbl);

                jmp(ptr[rip + resumeAddr]);

                L(skipLbl);
                jmp(ptr[rip + bailAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(bailAddr);
                dq(a_bail);
            }
        };

        struct PatchSEVR final : Xbyak::CodeGenerator
        {
            PatchSEVR(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_bail)
            {
                Xbyak::Label skipLbl, resumeAddr, bailAddr;

                mov(rax, ptr[rbx]);

                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);
                util::EmitLoadedSlotGuard(*this, ptr[rax + 0x10], skipLbl);

                call(ptr[rax + 0x10]);
                jmp(ptr[rip + resumeAddr]);

                L(skipLbl);
                jmp(ptr[rip + bailAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(bailAddr);
                dq(a_bail);
            }
        };

        inline void InstallAE(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd)
        {
            const auto&          site = util::SelectForVersion(kSetupAndDrawPassAE_ByVersion);
            const std::uintptr_t entry = REL::Relocation<std::uintptr_t>{ REL::Offset{ site.entry } }.address();
            const std::uintptr_t call = entry + kAECallDelta;

            if (!std::equal(std::begin(kExpectedAE), std::end(kExpectedAE), reinterpret_cast<const std::uint8_t*>(call))) {
                logger::warn("batchrenderer shader technique UAF fix: unexpected bytes at {:X}, skipping"sv,
                    site.entry + kAECallDelta);
                return;
            }

            PatchAE p{ a_moduleBase, a_moduleEnd, call + std::size(kExpectedAE), entry + kAEBailDelta };
            p.ready();
            REL::Relocation<std::uintptr_t>{ call }.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed batchrenderer shader technique UAF fix (ae)"sv);
        }

        inline void InstallSEVR(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd, const SiteSEVR& a_site)
        {
            const std::uintptr_t call = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_site.callOffset } }.address();
            const std::uintptr_t bail = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_site.bailOffset } }.address();

            if (!std::equal(std::begin(kExpectedSEVR), std::end(kExpectedSEVR), reinterpret_cast<const std::uint8_t*>(call))) {
                logger::warn("batchrenderer shader technique UAF fix: unexpected bytes at {:X}, skipping"sv, a_site.callOffset);
                return;
            }

            PatchSEVR p{ a_moduleBase, a_moduleEnd, call + std::size(kExpectedSEVR), bail };
            p.ready();
            REL::Relocation<std::uintptr_t>{ call }.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed batchrenderer shader technique UAF fix (se/vr)"sv);
        }
    }

    inline void Install()
    {
        const auto [moduleBase, moduleEnd] = util::GetModuleImageBounds();

        if (REL::Module::IsVR()) {
            detail::InstallSEVR(moduleBase, moduleEnd, detail::kBeginPassVR);
        } else if (REL::Module::IsAE()) {
            detail::InstallAE(moduleBase, moduleEnd);
        } else {
            detail::InstallSEVR(moduleBase, moduleEnd, detail::kBeginPassSE);
        }
    }
}
