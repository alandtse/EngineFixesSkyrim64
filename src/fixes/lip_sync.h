#pragma once

namespace Fixes::LipSync
{
    inline void Install()
    {
        const std::array offsets{
            VAR_NUM(0x1E, 0x27),
            VAR_NUM(0x3A, 0x67),
            VAR_NUM(0x9A, 0xF4),
            VAR_NUM(0xD8, 0x126),
        };

        REL::Relocation<std::uintptr_t> targetBase{ RELOCATION_ID(16023, 16267) };

        constexpr auto JMP = std::uint8_t{ 0xEB };

        for (auto offset : offsets) {
            REL::Relocation<std::uintptr_t>{ targetBase.address() + offset }.write(JMP);
        }

        logger::info("installed lip sync fix"sv);
    }
}
