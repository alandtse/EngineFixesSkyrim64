#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

// Records who frees a small set of watched heap blocks. Disabled unless a diagnostic registers a block.
namespace Memory::FreeWatch
{
    inline constexpr std::size_t kSlots = 64;
    inline constexpr std::size_t kFrames = 20;

    struct FreeRecord
    {
        std::atomic<bool>          freed{ false };
        std::uint32_t              threadId{ 0 };
        std::uint64_t              tick{ 0 };
        std::uint32_t              frameCount{ 0 };
        std::array<void*, kFrames> frames{};
    };

    inline std::atomic<std::uint32_t>                      matched{ 0 };
    inline std::atomic<bool>                               enabled{ false };
    inline std::array<std::atomic<std::uintptr_t>, kSlots> addresses{};
    inline std::array<FreeRecord, kSlots>                  records{};

    // returns the slot used, or kSlots when the table is full
    inline std::size_t Watch(const void* a_block)
    {
        for (std::size_t i = 0; i < kSlots; ++i) {
            std::uintptr_t expected = 0;
            if (addresses[i].compare_exchange_strong(expected, reinterpret_cast<std::uintptr_t>(a_block))) {
                records[i].freed.store(false, std::memory_order_relaxed);
                enabled.store(true, std::memory_order_release);
                return i;
            }
        }
        return kSlots;
    }

    inline void Unwatch(std::size_t a_slot)
    {
        if (a_slot < kSlots) {
            addresses[a_slot].store(0, std::memory_order_release);
            records[a_slot].freed.store(false, std::memory_order_relaxed);
        }
    }

    inline void OnFree(const void* a_block)
    {
        if (!enabled.load(std::memory_order_relaxed) || !a_block) {
            return;
        }
        const auto address = reinterpret_cast<std::uintptr_t>(a_block);
        for (std::size_t i = 0; i < kSlots; ++i) {
            if (addresses[i].load(std::memory_order_relaxed) != address) {
                continue;
            }
            matched.fetch_add(1, std::memory_order_relaxed);
            auto& record = records[i];
            record.threadId = ::GetCurrentThreadId();
            record.tick = ::GetTickCount64();
            record.frameCount = ::RtlCaptureStackBackTrace(1, static_cast<DWORD>(kFrames), record.frames.data(), nullptr);
            record.freed.store(true, std::memory_order_release);
            return;
        }
    }
}
