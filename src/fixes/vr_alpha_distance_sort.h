#pragma once

#include "detail/alpha_distance_sort.h"
#include "detail/alpha_distance_sort_code.h"
#include "installed_fixes.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>

namespace Fixes::VRAlphaDistanceSort
{
    namespace detail
    {
        using SortList = void (*)(RE::BSRenderPass**, const RE::NiCamera*, bool, bool);
        using RenderAlphas = void (*)(RE::NiAccumulator*, std::uint32_t);

        inline SortList                                     originalSort{};
        inline RenderAlphas                                 originalRender{};
        inline std::array<std::unique_ptr<KeyCode>, 3>      keyCode;
        inline std::array<safetyhook::InlineHook, 5>        hooks;
        inline thread_local std::optional<policy::Position> consumingOrigin;
        inline std::atomic_bool                             active{};
        inline bool                                         attempted{};

        template <class... Args>
        inline void Warn(spdlog::format_string_t<Args...> a_format, Args&&... a_args) noexcept
        {
            try {
                logger::warn(a_format, std::forward<Args>(a_args)...);
            } catch (...) {
            }
        }

        inline policy::Position Position(const RE::NiPoint3& a_point) noexcept
        {
            return { a_point.x, a_point.y, a_point.z };
        }

        inline std::optional<policy::Position> Origin(const RE::NiCamera* a_camera) noexcept
        {
            if (!a_camera)
                return std::nullopt;
            const auto origin = Position(a_camera->world.translate);
            return policy::IsFinite(origin) ? std::optional{ origin } : std::nullopt;
        }

        inline float DistanceKey(const RE::NiBound& a_bound, policy::Position a_origin, float a_closest) noexcept
        {
            return policy::DistanceKey(Position(a_bound.center), a_bound.radius, a_origin, a_closest);
        }

        inline float ProjectedKey(const RE::NiBound& a_bound, const RE::NiPoint3& a_direction, float a_closest) noexcept
        {
            return policy::ProjectedDepth(Position(a_bound.center), Position(a_direction)) - a_closest * a_bound.radius;
        }

        inline float GroupKey(const RE::NiBound* a_bound, const RE::NiCamera* a_camera, bool a_closest) noexcept
        {
            if (active.load(std::memory_order_acquire)) {
                if (const auto origin = Origin(a_camera))
                    return DistanceKey(*a_bound, *origin, a_closest ? 1.0f : 0.0f);
            }
            const auto& rotation = a_camera->world.rotate.entry;
            return policy::ProjectedGroupKey(Position(a_bound->center), a_bound->radius,
                { rotation[0][0], rotation[1][0], rotation[2][0] }, a_closest);
        }

        inline float QueueKey(const RE::NiBound* a_bound, const RE::NiPoint3* a_direction, float a_closest) noexcept
        {
            return active.load(std::memory_order_acquire) && consumingOrigin ?
                       DistanceKey(*a_bound, *consumingOrigin, a_closest) :
                       ProjectedKey(*a_bound, *a_direction, a_closest);
        }

        inline void Sort(RE::BSRenderPass** a_head, const RE::NiCamera* a_camera, bool a_closest, bool a_reverse)
        {
            if (!a_head || !*a_head || !(*a_head)->passGroupNext)
                return;
            // The shared helper also sorts opaque batches in the opposite direction.
            if (!a_reverse && active.load(std::memory_order_acquire)) {
                if (const auto origin = Origin(a_camera)) {
                    policy::Sort(*a_head, [&](const RE::BSRenderPass* a_pass) noexcept {
                        return a_pass->geometry ? DistanceKey(a_pass->geometry->worldBound, *origin, a_closest ? 1.0f : 0.0f) : 0.0f;
                    });
                    return;
                }
            }
            originalSort(a_head, a_camera, a_closest, a_reverse);
        }

        inline void Render(RE::NiAccumulator* a_accumulator, std::uint32_t a_flags)
        {
            if (!active.load(std::memory_order_acquire)) {
                originalRender(a_accumulator, a_flags);
                return;
            }
            struct Scope
            {
                std::optional<policy::Position> previous = consumingOrigin;
                ~Scope() { consumingOrigin = previous; }
            } scope;
            consumingOrigin = Origin(a_accumulator->camera);
            originalRender(a_accumulator, a_flags);
        }

        struct Site
        {
            std::uintptr_t                address;
            std::span<const std::uint8_t> expected;
            void*                         replacement{};
        };

        inline bool Matches(const Site& a_site)
        {
            std::array<std::uint8_t, 65> actual{};
            SIZE_T                       read{};
            return a_site.expected.size() <= actual.size() &&
                   ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(a_site.address), actual.data(), a_site.expected.size(), &read) &&
                   read == a_site.expected.size() && std::equal(a_site.expected.begin(), a_site.expected.end(), actual.begin());
        }

        inline void RollBack()
        {
            active.store(false, std::memory_order_release);
            for (auto it = hooks.rbegin(); it != hooks.rend(); ++it) {
                auto& hook = *it;
                if (!hook.enabled())
                    continue;
                try {
                    if (const auto result = hook.disable(); !result)
                        Warn("VR alpha distance sort: could not remove hook at {:X}; retaining native-key fallback"sv, hook.target_address());
                } catch (const std::exception& error) {
                    Warn("VR alpha distance sort: rollback at {:X} failed ({}); retaining native-key fallback"sv, hook.target_address(), error.what());
                } catch (...) {
                    Warn("VR alpha distance sort: rollback at {:X} failed; retaining native-key fallback"sv, hook.target_address());
                }
            }
            // A hook that could not be removed still needs its bridge and original trampoline.
        }
    }

    inline void Install()
    {
        using namespace detail;
        if (attempted || !REL::Module::IsVR())
            return;
        attempted = true;
        if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
            Warn("VR alpha distance sort: unsupported runtime, skipping"sv);
            return;
        }

        static_assert(offsetof(RE::NiAVObject, world) == 0x7C && offsetof(RE::NiAVObject, worldBound) == 0xE4);
        static_assert(offsetof(RE::NiTransform, translate) == 0x24 && offsetof(RE::NiAccumulator, camera) == 0x10);
        static_assert(offsetof(RE::BSRenderPass, geometry) == 0x10 && offsetof(RE::BSRenderPass, passGroupNext) == 0x30);

        const auto base = REL::Module::get().base();
        const auto render = REL::ID(99940).address();
        const auto group = REL::ID(100874).address() + 0x82;
        const auto consume = REL::ID(100877).address() + 0xAB;
        const auto groupCount = REL::ID(528319).address();
        // These two functions have no released VR Address Library IDs yet; keep the exact-runtime byte guards.
        constexpr std::uintptr_t kSortRVA = 0x133FF00;
        constexpr std::uintptr_t kPeekKeyRVA = 0x134A64C;

        constexpr std::array<std::uint8_t, 14> sortBytes{ 0x48, 0x89, 0x6C, 0x24, 0x18, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57 };
        constexpr std::array<std::uint8_t, 15> renderBytes{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18 };
        constexpr std::array<std::uint8_t, 53> groupBytes{
            0xF3, 0x0F, 0x10, 0x47, 0x7C, 0xF3, 0x0F, 0x10, 0x97, 0x88, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x8F, 0x94, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x06, 0xF3, 0x0F, 0x59, 0x56, 0x04, 0xF3, 0x0F, 0x59, 0x4E, 0x08, 0xF3, 0x0F, 0x58, 0xD0, 0xF3, 0x0F, 0x58, 0xD1, 0x45, 0x84, 0xFF, 0x74, 0x05, 0xF3, 0x0F, 0x5C, 0x56, 0x0C
        };
        constexpr std::array<std::uint8_t, 64> peekBytes{
            0xF3, 0x0F, 0x10, 0x90, 0xE8, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x52, 0x04, 0xF3, 0x0F, 0x10, 0x88, 0xEC, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x59, 0x98, 0xF0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x10, 0x02, 0xF3, 0x0F, 0x59, 0x80, 0xE4, 0x00, 0x00, 0x00, 0x8B, 0x05, 0xD5, 0xBB, 0x18, 0x02, 0xF3, 0x0F, 0x59, 0x4A, 0x08, 0xF3, 0x0F, 0x58, 0xD0, 0xF3, 0x0F, 0x58, 0xD1, 0xF3, 0x0F, 0x5C, 0xD3
        };
        constexpr std::array<std::uint8_t, 65> consumeBytes{
            0xF3, 0x0F, 0x10, 0x80, 0xE4, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x07, 0xF3, 0x0F, 0x10, 0xB0, 0xE8, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x77, 0x04, 0xF3, 0x0F, 0x10, 0x88, 0xEC, 0x00, 0x00, 0x00, 0xF3, 0x41, 0x0F, 0x59, 0x4F, 0x08, 0xF3, 0x0F, 0x58, 0xF0, 0x41, 0x0F, 0x28, 0xC0, 0xF3, 0x0F, 0x59, 0x80, 0xF0, 0x00, 0x00, 0x00, 0xF3, 0x0F, 0x58, 0xF1, 0xF3, 0x0F, 0x5C, 0xF0
        };
        // The final drain compares against an empty sentinel, not a projected-depth cutoff.
        constexpr std::array<std::uint8_t, 40> drainBytes{
            0xF3, 0x0F, 0x10, 0x35, 0x9B, 0x3C, 0x2A, 0x00, 0x0F, 0x2F, 0xC6, 0x48, 0x8B, 0x8B, 0x58, 0x01, 0x00, 0x00, 0x76, 0x2A, 0x0F, 0x28, 0xDE, 0x89, 0x7C, 0x24, 0x20, 0x0F, 0x28, 0xD7, 0x48, 0x8D, 0x54, 0x24, 0x30, 0xE8, 0x6F, 0x03, 0x04, 0x00
        };
        constexpr std::array<std::uint8_t, 4> emptyBytes{ 0xFF, 0xFF, 0x7F, 0xFF };
        std::int32_t                          sentinelDisplacement{}, countDisplacement{};
        std::memcpy(&sentinelDisplacement, drainBytes.data() + 4, sizeof(sentinelDisplacement));
        std::memcpy(&countDisplacement, peekBytes.data() + 43, sizeof(countDisplacement));
        const auto drain = render + 0x119;
        const auto sentinel = drain + 8 + sentinelDisplacement;
        if (groupCount != base + kPeekKeyRVA + 47 + countDisplacement) {
            Warn("VR alpha distance sort: group-count relocation mismatch, skipping"sv);
            return;
        }
        std::array sites{
            Site{ base + kSortRVA, sortBytes, reinterpret_cast<void*>(&Sort) },
            Site{ render, renderBytes, reinterpret_cast<void*>(&Render) },
            Site{ group, groupBytes },
            Site{ base + kPeekKeyRVA, peekBytes },
            Site{ consume, consumeBytes },
            Site{ drain, drainBytes },
            Site{ sentinel, emptyBytes }
        };
        for (const auto& site : sites) {
            if (!Matches(site)) {
                Warn("VR alpha distance sort: unexpected bytes at {:X}, skipping"sv, site.address);
                return;
            }
        }

        std::string capability;
        auto&       registry = InstalledFixes::Registry();
        try {
            capability = InstalledFixes::Public::kVRAlphaDistanceSort;
            for (std::size_t i = 0; i < keyCode.size(); ++i) {
                auto&      site = sites[i + 2];
                const auto helper = i == 0 ? reinterpret_cast<std::uintptr_t>(&GroupKey) : reinterpret_cast<std::uintptr_t>(&QueueKey);
                keyCode[i] = std::make_unique<KeyCode>(static_cast<KeySite>(i), helper, site.address + site.expected.size(), groupCount);
                site.replacement = const_cast<std::uint8_t*>(keyCode[i]->getCode());
            }
            for (std::size_t i = 0; i < hooks.size(); ++i) {
                auto created = safetyhook::InlineHook::create(reinterpret_cast<void*>(sites[i].address), sites[i].replacement, safetyhook::InlineHook::StartDisabled);
                if (!created) {
                    Warn("VR alpha distance sort: could not prepare hook at {:X} ({}), skipping"sv, sites[i].address, static_cast<unsigned>(created.error().type));
                    return;
                }
                hooks[i] = std::move(*created);
            }
            originalSort = hooks[0].original<SortList>();
            originalRender = hooks[1].original<RenderAlphas>();
            // Preload only: all callbacks retain native keys until every hook is enabled.
            for (auto& hook : hooks) {
                if (const auto result = hook.enable(); !result) {
                    RollBack();
                    Warn("VR alpha distance sort: could not enable hook at {:X} ({}), skipping"sv, hook.target_address(), static_cast<unsigned>(result.error().type));
                    return;
                }
            }
        } catch (const std::exception& error) {
            RollBack();
            Warn("VR alpha distance sort: installation failed ({}), skipping"sv, error.what());
            return;
        } catch (...) {
            RollBack();
            Warn("VR alpha distance sort: installation failed, skipping"sv);
            return;
        }

        active.store(true, std::memory_order_release);
        try {
            EF_INSTALLED(InstalledFixes::Public::kVRAlphaDistanceSort, "installed VR alpha distance sort"sv);
        } catch (...) {
            // A logging failure after registration must not invalidate a published capability.
            if (!registry.contains(capability)) {
                RollBack();
                Warn("VR alpha distance sort: capability registration failed, skipping"sv);
            }
        }
    }
}
