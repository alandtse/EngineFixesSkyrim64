#pragma once
#include "settings.h"

namespace Patches::SaveGameMaxSize
{
    inline void Install()
    {
        if (!Settings::MemoryManager::bOverrideMemoryManager.GetValue()) {
            logger::info("skipping save game max size patch as it requires memory manager override patch"sv);
            return;
        }

        using PairT = std::pair<REL::RelocationID, std::ptrdiff_t>;
        const std::array<PairT, 3> todo = {
            PairT{ RELOCATION_ID(101985, 109378), VAR_NUM(0x11, 0x17) },
            PairT{ RELOCATION_ID(101962, 109355), VAR_NUM(0x14B, 0x20B) },
            PairT{ RELOCATION_ID(35203, 36095),   0x1 },
        };

        if (Settings::Patches::iSaveGameMaxSize.GetValue() > 4095) {
            logger::error("iSaveGameMaxSize of {} is too large"sv, Settings::Patches::iSaveGameMaxSize.GetValue());
            return;
        }

        std::uint32_t sizeBytes = Settings::Patches::iSaveGameMaxSize.GetValue() * 1024 * 1024;

        for (auto& [id, offset] : todo) {
            REL::Relocation target{ id, offset };
            target.write(sizeBytes);
        }

        logger::info("installed save game max size patch"sv, Settings::Patches::iSaveGameMaxSize.GetValue());
    }
}
