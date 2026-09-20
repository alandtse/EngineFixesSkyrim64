#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

#include "installed_fixes.h"

// AppendVirtual writes through PopFreeQueueEntry's result unchecked; the pool returns null once exhausted.
namespace Fixes::CullingProcessAppendVirtualPoolGuard
{
    namespace detail
    {
        inline constexpr std::size_t    kAppendVirtualSlot = 0x18;
        inline constexpr std::uintptr_t kFreePoolOffset = 0x20150;
        inline constexpr std::uintptr_t kPoolHeadOffset = 0x10000;
        inline constexpr std::uintptr_t kPoolTailOffset = 0x10008;
        inline constexpr std::uintptr_t kNonAccumFlagOffset = 0x301d5;
        inline constexpr std::uint32_t  kMinFreeEntries = 16;

        inline constexpr std::uint8_t kExpectedPrologue[] = {
            0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x80
        };

        using AppendVirtual_t = void (*)(RE::BSCullingProcess*, RE::BSGeometry*, std::int32_t);
        inline AppendVirtual_t            originalAppendVirtual = nullptr;
        inline std::atomic<std::uint64_t> droppedAppends{ 0 };

        inline bool WouldPopFreePool(const std::uint8_t* a_this, std::int32_t a_alphaGroupIndex)
        {
            return a_this[kNonAccumFlagOffset] != 0 || a_alphaGroupIndex != -1;
        }

        inline void AppendVirtualGuarded(RE::BSCullingProcess* a_this, RE::BSGeometry* a_geometry, std::int32_t a_alphaGroupIndex)
        {
            const auto* base = reinterpret_cast<const std::uint8_t*>(a_this);
            if (WouldPopFreePool(base, a_alphaGroupIndex)) {
                const auto* pool = base + kFreePoolOffset;
                const auto  head = std::atomic_ref<const std::uint32_t>(*reinterpret_cast<const std::uint32_t*>(pool + kPoolHeadOffset)).load(std::memory_order_acquire);
                const auto  tail = std::atomic_ref<const std::uint32_t>(*reinterpret_cast<const std::uint32_t*>(pool + kPoolTailOffset)).load(std::memory_order_acquire);
                if (tail - head < kMinFreeEntries) {
                    if (droppedAppends.fetch_add(1, std::memory_order_relaxed) == 0) {
                        logger::warn("culling process free pool exhausted; dropping shadow caster appends (logged once)"sv);
                    }
                    return;
                }
            }
            originalAppendVirtual(a_this, a_geometry, a_alphaGroupIndex);
        }
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> cullingVtbl{ RE::BSCullingProcess::VTABLE[0] };
        REL::Relocation<std::uintptr_t> parabolicVtbl{ RE::BSParabolicCullingProcess::VTABLE[0] };

        const auto cullingTarget = reinterpret_cast<const std::uintptr_t*>(cullingVtbl.address())[detail::kAppendVirtualSlot];
        const auto parabolicTarget = reinterpret_cast<const std::uintptr_t*>(parabolicVtbl.address())[detail::kAppendVirtualSlot];
        if (cullingTarget != parabolicTarget ||
            !std::equal(std::begin(detail::kExpectedPrologue), std::end(detail::kExpectedPrologue),
                reinterpret_cast<const std::uint8_t*>(cullingTarget))) {
            logger::warn("culling process AppendVirtual pool guard: unexpected slot {:#x} target, skipping"sv, detail::kAppendVirtualSlot);
            return;
        }

        detail::originalAppendVirtual = reinterpret_cast<detail::AppendVirtual_t>(cullingTarget);
        cullingVtbl.write_vfunc(detail::kAppendVirtualSlot, detail::AppendVirtualGuarded);
        parabolicVtbl.write_vfunc(detail::kAppendVirtualSlot, detail::AppendVirtualGuarded);

        InstalledFixes::MarkInstalled("CullingProcessAppendVirtualPoolGuard"sv);
        logger::info("installed culling process AppendVirtual pool guard"sv);
    }
}
