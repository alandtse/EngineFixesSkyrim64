#pragma once

namespace Fixes::GlobalTime
{
    inline void Install()
    {
        const REL::Relocation<> secondsSinceLastFrameRealTime{ RELOCATION_ID(523661, 410200) };

        auto patch = [&](const REL::RelocationID& a_id, std::ptrdiff_t a_offset) {
            REL::Relocation<> target{ a_id, a_offset };
            const auto timerOffset = static_cast<std::int32_t>(
                secondsSinceLastFrameRealTime.address() - target.address() - 0x4);
            target.write(timerOffset);
        };

        // BookMenu::vf4 — VR prologue differs, disp32 at func+0x990 vs SE func+0xA95
        patch(RELOCATION_ID(50118, 51049), VAR_NUM(0xA91 + 0x4, 0xB70 + 0x4, 0x990));
        // SleepWaitMenu::vf4 — VR already reads secondsSinceLastFrameRealTime directly; skip
        if (!REL::Module::IsVR()) {
            patch(RELOCATION_ID(51614, 52486), VAR_NUM(0x1BC + 0x4, 0x1BE + 0x4));
        }
        // ThirdPersonState::UpdateDelayedParameters — same relative offsets in SE and VR
        patch(RELOCATION_ID(49977, 50913), VAR_NUM(0x2B + 0x4, 0x3B + 0x4));
        patch(RELOCATION_ID(49977, 50913), VAR_NUM(0x92 + 0x4, 0x9D + 0x4));
        patch(RELOCATION_ID(49977, 50913), VAR_NUM(0x1F9 + 0x4, 0x1B6 + 0x4));  // unreachable in VR (JMP at +0x1F8); harmless
        patch(RELOCATION_ID(49980, 50911), VAR_NUM(0xB6 + 0x4, 0x264 + 0x4));
        patch(RELOCATION_ID(49981, 50921), VAR_NUM(0x13 + 0x4, 0x13 + 0x4));

        logger::info("installed global time fix"sv);
    }
}
