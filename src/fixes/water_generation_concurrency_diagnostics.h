#pragma once

#include "installed_fixes.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>

// BGSAutoWater::GeneratePlaceableWaterForCell shares the singleton's multiBounds array and default
// water object between callers, and is entered from both cell-loader tasks and the main thread.
// This only counts and logs overlapping entries; it does not change behavior.
namespace Fixes::WaterGenerationConcurrencyDiagnostics
{
    namespace detail
    {
        using GenerateForCell_t = void (*)(void*, void*, bool);

        inline constexpr std::uint8_t  kCallOpcode = 0xE8;
        inline constexpr std::size_t   kCallInstructionSize = 5;
        inline constexpr std::size_t   kMaxCallSites = 3;
        inline constexpr std::uint32_t kMaxLoggedOverlaps = 64;
        inline constexpr std::uint32_t kCallSummaryInterval = 100;

        struct Site
        {
            std::uintptr_t                            function;
            std::array<std::uintptr_t, kMaxCallSites> callSites;
        };

        inline constexpr Site kSiteSE{ 0x4BCA00, { 0x15BC66, 0x24B738, 0 } };
        inline constexpr Site kSiteVR{ 0x4CCB50, { 0x16C726, 0x25CE28, 0 } };
        inline constexpr Site kSiteAE1170{ 0x5193B0, { 0x1A5950, 0x29BBC9, 0x29CFF5 } };
        inline constexpr Site kSiteAE1104{ 0x5210D0, { 0x1AAD70, 0x2A1429, 0x2A2875 } };

        inline GenerateForCell_t          original = nullptr;
        inline util::SpinLock             serializeLock;
        inline bool                       serialize = false;
        inline std::atomic<std::int32_t>  active{ 0 };
        inline std::atomic<std::int32_t>  peak{ 0 };
        inline std::atomic<std::uint32_t> calls{ 0 };
        inline std::atomic<std::uint32_t> overlaps{ 0 };

        inline std::atomic<std::uint32_t> nullLoadedDataEntries{ 0 };
        inline std::atomic<std::uint32_t> loadedDataChanges{ 0 };

        inline void LogLoadedDataEvent(std::atomic<std::uint32_t>& a_counter, std::string_view a_what, const RE::TESObjectCELL* a_cell,
            const void* a_before, const void* a_after)
        {
            const auto count = a_counter.fetch_add(1, std::memory_order_relaxed) + 1;
            if (count <= kMaxLoggedOverlaps) {
                logger::warn("water generation: {} #{} (thread {}, cell {:p}, loadedData before {:p} after {:p})"sv,
                    a_what, count, ::GetCurrentThreadId(), static_cast<const void*>(a_cell), a_before, a_after);
            }
        }

        inline void Thunk(void* a_this, void* a_cell, bool a_skipFlowMap)
        {
            const auto inside = active.fetch_add(1, std::memory_order_acq_rel) + 1;
            const auto total = calls.fetch_add(1, std::memory_order_relaxed) + 1;

            if (total == 1 || total % kCallSummaryInterval == 0) {
                logger::info("water generation: {} call(s) so far, peak concurrency {}, {} overlap(s), {} null-loadedData entr(ies), {} loadedData change(s)"sv,
                    total, peak.load(std::memory_order_relaxed), overlaps.load(std::memory_order_relaxed),
                    nullLoadedDataEntries.load(std::memory_order_relaxed), loadedDataChanges.load(std::memory_order_relaxed));
            }

            if (inside > 1) {
                const auto overlapCount = overlaps.fetch_add(1, std::memory_order_relaxed) + 1;
                if (overlapCount <= kMaxLoggedOverlaps) {
                    logger::warn("water generation overlap #{}: {} threads inside GeneratePlaceableWaterForCell (this thread {}, cell {:p}, skipFlowMap {}, {} calls so far)"sv,
                        overlapCount, inside, ::GetCurrentThreadId(), a_cell, a_skipFlowMap, total);
                }
            }

            auto seenPeak = peak.load(std::memory_order_relaxed);
            while (inside > seenPeak && !peak.compare_exchange_weak(seenPeak, inside, std::memory_order_relaxed)) {
            }

            auto* const cell = static_cast<RE::TESObjectCELL*>(a_cell);
            const auto  dataBefore = cell ? cell->GetRuntimeData().loadedData : nullptr;
            if (cell && !dataBefore) {
                LogLoadedDataEvent(nullLoadedDataEntries, "entered with null loadedData"sv, cell, dataBefore, dataBefore);
            }

            if (serialize) {
                std::scoped_lock lock(serializeLock);
                original(a_this, a_cell, a_skipFlowMap);
            } else {
                original(a_this, a_cell, a_skipFlowMap);
            }

            const auto dataAfter = cell ? cell->GetRuntimeData().loadedData : nullptr;
            if (cell && dataBefore && dataAfter != dataBefore) {
                LogLoadedDataEvent(loadedDataChanges, "loadedData changed during generation"sv, cell, dataBefore, dataAfter);
            }

            active.fetch_sub(1, std::memory_order_acq_rel);
        }

        inline bool HookCallSite(std::uintptr_t a_callSiteRva, std::uintptr_t a_functionRva)
        {
            REL::Relocation<std::uintptr_t> callSite{ REL::Offset{ a_callSiteRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(callSite.address());

            std::int32_t rel32;
            std::memcpy(&rel32, bytes + 1, sizeof(rel32));
            const auto target = callSite.address() + kCallInstructionSize + rel32;
            if (bytes[0] != kCallOpcode || target != REL::Module::get().base() + a_functionRva) {
                logger::warn("water generation diagnostics: unexpected call site at +{:X}, skipping"sv, a_callSiteRva);
                return false;
            }

            original = reinterpret_cast<GenerateForCell_t>(callSite.write_call<5>(Thunk));
            return true;
        }
    }

    inline void Install()
    {
        const auto& site = REL::Module::IsVR() ? detail::kSiteVR :
                           REL::Module::IsAE() ? (util::IsAE1799() ? detail::kSiteAE1104 : detail::kSiteAE1170) :
                                                 detail::kSiteSE;

        detail::serialize = Settings::Fixes::bWaterGenerationSerialize.GetValue();

        std::size_t hooked = 0;
        for (const auto callSite : site.callSites) {
            if (callSite != 0 && detail::HookCallSite(callSite, site.function)) {
                ++hooked;
            }
        }

        if (hooked == 0) {
            logger::warn("water generation diagnostics: not installed"sv);
            return;
        }

        EF_INSTALLED("WaterGenerationConcurrencyDiagnostics"sv, "installed water generation concurrency diagnostics ({} call sites, serialize {})"sv, hooked, detail::serialize);
    }
}
