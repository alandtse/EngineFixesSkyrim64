#pragma once

// The console SaveGame handler runs BGSSaveLoadManager::Save_Impl synchronously
// on whatever thread drains the console command queue. When that is not the
// main thread (programmatic Console::ExecuteCommand commands can drain on a
// task thread), the game deadlocks: the main thread's save-load manager update
// holds the VM save mutex while it waits for the save job, and Save_Impl blocks
// forever acquiring that mutex inside SkyrimVM::Freeze. No crash, no crash log.
//
// Fix: hook the handler's Save_Impl call (handler+0xC4 in all three runtimes,
// after the handler's own can-save check and flag setup) and defer any
// off-main-thread call to the main thread via the SKSE task queue -- the same
// context the pause-menu save runs in. Main-thread calls pass through
// unchanged, so a save typed into the console behaves exactly as before.

namespace Fixes::ConsoleSaveDeadlock
{
    namespace detail
    {
        inline std::uint32_t g_mainThreadId{ 0 };

        inline REL::Relocation<bool(RE::BGSSaveLoadManager*, std::uint32_t, std::uint32_t, const char*)> orig_SaveImpl;

        inline bool Hook(RE::BGSSaveLoadManager* a_manager, std::uint32_t a_deviceID, std::uint32_t a_flags, const char* a_fileName)
        {
            auto* task = SKSE::GetTaskInterface();
            if (::GetCurrentThreadId() == g_mainThreadId || !task)
                return orig_SaveImpl(a_manager, a_deviceID, a_flags, a_fileName);

            // a_fileName points into the console handler's stack frame; copy it
            // before deferring.
            std::string fileName{ a_fileName ? a_fileName : "" };
            logger::debug("deferring off-main-thread console save '{}' to the main thread"sv, fileName);
            task->AddTask([a_manager, a_deviceID, a_flags, fileName = std::move(fileName)]() {
                orig_SaveImpl(a_manager, a_deviceID, a_flags, fileName.empty() ? nullptr : fileName.c_str());
            });
            return true;
        }
    }

    inline void Install()
    {
        // Install() runs on the game's primary thread, which is the thread that
        // later runs MainLoop.
        detail::g_mainThreadId = ::GetCurrentThreadId();

        // Console SaveGame handler entry (VR resolves 22465 via the VR Address
        // Library; requires a release containing that id).
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(22465, 22940), 0xC4 };
        detail::orig_SaveImpl = target.write_call<5>(detail::Hook);

        logger::info("installed console save deadlock fix"sv);
    }
}
