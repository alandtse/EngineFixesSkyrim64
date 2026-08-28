#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

// Guards BSBatchRenderer::SetupAndDrawPass's several independent shader-pointer dispatches against a freed/reused vtable.
namespace Fixes::BatchRendererShaderTechniqueUAF
{
    namespace detail
    {
        // AE: mov rdi,[rcx]; mov r14,[rcx+0x10] at entry+0x34 loads the shader pointer RDI feeds downstream.
        struct SiteAE
        {
            std::uintptr_t entry;
        };

        inline constexpr std::uintptr_t kAECallDelta = 0x34;
        inline constexpr std::uintptr_t kAEBailDelta = 0x332;
        inline constexpr std::uint8_t   kExpectedAE[] = { 0x48, 0x8B, 0x39, 0x4C, 0x8B, 0x71, 0x10 };
        static_assert(std::size(kExpectedAE) == 7, "AE displaced block must land on a real instruction boundary");

        inline constexpr SiteAE kSetupAndDrawPassAE{ 0x14F3DC0 };      // 1.6.353 - 1.6.1170
        inline constexpr SiteAE kSetupAndDrawPassAE1799{ 0x15600E0 };  // 1.7.99+
        inline constexpr SiteAE kSetupAndDrawPassAE1104{ 0x1560340 };  // 1.7.104+

        // See kSitesAE_ByVersion in culling_freed_object_crash.h for why this is a table.
        inline constexpr std::array<util::VersionedValue<SiteAE>, 3> kSetupAndDrawPassAE_ByVersion{ {
            { REL::Version{ 1, 6, 353, 0 }, kSetupAndDrawPassAE },
            { REL::Version{ 1, 7, 99, 0 }, kSetupAndDrawPassAE1799 },
            { REL::Version{ 1, 7, 104, 0 }, kSetupAndDrawPassAE1104 },
        } };

        // SE/VR: mov r14,[rcx]; movzx r15d,r8b loads the shader pointer near entry; bail is the function's epilogue.
        struct SiteSEVR
        {
            std::uintptr_t callOffset;
            std::uintptr_t bailOffset;
        };

        inline constexpr std::uint8_t kExpectedSEVR[] = { 0x4C, 0x8B, 0x31, 0x45, 0x0F, 0xB6, 0xF8 };
        static_assert(std::size(kExpectedSEVR) == 7, "SE/VR displaced block must land on a real instruction boundary");

        inline constexpr SiteSEVR kSetupAndDrawPassSE{ 0x130845C, 0x1308504 };
        inline constexpr SiteSEVR kSetupAndDrawPassVR{ 0x134969C, 0x1349747 };

        // Defense in depth: a concurrent free can land between the entry guard and this later SetupMaterial dispatch.
        inline constexpr std::uintptr_t kAEMaterialCallDelta = 0xC2;
        inline constexpr std::uint8_t   kExpectedAEMaterial[] = {
            0x48, 0x8B, 0x07, 0x48, 0x8B, 0xD3, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x20
        };
        static_assert(std::size(kExpectedAEMaterial) == 12, "AE material displaced block must land on a real instruction boundary");

        // SE/VR: mov rax,[r14]; mov rdx,rdi/rcx; mov rcx,r14; call [rax+0x20].
        inline constexpr std::uint8_t kExpectedSEVRMaterial[] = {
            0x49, 0x8B, 0x06, 0x48, 0x8B, 0xD7, 0x49, 0x8B, 0xCE, 0xFF, 0x50, 0x20
        };
        static_assert(std::size(kExpectedSEVRMaterial) == 12, "SE/VR material displaced block must land on a real instruction boundary");

        inline constexpr std::uintptr_t kSetupMaterialCallSE = 0x13084B2;
        inline constexpr std::uintptr_t kSetupMaterialCallVR = 0x13496F5;

        // Likely culprit: restores technique on the PREVIOUS frame's cached shader, only null-checked.
        inline constexpr std::uintptr_t kAERestoreCallDelta = 0x62;
        inline constexpr std::uintptr_t kAERestoreResumeDelta = 0x6A;
        inline constexpr std::uintptr_t kAERestoreBailDelta = 0x6D;
        inline constexpr std::uint8_t   kExpectedAERestore[] = { 0x48, 0x85, 0xC9, 0x74, 0x06, 0x48, 0x8B, 0x01 };
        static_assert(std::size(kExpectedAERestore) == 8, "AE restore displaced block must land on a real instruction boundary");

        // SE/VR: same shape, same site inside BeginPass (not SetupAndDrawPass).
        struct SiteRestoreSEVR
        {
            std::uintptr_t callOffset;
            std::uintptr_t resumeOffset;
            std::uintptr_t bailOffset;
        };
        inline constexpr std::uint8_t kExpectedSEVRRestore[] = { 0x48, 0x85, 0xC9, 0x74, 0x0C, 0x48, 0x8B, 0x01 };
        static_assert(std::size(kExpectedSEVRRestore) == 8, "SE/VR restore displaced block must land on a real instruction boundary");

        inline constexpr SiteRestoreSEVR kRestoreCallSE{ 0x13086DB, 0x13086E3, 0x13086EC };
        inline constexpr SiteRestoreSEVR kRestoreCallVR{ 0x1349933, 0x134993B, 0x1349944 };

        // Validates the shader's vtable pointer is in-module before any downstream dispatch through it runs.
        struct PatchAE final : Xbyak::CodeGenerator
        {
            PatchAE(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_bail)
            {
                Xbyak::Label skipLbl, resumeAddr, bailAddr;

                mov(rdi, ptr[rcx]);
                mov(r14, ptr[rcx + 0x10]);

                mov(rax, ptr[rdi]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

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

                mov(r14, ptr[rcx]);
                movzx(r15d, r8b);

                mov(rax, ptr[r14]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

                jmp(ptr[rip + resumeAddr]);

                L(skipLbl);
                jmp(ptr[rip + bailAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(bailAddr);
                dq(a_bail);
            }
        };

        // Both branches converge on the same address; the call is simply skipped when invalid.
        struct PatchMaterialAE final : Xbyak::CodeGenerator
        {
            PatchMaterialAE(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd, std::uintptr_t a_converge)
            {
                Xbyak::Label skipLbl, convergeAddr;

                mov(rax, ptr[rdi]);
                mov(rdx, rbx);
                mov(rcx, rdi);

                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);
                call(ptr[rax + 0x20]);

                L(skipLbl);
                jmp(ptr[rip + convergeAddr]);

                L(convergeAddr);
                dq(a_converge);
            }
        };

        struct PatchMaterialSEVR final : Xbyak::CodeGenerator
        {
            PatchMaterialSEVR(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd, std::uintptr_t a_converge)
            {
                Xbyak::Label skipLbl, convergeAddr;

                mov(rax, ptr[r14]);
                mov(rdx, rdi);
                mov(rcx, r14);

                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);
                call(ptr[rax + 0x20]);

                L(skipLbl);
                jmp(ptr[rip + convergeAddr]);

                L(convergeAddr);
                dq(a_converge);
            }
        };

        // RCX holds the cached previous shader here, not the current pass's shader.
        struct PatchRestoreAE final : Xbyak::CodeGenerator
        {
            PatchRestoreAE(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_bail)
            {
                Xbyak::Label skipLbl, resumeAddr, bailAddr;

                test(rcx, rcx);
                jz(skipLbl);
                mov(rax, ptr[rcx]);

                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

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

            const std::uintptr_t materialCall = entry + kAEMaterialCallDelta;
            if (!std::equal(std::begin(kExpectedAEMaterial), std::end(kExpectedAEMaterial), reinterpret_cast<const std::uint8_t*>(materialCall))) {
                logger::warn("batchrenderer shader technique UAF fix: unexpected bytes at {:X}, skipping material guard"sv,
                    site.entry + kAEMaterialCallDelta);
                return;
            }
            PatchMaterialAE mp{ a_moduleBase, a_moduleEnd, materialCall + std::size(kExpectedAEMaterial) };
            mp.ready();
            REL::Relocation<std::uintptr_t>{ materialCall }.write_branch<5>(SKSE::GetTrampoline().allocate(mp));
            logger::info("installed batchrenderer shader technique UAF fix (ae, material)"sv);

            const std::uintptr_t restoreCall = entry + kAERestoreCallDelta;
            if (!std::equal(std::begin(kExpectedAERestore), std::end(kExpectedAERestore), reinterpret_cast<const std::uint8_t*>(restoreCall))) {
                logger::warn("batchrenderer shader technique UAF fix: unexpected bytes at {:X}, skipping restore guard"sv,
                    site.entry + kAERestoreCallDelta);
                return;
            }
            PatchRestoreAE rp{ a_moduleBase, a_moduleEnd, entry + kAERestoreResumeDelta, entry + kAERestoreBailDelta };
            rp.ready();
            REL::Relocation<std::uintptr_t>{ restoreCall }.write_branch<5>(SKSE::GetTrampoline().allocate(rp));
            logger::info("installed batchrenderer shader technique UAF fix (ae, restore)"sv);
        }

        inline void InstallSEVR(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
            const SiteSEVR& a_site, std::uintptr_t a_materialCallOffset, const SiteRestoreSEVR& a_restoreSite)
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

            const std::uintptr_t materialCall = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_materialCallOffset } }.address();
            if (!std::equal(std::begin(kExpectedSEVRMaterial), std::end(kExpectedSEVRMaterial), reinterpret_cast<const std::uint8_t*>(materialCall))) {
                logger::warn("batchrenderer shader technique UAF fix: unexpected bytes at {:X}, skipping material guard"sv,
                    a_materialCallOffset);
                return;
            }
            PatchMaterialSEVR mp{ a_moduleBase, a_moduleEnd, materialCall + std::size(kExpectedSEVRMaterial) };
            mp.ready();
            REL::Relocation<std::uintptr_t>{ materialCall }.write_branch<5>(SKSE::GetTrampoline().allocate(mp));
            logger::info("installed batchrenderer shader technique UAF fix (se/vr, material)"sv);

            const std::uintptr_t restoreCall = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_restoreSite.callOffset } }.address();
            if (!std::equal(std::begin(kExpectedSEVRRestore), std::end(kExpectedSEVRRestore), reinterpret_cast<const std::uint8_t*>(restoreCall))) {
                logger::warn("batchrenderer shader technique UAF fix: unexpected bytes at {:X}, skipping restore guard"sv,
                    a_restoreSite.callOffset);
                return;
            }
            PatchRestoreAE rp{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_restoreSite.resumeOffset } }.address(),
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_restoreSite.bailOffset } }.address() };
            rp.ready();
            REL::Relocation<std::uintptr_t>{ restoreCall }.write_branch<5>(SKSE::GetTrampoline().allocate(rp));
            logger::info("installed batchrenderer shader technique UAF fix (se/vr, restore)"sv);
        }
    }

    inline void Install()
    {
        const auto [moduleBase, moduleEnd] = util::GetModuleImageBounds();

        if (REL::Module::IsVR()) {
            detail::InstallSEVR(moduleBase, moduleEnd, detail::kSetupAndDrawPassVR, detail::kSetupMaterialCallVR, detail::kRestoreCallVR);
        } else if (REL::Module::IsAE()) {
            detail::InstallAE(moduleBase, moduleEnd);
        } else {
            detail::InstallSEVR(moduleBase, moduleEnd, detail::kSetupAndDrawPassSE, detail::kSetupMaterialCallSE, detail::kRestoreCallSE);
        }
    }
}
