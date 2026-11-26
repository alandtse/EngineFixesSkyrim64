#pragma once

#include "installed_fixes.h"

namespace Fixes::TangentSpaceSignedIndexCrash
{
    // The tangent builder treats 16-bit vertex indices as signed, so on meshes with more than 32768 vertices
    // the high indices turn negative and it writes outside its buffer. The patch reads them as unsigned.
    namespace detail
    {
        struct Site
        {
            std::uintptr_t offset;  // from the function start
            std::uint8_t   expected[4];
        };

        constexpr Site kSitesVR[] = {
            { 0xDB, { 0x48, 0x0F, 0xBF, 0xD0 } },  // movsx rdx, ax
            { 0xE3, { 0x49, 0x0F, 0xBF, 0xC1 } },  // movsx rax, r9w
            { 0xE7, { 0x48, 0x0F, 0xBF, 0xC9 } },  // movsx rcx, cx
        };

        constexpr std::size_t  kOpcodeByte = 2;
        constexpr std::uint8_t kMovzx = 0xB7;
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;  // only verified on VR 1.4.15

        REL::Relocation<std::uintptr_t> func{ REL::Offset(0xDCC890) };  // tangent-space builder

        for (const auto& site : detail::kSitesVR) {
            if (!REL::verify_code(func.address() + site.offset, site.expected)) {
                logger::warn("tangent space signed index crash fix: unexpected bytes at func+{:#x} (signature mismatch or another plugin already patched it), skipping"sv, site.offset);
                return;
            }
        }

        for (const auto& site : detail::kSitesVR)
            REL::safe_write(func.address() + site.offset + detail::kOpcodeByte, detail::kMovzx);

        EF_INSTALLED("TangentSpaceSignedIndexCrash"sv, "installed tangent space signed index crash fix (VR)"sv);
    }
}
