#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include "installed_fixes.h"
#include "util.h"

// StartGroupingAlphas claims alpha-group slots with an unchecked lock xadd; past the array's
// capacity the returned slot is garbage.
namespace Fixes::BatchRendererAlphaGeometryGroupOverflow
{
    namespace detail
    {
        inline constexpr std::uint32_t kCapacityFlat = 512;
        inline constexpr std::uint32_t kCapacityVR = 1024;
        inline constexpr std::uint32_t kCapacityMargin = 8;

        struct Site
        {
            std::uintptr_t patchOffset;    // start of "mov eax,1" (module-relative)
            std::uintptr_t resumeOffset;   // first untouched instruction after the displaced block
            std::uintptr_t skipOffset;     // shared "no group" tail also used when the input shape is null
            std::uintptr_t counterOffset;  // BSBatchRenderer__alphaGeometryGroupCount
        };

        inline constexpr Site kSiteSE{ 0x13092de, 0x13092eb, 0x1309347, 0x3283ba0 };
        inline constexpr Site kSiteVR{ 0x134a52e, 0x134a53b, 0x134a597, 0x34d6250 };
        inline constexpr Site kSiteAE1170{ 0x14f5137, 0x14f5144, 0x14f51d5, 0x35e9fd0 };
        inline constexpr Site kSiteAE1104{ 0x15616b7, 0x15616c4, 0x1561755, 0x36939d0 };  // 1.7.104+

        // Fixed bytes of "mov eax,1; lock xadd dword ptr [rip+X],eax", excluding the trailing
        // 4-byte rel32 (differs per runtime, verified separately below by resolving it).
        inline constexpr std::uint8_t kExpected[] = { 0xB8, 0x01, 0x00, 0x00, 0x00, 0xF0, 0x0F, 0xC1, 0x05 };

        struct PatchOverflowGuard final : Xbyak::CodeGenerator
        {
            PatchOverflowGuard(std::uintptr_t a_counterAddr, std::uint32_t a_limit,
                std::uintptr_t a_resume, std::uintptr_t a_skip)
            {
                Xbyak::Label retryLbl, skipLbl, resumeAddr, skipAddr;

                // The counter is SortAlphaGeometryGroups' qsort element count, so it must never pass the limit.
                mov(rcx, a_counterAddr);
                push(rdx);
                L(retryLbl);
                mov(eax, dword[rcx]);
                cmp(eax, a_limit);
                jae(skipLbl);
                lea(edx, ptr[rax + 1]);
                lock();
                cmpxchg(dword[rcx], edx);
                jne(retryLbl);
                pop(rdx);
                jmp(ptr[rip + resumeAddr]);

                L(skipLbl);
                pop(rdx);
                mov(eax, dword[rsp + 0x70]);
                jmp(ptr[rip + skipAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(skipAddr);
                dq(a_skip);
            }
        };

        inline std::size_t InstallSite(const Site& a_site, std::uint32_t a_limit)
        {
            const std::uintptr_t patch = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_site.patchOffset } }.address();
            const std::uintptr_t counter = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_site.counterOffset } }.address();
            const std::uintptr_t resume = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_site.resumeOffset } }.address();
            const std::uintptr_t skip = REL::Relocation<std::uintptr_t>{ REL::Offset{ a_site.skipOffset } }.address();

            const auto* bytes = reinterpret_cast<const std::uint8_t*>(patch);
            if (!std::equal(std::begin(kExpected), std::end(kExpected), bytes)) {
                logger::warn("batchrenderer alpha geometry group overflow fix: unexpected bytes at {:X}, skipping site"sv, a_site.patchOffset);
                return 0;
            }

            // The XADD's rel32 (bytes[9..13)) must resolve to the counter global itself --
            // the one part of the instruction kExpected can't check statically.
            std::int32_t rel32;
            std::memcpy(&rel32, bytes + 9, sizeof(rel32));
            if (static_cast<std::uintptr_t>(patch + 13 + rel32) != counter) {
                logger::warn("batchrenderer alpha geometry group overflow fix: XADD target mismatch at {:X}, skipping site"sv, a_site.patchOffset);
                return 0;
            }

            PatchOverflowGuard p{ counter, a_limit, resume, skip };
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

        std::size_t installed = 0;
        if (REL::Module::IsVR()) {
            installed += detail::InstallSite(detail::kSiteVR, limit);
        } else if (REL::Module::IsAE()) {
            installed += detail::InstallSite(util::IsAE1799() ? detail::kSiteAE1104 : detail::kSiteAE1170, limit);
        } else {
            installed += detail::InstallSite(detail::kSiteSE, limit);
        }

        if (installed > 0) {
            InstalledFixes::MarkInstalled("BatchRendererAlphaGeometryGroupOverflow"sv);
            logger::info("installed batchrenderer alpha geometry group overflow fix (limit={})"sv, limit);
        } else {
            logger::warn("batchrenderer alpha geometry group overflow fix: no sites matched, not installed"sv);
        }
    }
}
