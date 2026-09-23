#pragma once

// Actor::GetCombatGroup() can return a non-null pointer that isn't a valid CombatGroup*
// (observed: 0x1) when read concurrently with SetCombatGroup on another thread; every
// caller's next dereference is then a wild read. This validates the returned pointer before
// handing it back, protecting every caller (vanilla and every plugin) from one patch site
// per runtime rather than each caller needing its own guard.
//
// SE/AE null-check an intermediate object (Actor+0x158 / +0x160) before reading its first
// field for the CombatGroup*; VR stores the CombatGroup* directly on Actor at +0x10D0. Both
// shapes are missing a check on that final loaded value.

#include <algorithm>
#include <cstdint>
#include <span>

namespace Fixes::CombatGroupStalePointerCrash
{
    namespace detail
    {
        // The first 64KiB of address space is never mapped on Windows; anything below it
        // (e.g. the observed 0x1) is never a real CombatGroup* and is safe to reject.
        [[nodiscard]] inline bool LooksLikeValidPointer(std::uintptr_t a_ptr) noexcept
        {
            return a_ptr >= 0x10000;
        }

        inline RE::CombatGroup* GetCombatGroupFlat(RE::Actor* a_this) noexcept
        {
            const auto  offset = REL::Module::IsAE() ? 0x160 : 0x158;
            const auto* sub = *reinterpret_cast<void* const*>(reinterpret_cast<const std::uint8_t*>(a_this) + offset);
            if (!sub) {
                return nullptr;
            }
            const auto raw = *reinterpret_cast<const std::uintptr_t*>(sub);
            return LooksLikeValidPointer(raw) ? reinterpret_cast<RE::CombatGroup*>(raw) : nullptr;
        }

        inline RE::CombatGroup* GetCombatGroupVR(RE::Actor* a_this) noexcept
        {
            const auto raw = *reinterpret_cast<const std::uintptr_t*>(reinterpret_cast<const std::uint8_t*>(a_this) + 0x10D0);
            return LooksLikeValidPointer(raw) ? reinterpret_cast<RE::CombatGroup*>(raw) : nullptr;
        }

        // clang-format off
        inline constexpr std::uint8_t kExpectedSE[] = {
            0x48, 0x8B, 0x81, 0x58, 0x01, 0x00, 0x00,  // mov rax, [rcx+0x158]
            0x48, 0x85, 0xC0,                          // test rax, rax
            0x74, 0x04,                                // jz +4
            0x48, 0x8B, 0x00,                          // mov rax, [rax]
            0xC3,                                      // ret
            0xC3,                                      // ret (jz target)
        };
        inline constexpr std::uint8_t kExpectedAE[] = {
            0x48, 0x8B, 0x81, 0x60, 0x01, 0x00, 0x00,  // mov rax, [rcx+0x160]
            0x48, 0x85, 0xC0,                          // test rax, rax
            0x74, 0x04,                                // jz +4
            0x48, 0x8B, 0x00,                          // mov rax, [rax]
            0xC3,                                      // ret
            0xC3,                                      // ret (jz target)
        };
        inline constexpr std::uint8_t kExpectedVR[] = {
            0x48, 0x8B, 0x81, 0xD0, 0x10, 0x00, 0x00,  // mov rax, [rcx+0x10D0]
            0xC3,                                      // ret
        };
        // clang-format on
    }

    inline void Install()
    {
        const REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(37601, 38554) };

        const bool                          isVR = REL::Module::IsVR();
        const bool                          isAE = REL::Module::IsAE();
        const std::span<const std::uint8_t> expected =
            isVR ? std::span<const std::uint8_t>{ detail::kExpectedVR } :
            isAE ? std::span<const std::uint8_t>{ detail::kExpectedAE } :
                   std::span<const std::uint8_t>{ detail::kExpectedSE };
        const char* label = isVR ? "VR" : isAE ? "AE" :
                                                 "SE";

        if (!std::equal(expected.begin(), expected.end(), reinterpret_cast<const std::uint8_t*>(target.address()))) {
            logger::warn("combat group stale pointer crash fix: unexpected bytes at {} site, not installed"sv, label);
            return;
        }

        SKSE::GetTrampoline().write_branch<5>(target.address(), isVR ? &detail::GetCombatGroupVR : &detail::GetCombatGroupFlat);
        logger::info("installed combat group stale pointer crash fix ({})"sv, label);
    }
}
