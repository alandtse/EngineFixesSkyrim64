#pragma once

namespace Fixes::GetGameSettingNotFoundCrash
{
    // Vanilla null-pointer crash in the `GetGameSetting` (console `getgs`) script-command handler.
    //
    // When GameSettingCollection::GetSetting(name) returns null (setting not found) AND the
    // console-echo flag is set, the not-found branch falls into the shared ConsoleLog::Log tail:
    //     ConsoleLog::Log(gConsoleLog, "GameSetting %s >> NOT FOUND", name, <unused 4th vararg>)
    // The compiler emits the unused 4th vararg (r9) as *(setting + 8) with `setting` constant-folded
    // to 0, producing `mov r9, qword ptr [0x8]` -> reads unmapped address 0x8 -> EXCEPTION_ACCESS_VIOLATION.
    // The %s (name) argument is valid; only the dead r9 load faults.
    //
    // Repro: `getgs <nonexistent_setting>` from the console.
    // Present identically in SE 1.5.97 (0x1402f2456), AE 1.6.1170 (0x140348715) and VR (0x140303956).
    //
    // Handler is address library id 21549 (AE id 22031). The faulting instruction sits at:
    //   SE  func+0x1D6   AE  func+0x1A5   VR  func+0x1D6
    // r9 is an unused vararg, so we replace the 8-byte `mov r9,[0x8]` with `xor r9d,r9d` + nop5.
    // The "GameSetting %s >> NOT FOUND" message then prints correctly instead of crashing.
    inline void Install()
    {
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(21549, 22031), VAR_NUM(0x1D6, 0x1A5, 0x1D6) };

        // 45 31 C9             xor r9d, r9d
        // 0F 1F 44 00 00       nop dword ptr [rax + rax*1 + 0x0]
        constexpr std::array<std::uint8_t, 8> patch{ 0x45, 0x31, 0xC9, 0x0F, 0x1F, 0x44, 0x00, 0x00 };
        REL::safe_write(target.address(), patch.data(), patch.size());

        logger::info("installed getgamesetting not found crash fix"sv);
    }
}
