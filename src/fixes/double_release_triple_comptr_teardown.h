#pragma once

#include <array>
#include <cstdint>
#include <cstring>

namespace Fixes::DoubleReleaseTripleComPtrTeardown
{
    // Vanilla AE 1.7.99+ regression: releases its 3 owned COM sub-pointers twice (1.6.1170 releases once).
    namespace detail
    {
        struct Site
        {
            REL::Version   version;
            std::uintptr_t patchRva;    // start of the redundant second release pass
            std::uintptr_t landingRva;  // shared dealloc tail, right after the (skipped) second pass
        };

        inline constexpr Site kSites[] = {
            { REL::Version{ 1, 7, 99, 0 }, 0x100EF7E, 0x100EFAB },
            { REL::Version{ 1, 7, 104, 0 }, 0x100F1DE, 0x100F20B },
        };

        // Full redundant second pass, validated so a stale offset can't land the jump mid-instruction.
        inline constexpr std::array<std::uint8_t, 45> kExpected{
            0x48, 0x8B, 0x0B, 0x48, 0x85, 0xC9, 0x74, 0x06, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x10,
            0x48, 0x8B, 0x4B, 0x08, 0x48, 0x85, 0xC9, 0x74, 0x06, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x10,
            0x48, 0x8B, 0x4B, 0x10, 0x48, 0x85, 0xC9, 0x74, 0x07, 0x48, 0x8B, 0x01, 0xFF, 0x50, 0x10, 0x90
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsAE())
            return;

        const detail::Site* site = nullptr;
        const auto          version = REL::Module::get().version();
        for (const auto& s : detail::kSites) {
            if (version == s.version) {
                site = &s;
                break;
            }
        }
        if (!site)
            return;

        REL::Relocation<std::uintptr_t> target{ REL::Offset{ site->patchRva } };
        const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(target.address());
        if (!std::equal(detail::kExpected.begin(), detail::kExpected.end(), bytes)) {
            logger::warn("double-release triple-COM-ptr teardown fix: unexpected bytes at {:X}, skipping"sv,
                site->patchRva);
            return;
        }

        const auto                  landing = REL::Relocation<std::uintptr_t>{ REL::Offset{ site->landingRva } }.address();
        std::array<std::uint8_t, 5> jmp{ 0xE9, 0, 0, 0, 0 };
        const auto                  rel = static_cast<std::int32_t>(landing - (target.address() + jmp.size()));
        std::memcpy(jmp.data() + 1, &rel, sizeof(rel));
        REL::safe_write(target.address(), jmp.data(), jmp.size());

        logger::info("installed double-release triple-COM-ptr teardown fix"sv);
    }
}
