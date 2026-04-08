#pragma once

namespace Fixes::ClimateLoad
{
    namespace detail
    {
        struct Sky
        {
            static void LoadGame(RE::Sky* a_self, RE::BGSLoadGameBuffer* a_loadGameBuffer)
            {
                _LoadGame(a_self, a_loadGameBuffer);

                using Flags = RE::Sky::Flags;
                a_self->flags.set(Flags::kUpdateSunriseBegin, Flags::kUpdateSunriseEnd, Flags::kUpdateSunsetBegin, Flags::kUpdateSunsetEnd, Flags::kUpdateColorsSunriseBegin, Flags::kUpdateColorsSunsetEnd);
            }

            static inline REL::Relocation<decltype(LoadGame)> _LoadGame;
        };
    }

    inline void Install()
    {
        using PairT = std::pair<REL::RelocationID, std::ptrdiff_t>;
        const std::array<PairT, 2> todo = {
            PairT{ RELOCATION_ID(25675, 26218), VAR_NUM(0x124, 0x1A0) },
            PairT{ RELOCATION_ID(34736, 35642), VAR_NUM(0x100, 0x241) },
        };

        for (const auto& [id, offset] : todo) {
            REL::Relocation target{ id, offset };
            detail::Sky::_LoadGame = target.write_call<5>(detail::Sky::LoadGame);
        }

        logger::info("installed climate load fix"sv);
    }
}
