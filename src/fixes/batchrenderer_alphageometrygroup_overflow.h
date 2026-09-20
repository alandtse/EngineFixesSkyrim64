#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "installed_fixes.h"

// StartGroupingAlphas claims alpha-group slots with an unchecked lock xadd; past the array's
// capacity the returned slot is garbage.
namespace Fixes::BatchRendererAlphaGeometryGroupOverflow
{
    namespace detail
    {
        inline constexpr std::uint32_t kCapacityFlat = 512;
        inline constexpr std::uint32_t kCapacityVR = 1024;
        inline constexpr std::uint32_t kCapacityMargin = 8;

        inline constexpr std::int32_t kSavedEaxStackOffset = 0x70;

        struct Site
        {
            std::uintptr_t patchOffset;
            std::uintptr_t resumeOffset;
            std::uintptr_t noGroupExitOffset;
        };

        inline constexpr Site kSiteFlat{ 0x5E, 0x6B, 0xC7 };
        inline constexpr Site kSiteAE{ 0x67, 0x74, 0x105 };

        inline constexpr std::uint8_t kExpectedXaddPrefix[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0xC1, 0x05 };

        struct PatchOverflowGuard final : Xbyak::CodeGenerator
        {
            PatchOverflowGuard(std::uintptr_t a_counterAddr, std::uint32_t a_limit,
                std::uintptr_t a_resume, std::uintptr_t a_noGroupExit)
            {
                Xbyak::Label retryLbl, noGroupExitLbl, resumeAddr, noGroupExitAddr;

                // The counter is SortAlphaGeometryGroups' qsort element count, so it must never pass the limit.
                mov(rcx, a_counterAddr);
                push(rdx);
                L(retryLbl);
                mov(eax, dword[rcx]);
                cmp(eax, a_limit);
                jae(noGroupExitLbl);
                lea(edx, ptr[rax + 1]);
                lock();
                cmpxchg(dword[rcx], edx);
                jne(retryLbl);
                pop(rdx);
                jmp(ptr[rip + resumeAddr]);

                L(noGroupExitLbl);
                pop(rdx);
                mov(eax, dword[rsp + kSavedEaxStackOffset]);
                jmp(ptr[rip + noGroupExitAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(noGroupExitAddr);
                dq(a_noGroupExit);
            }
        };

        inline std::size_t InstallSite(const Site& a_site, std::uint32_t a_limit)
        {
            const REL::Relocation<std::uintptr_t> function{ RELOCATION_ID(100874, 107670) };
            const std::uintptr_t                  patch = function.address() + a_site.patchOffset;
            const std::uintptr_t                  resume = function.address() + a_site.resumeOffset;
            const std::uintptr_t                  noGroupExit = function.address() + a_site.noGroupExitOffset;

            const auto* bytes = reinterpret_cast<const std::uint8_t*>(patch);
            if (!std::equal(std::begin(kExpectedXaddPrefix), std::end(kExpectedXaddPrefix), bytes)) {
                logger::warn("batchrenderer alpha geometry group overflow fix: unexpected bytes at StartGroupingAlphas+{:X}, skipping site"sv, a_site.patchOffset);
                return 0;
            }

            std::int32_t rel32;
            std::memcpy(&rel32, bytes + sizeof(kExpectedXaddPrefix), sizeof(rel32));
            const std::uintptr_t counter = patch + sizeof(kExpectedXaddPrefix) + sizeof(rel32) + rel32;

            PatchOverflowGuard p{ counter, a_limit, resume, noGroupExit };
            p.ready();
            REL::Relocation<std::uintptr_t>{ patch }.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            return 1;
        }
    }

    inline void Install()
    {
        const std::uint32_t capacity = REL::Module::IsVR() ? detail::kCapacityVR : detail::kCapacityFlat;
        const std::uint32_t requested = Settings::Fixes::iBatchRendererAlphaGeometryGroupLimit.GetValue();
        const std::uint32_t limit = requested == 0 ? capacity - detail::kCapacityMargin : (std::min)(requested, capacity);

        const std::size_t installed = detail::InstallSite(REL::Module::IsAE() ? detail::kSiteAE : detail::kSiteFlat, limit);

        if (installed > 0) {
            InstalledFixes::MarkInstalled("BatchRendererAlphaGeometryGroupOverflow"sv);
            logger::info("installed batchrenderer alpha geometry group overflow fix (limit={})"sv, limit);
        } else {
            logger::warn("batchrenderer alpha geometry group overflow fix: no sites matched, not installed"sv);
        }
    }
}
