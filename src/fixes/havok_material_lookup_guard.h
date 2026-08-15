#pragma once

// MIT-licensed adaptation of Skyrim VR Havok Material Guard by Treatid.
// Standalone source: https://github.com/Treatid2/SkyrimVR-Havok-Material-Guard

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Fixes::HavokMaterialLookupGuard
{
    namespace detail
    {
        using MaterialResolver = const std::uint8_t* (*)(RE::hkpCompressedMeshShape*, RE::hkpShapeKey);

        inline constexpr std::uintptr_t               kMaterialEntrySize = 8;
        inline constexpr std::uintptr_t               kMaterialLookupRva = 0xE564D0;
        inline constexpr std::uintptr_t               kMaterialResolverRva = 0xE97490;
        inline constexpr std::array<std::uint8_t, 16> kExpectedLookupEntry{
            0x48, 0x83, 0xEC, 0x28,  // sub rsp, 28h
            0x48, 0x8B, 0x49, 0x10,  // mov rcx, [rcx + 10h]
            0x48, 0x85, 0xC9,        // test rcx, rcx
            0x74, 0x17,              // je native neutral return
            0x83, 0xFA, 0xFF         // cmp edx, -1
        };

        inline MaterialResolver           g_materialResolver = nullptr;
        inline std::atomic<std::uint32_t> g_recoveries{ 0 };
        inline std::uint32_t              g_reportedRecoveries = 0;

        [[nodiscard]] constexpr bool IsEntryInBounds(
            std::uintptr_t a_materialBase,
            std::uintptr_t a_materialAddress,
            std::uintptr_t a_materialStride,
            std::uintptr_t a_materialCount) noexcept
        {
            if (a_materialBase == 0 || a_materialStride < kMaterialEntrySize || a_materialCount == 0 ||
                a_materialCount > (std::numeric_limits<std::uintptr_t>::max)() / a_materialStride ||
                a_materialAddress < a_materialBase) {
                return false;
            }

            const auto tableBytes = a_materialStride * a_materialCount;
            const auto entryOffset = a_materialAddress - a_materialBase;
            return entryOffset % a_materialStride == 0 && entryOffset <= tableBytes - kMaterialEntrySize;
        }

        static_assert(IsEntryInBounds(0x1000, 0x1000, 8, 1));
        static_assert(IsEntryInBounds(0x1000, 0x1008, 8, 2));
        static_assert(IsEntryInBounds(0x1000, 0x1010, 16, 2));
        static_assert(!IsEntryInBounds(0, 0x1000, 8, 1));
        static_assert(!IsEntryInBounds(0x1000, 0x1000, 7, 1));
        static_assert(!IsEntryInBounds(0x1000, 0x1000, 8, 0));
        static_assert(!IsEntryInBounds(0x1000, 0x0FF8, 8, 1));
        static_assert(!IsEntryInBounds(0x1000, 0x1004, 8, 2));
        static_assert(!IsEntryInBounds(0x1000, 0x1010, 8, 2));
        static_assert(!IsEntryInBounds(
            0x1000,
            0x1000,
            8,
            (std::numeric_limits<std::uintptr_t>::max)() / 8 + 1));
        static_assert(!IsEntryInBounds(0x1000, 0x1000 + 0xFFFF * 8, 8, 0xFFFF));

        inline void RecordRecovery() noexcept
        {
            // Havok may call this on worker threads. Do not log, lock, or allocate here.
            g_recoveries.fetch_add(1, std::memory_order_relaxed);
        }

        inline std::uint32_t GuardedMaterialLookup(const void* a_shapeWrapper, RE::hkpShapeKey a_shapeKey) noexcept
        {
            if (!a_shapeWrapper || a_shapeKey == RE::HK_INVALID_SHAPE_KEY || !g_materialResolver)
                return 0;

            RE::hkpCompressedMeshShape* shape = nullptr;
            __try {
                shape = *reinterpret_cast<RE::hkpCompressedMeshShape* const*>(
                    static_cast<const std::uint8_t*>(a_shapeWrapper) + 0x10);
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                RecordRecovery();
                return 0;
            }

            if (!shape)
                return 0;

            // Keep compatible detours on the native decoder entry point live. Deliberately do not
            // hide a fault raised inside third-party decoder code.
            const auto* material = g_materialResolver(shape, a_shapeKey);
            if (!material)
                return 0;

            std::uint32_t materialID = 0;
            bool          validEntry = false;
            __try {
                const auto base = reinterpret_cast<std::uintptr_t>(shape->meshMaterials);
                const auto address = reinterpret_cast<std::uintptr_t>(material);
                const auto stride = static_cast<std::uintptr_t>(shape->materialStriding);
                const auto count = static_cast<std::uintptr_t>(shape->numMaterials);

                if (IsEntryInBounds(base, address, stride, count)) {
                    materialID = *reinterpret_cast<const std::uint32_t*>(material + 0x04);
                    validEntry = true;
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                validEntry = false;
            }

            if (!validEntry) {
                RecordRecovery();
                return 0;  // Skyrim's existing neutral material value
            }

            return materialID;
        }
    }

    inline void ReportRecoveries()
    {
        if (!REL::Module::IsVR())
            return;

        const auto recoveries = detail::g_recoveries.load(std::memory_order_relaxed);
        if (recoveries != detail::g_reportedRecoveries) {
            logger::warn("recovered {} invalid Havok compressed-mesh material lookup(s) this session"sv, recoveries);
            detail::g_reportedRecoveries = recoveries;
        }
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;

        static_assert(sizeof(RE::bhkMeshMaterial) == detail::kMaterialEntrySize);
        static_assert(offsetof(RE::hkpCompressedMeshShape, meshMaterials) == 0xF8);
        static_assert(offsetof(RE::hkpCompressedMeshShape, materialStriding) == 0x100);
        static_assert(offsetof(RE::hkpCompressedMeshShape, numMaterials) == 0x102);

        if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
            logger::warn("skipping Havok material lookup guard: unsupported VR runtime"sv);
            return;
        }

        const auto moduleBase = REL::Module::get().base();
        const auto materialLookup = moduleBase + detail::kMaterialLookupRva;
        if (!std::equal(
                detail::kExpectedLookupEntry.begin(),
                detail::kExpectedLookupEntry.end(),
                reinterpret_cast<const std::uint8_t*>(materialLookup))) {
            logger::warn("skipping Havok material lookup guard: patch-site signature mismatch or another plugin owns the site"sv);
            return;
        }

        detail::g_materialResolver = reinterpret_cast<detail::MaterialResolver>(moduleBase + detail::kMaterialResolverRva);
        SKSE::GetTrampoline().write_branch<5>(materialLookup, detail::GuardedMaterialLookup);
        logger::info("installed Havok compressed-mesh material lookup guard (VR)"sv);
    }
}
