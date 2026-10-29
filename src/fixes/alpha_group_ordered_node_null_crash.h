#pragma once

#include "installed_fixes.h"

#include <array>
#include <atomic>
#include <cstring>

// The alpha-group accumulate step does `node = FindBSOrderedNodeAncestor(geometry)` and passes
// `&node->worldBound` to BSShaderAccumulator::StartGroupingAlphas without a null check.
namespace Fixes::AlphaGroupOrderedNodeNullCrash
{
    namespace detail
    {
        inline constexpr std::uint8_t kExpectedPrologue[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC9
        };

        // Big enough to cover NiAVObject::worldBound on every runtime (+0xE4).
        inline constexpr std::size_t kStandInSize = 0x200;

        using FindAncestor_t = RE::NiAVObject* (*)(RE::NiAVObject*);

        inline SafetyHookInline                    orig_Find;
        inline std::array<std::byte, kStandInSize> standIn{};
        inline std::atomic<std::uint64_t>          fallbacks{ 0 };

        inline RE::NiAVObject* Find(RE::NiAVObject* a_object)
        {
            if (auto* node = orig_Find.call<RE::NiAVObject*>(a_object)) {
                return node;
            }

            if (fallbacks.fetch_add(1, std::memory_order_relaxed) == 0) {
                logger::warn("alpha group geometry has no BSOrderedNode ancestor; using its own bound (logged once)"sv);
            }

            // The caller only reads worldBound as the group's depth sort key.
            return a_object ? a_object : reinterpret_cast<RE::NiAVObject*>(standIn.data());
        }
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(101142, 108126) };  // FindBSOrderedNodeAncestor

        if (std::memcmp(reinterpret_cast<const void*>(target.address()), detail::kExpectedPrologue, sizeof(detail::kExpectedPrologue)) != 0) {
            logger::warn("alpha group ordered node crash fix: unexpected bytes at {:X}, skipping"sv, target.address());
            return;
        }

        detail::orig_Find = safetyhook::create_inline(target.address(), detail::Find);

        EF_INSTALLED("AlphaGroupOrderedNodeNullCrash"sv, "installed alpha group ordered node null crash fix"sv);
    }
}
