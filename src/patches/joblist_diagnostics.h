#pragma once

#include "installed_fixes.h"

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>

#include <intrin.h>

namespace Patches::JobListDiagnostics
{
    namespace detail
    {
        enum class Op : std::uint8_t
        {
            Submit,
            Finish,
            Clear,
            UpdateNonHighActors,
            JobTesAnimation
        };

        enum class Phase : std::uint8_t
        {
            Enter,
            Exit,
            Overlap
        };

        constexpr std::array OP_NAMES{ "Submit"sv, "Finish"sv, "Clear"sv, "UpdateNonHighActors"sv, "Job_TES_animation"sv };
        constexpr std::array PHASE_NAMES{ "enter"sv, "exit"sv, "OVERLAP"sv };

        // list+0x98 is the pending-job count Finish_impl waits on; list+0x28 is the field Clear zeroes
        constexpr std::ptrdiff_t PENDING_OFFSET = 0x98;
        constexpr std::ptrdiff_t SIZE_OFFSET = 0x28;
        constexpr std::uint32_t  LIST_INDEX = 7;
        constexpr std::uintptr_t UMBRELLA_KEY = 1;

        constexpr std::size_t    RING_SIZE = 1u << 16;
        constexpr std::size_t    SLOT_COUNT = 64;
        constexpr std::size_t    CRASH_TAIL = 4096;
        constexpr auto           FLUSH_INTERVAL = std::chrono::milliseconds(200);
        constexpr std::uintptr_t FINISH_IMPL_SPAN = 0x300;
        constexpr std::uintptr_t CLEAR_SPAN = 0x100;

        struct Event
        {
            std::int64_t   ns{ 0 };
            std::uintptr_t list{ 0 };
            std::int64_t   pending{ 0 };
            std::uint32_t  tid{ 0 };
            std::uint32_t  caller{ 0 };
            std::uint32_t  otherTid{ 0 };
            std::int32_t   size{ 0 };
            Op             op{};
            Phase          phase{};
            Op             otherOp{};
        };

        struct Slot
        {
            std::atomic<std::uint32_t>  tid{ 0 };
            std::atomic<std::uintptr_t> list{ 0 };
            std::atomic<std::uint8_t>   op{ 0 };
        };

        inline std::array<Event, RING_SIZE> ring;
        inline std::atomic<std::uint64_t>   head{ 0 };
        inline std::uint64_t                flushed{ 0 };
        inline std::array<Slot, SLOT_COUNT> slots;
        inline std::atomic<std::uintptr_t>  list7{ 0 };
        inline std::mutex                   fileMutex;
        inline std::filesystem::path        tracePath;
        inline std::uintptr_t               finishImpl{ 0 };
        inline std::uintptr_t               clearFn{ 0 };
        inline std::uintptr_t               moduleBase{ 0 };
        inline std::atomic<bool>            crashDumped{ false };

        inline SafetyHookInline origSubmit;
        inline SafetyHookInline origFinish;
        inline SafetyHookInline origClear;
        inline SafetyHookInline origUpdateNonHighActors;
        inline SafetyHookInline origJobTesAnimation;

        inline std::uintptr_t Resolve(const std::uint32_t a_se, const std::uint32_t a_ae, const std::uintptr_t a_vrOffset)
        {
            if (REL::Module::IsVR())
                return REL::Relocation<std::uintptr_t>{ REL::Offset{ a_vrOffset } }.address();
            return REL::Relocation<std::uintptr_t>{ REL::ID{ REL::Module::IsAE() ? a_ae : a_se } }.address();
        }

        inline std::int64_t NowNs()
        {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        inline void Record(const Event& a_event)
        {
            ring[head.fetch_add(1, std::memory_order_relaxed) & (RING_SIZE - 1)] = a_event;
        }

        inline void ReadList(const std::uintptr_t a_list, Event& a_event)
        {
            if (a_list > UMBRELLA_KEY) {
                a_event.pending = *reinterpret_cast<const std::int64_t*>(a_list + PENDING_OFFSET);
                a_event.size = *reinterpret_cast<const std::int32_t*>(a_list + SIZE_OFFSET);
            }
        }

        inline void RefreshList7()
        {
            static const auto getByIndex = Resolve(34549, 35404, 0x57C960);
            const auto        wrapper = reinterpret_cast<void** (*)(std::uint32_t)>(getByIndex)(LIST_INDEX);
            list7.store(wrapper ? reinterpret_cast<std::uintptr_t>(*wrapper) : 0, std::memory_order_relaxed);
        }

        inline bool Tracked(const std::uintptr_t a_list)
        {
            return a_list == UMBRELLA_KEY || (a_list != 0 && a_list == list7.load(std::memory_order_relaxed));
        }

        inline Slot* Enter(const Op a_op, const std::uintptr_t a_list, const std::uintptr_t a_caller)
        {
            const auto tid = ::GetCurrentThreadId();
            Slot*      mine = nullptr;
            for (auto& slot : slots) {
                std::uint32_t expected = 0;
                if (slot.tid.compare_exchange_strong(expected, tid)) {
                    slot.list.store(a_list);
                    slot.op.store(static_cast<std::uint8_t>(a_op));
                    mine = &slot;
                    break;
                }
            }

            Event event{ .ns = NowNs(), .list = a_list, .tid = tid, .caller = static_cast<std::uint32_t>(a_caller - moduleBase), .op = a_op, .phase = Phase::Enter };
            ReadList(a_list, event);
            Record(event);

            for (auto& slot : slots) {
                const auto otherTid = slot.tid.load();
                if (otherTid != 0 && otherTid != tid && slot.list.load() == a_list) {
                    Event overlap = event;
                    overlap.phase = Phase::Overlap;
                    overlap.otherTid = otherTid;
                    overlap.otherOp = static_cast<Op>(slot.op.load());
                    Record(overlap);
                }
            }
            return mine;
        }

        inline void Exit(Slot* a_slot, const Op a_op, const std::uintptr_t a_list)
        {
            Event event{ .ns = NowNs(), .list = a_list, .tid = ::GetCurrentThreadId(), .op = a_op, .phase = Phase::Exit };
            ReadList(a_list, event);
            Record(event);
            if (a_slot) {
                a_slot->list.store(0);
                a_slot->tid.store(0);
            }
        }

        inline std::string Format(const Event& a_event)
        {
            std::string line = std::format("{:.6f} tid={} {} {} list={:#x} caller=+{:#x} pending={} size={}",
                static_cast<double>(a_event.ns) / 1e9, a_event.tid, OP_NAMES[std::to_underlying(a_event.op)],
                PHASE_NAMES[std::to_underlying(a_event.phase)], a_event.list, a_event.caller, a_event.pending, a_event.size);
            if (a_event.phase == Phase::Overlap)
                line += std::format(" WITH tid={} {}", a_event.otherTid, OP_NAMES[std::to_underlying(a_event.otherOp)]);
            return line;
        }

        inline void WriteRange(std::ofstream& a_out, const std::uint64_t a_from, const std::uint64_t a_to)
        {
            for (auto i = a_from; i < a_to; ++i)
                a_out << Format(ring[i & (RING_SIZE - 1)]) << '\n';
        }

        inline void Flush()
        {
            const auto end = head.load(std::memory_order_relaxed);
            if (end == flushed)
                return;
            std::scoped_lock lock{ fileMutex };
            std::ofstream    out{ tracePath, std::ios::app };
            const auto       from = end - flushed > RING_SIZE ? end - RING_SIZE : flushed;
            WriteRange(out, from, end);
            flushed = end;
        }

        inline LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* a_info)
        {
            const auto record = a_info->ExceptionRecord;
            if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
                return EXCEPTION_CONTINUE_SEARCH;
            const auto rip = reinterpret_cast<std::uintptr_t>(record->ExceptionAddress);
            const bool inFinish = rip >= finishImpl && rip < finishImpl + FINISH_IMPL_SPAN;
            const bool inClear = rip >= clearFn && rip < clearFn + CLEAR_SPAN;
            if ((!inFinish && !inClear) || crashDumped.exchange(true))
                return EXCEPTION_CONTINUE_SEARCH;

            const auto       end = head.load();
            const auto       from = end > CRASH_TAIL ? end - CRASH_TAIL : 0;
            std::unique_lock lock{ fileMutex, std::try_to_lock };
            std::ofstream    out{ tracePath, std::ios::app };
            out << std::format("CRASH tid={} rip=+{:#x} in {} fault address {:#x}\n", ::GetCurrentThreadId(), rip - moduleBase,
                inFinish ? "Finish_impl" : "Clear", record->ExceptionInformation[1]);
            WriteRange(out, from, end);
            out.flush();
            return EXCEPTION_CONTINUE_SEARCH;
        }

        inline void Submit(void* a_this, void* a_arg2, std::uint64_t a_arg3)
        {
            const auto inner = a_this ? reinterpret_cast<std::uintptr_t>(*static_cast<void**>(a_this)) : 0;
            if (!Tracked(inner)) {
                origSubmit.call<void>(a_this, a_arg2, a_arg3);
                return;
            }
            auto* slot = Enter(Op::Submit, inner, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
            origSubmit.call<void>(a_this, a_arg2, a_arg3);
            Exit(slot, Op::Submit, inner);
        }

        inline void Finish(void* a_list)
        {
            const auto list = reinterpret_cast<std::uintptr_t>(a_list);
            if (!Tracked(list)) {
                origFinish.call<void>(a_list);
                return;
            }
            auto* slot = Enter(Op::Finish, list, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
            origFinish.call<void>(a_list);
            Exit(slot, Op::Finish, list);
        }

        inline void Clear(void* a_list)
        {
            const auto list = reinterpret_cast<std::uintptr_t>(a_list);
            if (!Tracked(list)) {
                origClear.call<void>(a_list);
                return;
            }
            auto* slot = Enter(Op::Clear, list, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
            origClear.call<void>(a_list);
            Exit(slot, Op::Clear, list);
        }

        inline void UpdateNonHighActors(void* a_this, float a_elapsedSeconds, std::uint8_t a_unk2, bool a_flags)
        {
            RefreshList7();
            auto* slot = Enter(Op::UpdateNonHighActors, UMBRELLA_KEY, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
            origUpdateNonHighActors.call<void>(a_this, a_elapsedSeconds, a_unk2, a_flags);
            Exit(slot, Op::UpdateNonHighActors, UMBRELLA_KEY);
        }

        inline void JobTesAnimation()
        {
            RefreshList7();
            auto* slot = Enter(Op::JobTesAnimation, UMBRELLA_KEY, reinterpret_cast<std::uintptr_t>(_ReturnAddress()));
            origJobTesAnimation.call<void>();
            Exit(slot, Op::JobTesAnimation, UMBRELLA_KEY);
        }

        inline bool Hook(SafetyHookInline& a_orig, const std::uintptr_t a_address, void* a_hook, const std::string_view a_name)
        {
            a_orig = safetyhook::create_inline(a_address, a_hook);
            if (!a_orig) {
                logger::warn("job list diagnostics: failed to hook {} at {:#x}"sv, a_name, a_address);
                return false;
            }
            return true;
        }
    }

    inline void Install()
    {
        using namespace detail;

        moduleBase = REL::Module::get().base();
        finishImpl = Resolve(68009, 69377, 0xC76DA0);
        clearFn = Resolve(68054, 69404, 0xC797E0);

        bool ok = true;
        ok &= Hook(origSubmit, Resolve(67991, 69352, 0xC76D60), reinterpret_cast<void*>(&Submit), "Submit");
        ok &= Hook(origFinish, finishImpl, reinterpret_cast<void*>(&Finish), "Finish_impl");
        ok &= Hook(origClear, clearFn, reinterpret_cast<void*>(&Clear), "Clear");
        ok &= Hook(origUpdateNonHighActors, Resolve(40291, 41301, 0x6FB570), reinterpret_cast<void*>(&UpdateNonHighActors), "UpdateNonHighActors");
        ok &= Hook(origJobTesAnimation, Resolve(38090, 39045, 0x649060), reinterpret_cast<void*>(&JobTesAnimation), "Job_TES_animation");
        if (!ok)
            return;

        auto path = SKSE::log::log_directory();
        if (!path)
            return;
        tracePath = *path / "EngineFixes_joblist_trace.log";
        std::filesystem::remove(tracePath);

        ::AddVectoredExceptionHandler(1, CrashHandler);
        std::thread([] {
            for (;;) {
                std::this_thread::sleep_for(FLUSH_INTERVAL);
                Flush();
            }
        }).detach();

        EF_INSTALLED("JobListDiagnostics"sv, "installed job list diagnostics, trace in EngineFixes_joblist_trace.log"sv);
    }
}
