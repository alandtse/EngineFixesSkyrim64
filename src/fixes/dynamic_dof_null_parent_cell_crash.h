#pragma once

#include "installed_fixes.h"

namespace Fixes::DynamicDOFNullParentCellCrash
{
    namespace detail
    {
        constexpr std::uint8_t kCallOpcode = 0xE8;
        constexpr std::uint8_t kMovRcxRdi[] = { 0x48, 0x8B, 0xCF };
        constexpr std::uint8_t kMovRcxRsi[] = { 0x48, 0x8B, 0xCE };

        inline REL::Relocation<void(RE::PlayerCharacter*)> orig_UpdateDynamicDOF;

        inline void Hook(RE::PlayerCharacter* a_player)
        {
            if (!a_player->GetParentCell())
                return;

            orig_UpdateDynamicDOF(a_player);
        }

        // `mov rcx, player` (rdi on SE/VR, rsi on AE) then `call UpdateDynamicDOF`.
        inline bool SiteMatches(std::uintptr_t a_call)
        {
            const auto* movRcx = reinterpret_cast<const std::uint8_t*>(a_call - sizeof(kMovRcxRdi));
            const bool  movMatches = std::memcmp(movRcx, kMovRcxRdi, sizeof(kMovRcxRdi)) == 0 ||
                                    std::memcmp(movRcx, kMovRcxRsi, sizeof(kMovRcxRsi)) == 0;
            return movMatches && *reinterpret_cast<const std::uint8_t*>(a_call) == kCallOpcode;
        }
    }

    inline void Install()
    {
        // PlayerCharacter::Update's call to UpdateDynamicDOF: SE +0xD6, AE +0xDC (1.6.1170 and 1.7.104), VR +0xDB.
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(39375, 40447), REL::Relocate(0xD6, 0xDC, 0xDB) };

        if (!detail::SiteMatches(target.address())) {
            logger::warn("dynamic DOF null parent cell crash fix: unexpected bytes at PlayerCharacter::Update call site, skipping"sv);
            return;
        }

        detail::orig_UpdateDynamicDOF = target.write_call<5>(detail::Hook);

        EF_INSTALLED("DynamicDOFNullParentCellCrash"sv, "installed dynamic DOF null parent cell crash fix"sv);
    }
}
