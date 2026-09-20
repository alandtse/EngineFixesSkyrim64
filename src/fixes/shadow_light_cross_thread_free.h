#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>

// A light released on a job thread (e.g. console coc -> SetCurrentCell -> ResetScene) can be freed
// while the render thread is still reading it through the scene node's unreferenced active-light list.
namespace Fixes::ShadowLightCrossThreadFree
{
    namespace detail
    {
        using DeletingDestructor_t = void* (*)(void*, std::uint64_t);

        struct DeferredFree
        {
            void*                light;
            std::uint64_t        flags;
            DeletingDestructor_t destroy;
        };

        inline constexpr std::size_t   kDeletingDestructorSlot = 0;
        inline constexpr std::uint64_t kDeleteFlag = 1;
        inline constexpr std::size_t   kMaxDeferredFrees = 256;
        inline constexpr std::uint8_t  kCallOpcode = 0xE8;
        inline constexpr std::size_t   kCallInstructionSize = 5;

        struct Site
        {
            std::uintptr_t frustumLightDeletingDestructor;
            std::uintptr_t parabolicLightDeletingDestructor;
            std::uintptr_t directionalLightDeletingDestructor;
            std::uintptr_t postRenderCleanupCall;
            std::uintptr_t postRenderCleanup;
        };

        inline constexpr Site kSiteVR{ 0x1370370, 0x13714B0, 0x135A520, 0x5B9BC9, 0x1323EF0 };
        inline constexpr Site kSiteSE{ 0x132D4F0, 0x132E600, 0x1327E70, 0x5B22CF, 0x12E4370 };
        inline constexpr Site kSiteAE1170{ 0x151AF80, 0x151BE50, 0x1515020, 0x644F28, 0x14CE140 };
        inline constexpr Site kSiteAE1104{ 0x1587EF0, 0x1588DC0, 0x1581E90, 0x6578F5, 0x153A5B0 };

        struct DeferredQueue
        {
            std::array<DeferredFree, kMaxDeferredFrees> entries;
            std::size_t                                 count = 0;
        };

        inline std::mutex                 deferredLock;
        inline DeferredQueue              deferredQueue;
        inline std::atomic_bool           hasDeferredFrees{ false };
        inline std::atomic_bool           capWarningLogged{ false };
        inline std::atomic<std::uint32_t> renderThreadId{ 0 };
        inline std::uintptr_t             originalPostRenderCleanup = 0;

        enum class DeferResult
        {
            Run,
            Deferred,
            QueueFull
        };

        // Nothing allocates under deferredLock: the queue is a fixed array.
        inline DeferResult DeferDestruction(void* a_light, std::uint64_t a_flags, DeletingDestructor_t a_destroy)
        {
            const auto renderThread = renderThreadId.load(std::memory_order_relaxed);
            if (!(a_flags & kDeleteFlag) || renderThread == 0) {
                return DeferResult::Run;
            }

            const bool onRenderThread = ::GetCurrentThreadId() == renderThread;

            std::scoped_lock lock(deferredLock);
            auto&            queue = deferredQueue;
            const auto       end = queue.entries.begin() + queue.count;
            const auto       queued = std::find_if(queue.entries.begin(), end,
                      [a_light](const DeferredFree& a_entry) { return a_entry.light == a_light; });
            if (queued != end) {
                if (!onRenderThread) {
                    return DeferResult::Deferred;
                }
                *queued = queue.entries[--queue.count];
                return DeferResult::Run;
            }

            if (onRenderThread) {
                return DeferResult::Run;
            }

            if (queue.count >= kMaxDeferredFrees) {
                return DeferResult::QueueFull;
            }

            queue.entries[queue.count++] = { a_light, a_flags, a_destroy };
            hasDeferredFrees.store(true, std::memory_order_release);
            return DeferResult::Deferred;
        }

        inline void DrainDeferredFrees()
        {
            if (!hasDeferredFrees.load(std::memory_order_acquire)) {
                return;
            }

            DeferredQueue ready;
            {
                std::scoped_lock lock(deferredLock);
                ready = deferredQueue;
                deferredQueue.count = 0;
                hasDeferredFrees.store(false, std::memory_order_release);
            }

            logger::info("shadow light cross-thread free fix: freeing {} deferred light(s) on the render thread"sv, ready.count);
            for (std::size_t i = 0; i < ready.count; ++i) {
                ready.entries[i].destroy(ready.entries[i].light, ready.entries[i].flags);
            }
        }

        inline void PostRenderCleanupHook()
        {
            renderThreadId.store(::GetCurrentThreadId(), std::memory_order_relaxed);
            reinterpret_cast<void (*)()>(originalPostRenderCleanup)();
            DrainDeferredFrees();
        }

        template <class LightTag>
        struct DeletingDestructorHook
        {
            static inline DeletingDestructor_t original = nullptr;

            static void* Thunk(void* a_light, std::uint64_t a_flags)
            {
                switch (DeferDestruction(a_light, a_flags, original)) {
                case DeferResult::Deferred:
                    return a_light;
                case DeferResult::QueueFull:
                    if (!capWarningLogged.exchange(true)) {
                        logger::warn("shadow light cross-thread free fix: deferred free queue full, freeing immediately"sv);
                    }
                    break;
                case DeferResult::Run:
                    break;
                }
                return original(a_light, a_flags);
            }
        };

        struct FrustumLight;
        struct ParabolicLight;
        struct DirectionalLight;

        template <class LightTag>
        inline bool HookDeletingDestructor(REL::Relocation<std::uintptr_t>& a_vtable, std::uintptr_t a_expectedRva)
        {
            const auto current = reinterpret_cast<const std::uintptr_t*>(a_vtable.address())[kDeletingDestructorSlot];
            if (current != REL::Module::get().base() + a_expectedRva) {
                logger::warn("shadow light cross-thread free fix: unexpected deleting destructor at vtable slot {}, skipping"sv, kDeletingDestructorSlot);
                return false;
            }

            DeletingDestructorHook<LightTag>::original = reinterpret_cast<DeletingDestructor_t>(current);
            a_vtable.write_vfunc(kDeletingDestructorSlot, DeletingDestructorHook<LightTag>::Thunk);
            return true;
        }

        inline bool HookPostRenderCleanupCall(const Site& a_site)
        {
            REL::Relocation<std::uintptr_t> callSite{ REL::Offset{ a_site.postRenderCleanupCall } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(callSite.address());

            std::int32_t rel32;
            std::memcpy(&rel32, bytes + 1, sizeof(rel32));
            const auto target = callSite.address() + kCallInstructionSize + rel32;
            if (bytes[0] != kCallOpcode || target != REL::Module::get().base() + a_site.postRenderCleanup) {
                logger::warn("shadow light cross-thread free fix: unexpected PostRenderCleanup call site, skipping"sv);
                return false;
            }

            originalPostRenderCleanup = callSite.write_call<5>(PostRenderCleanupHook);
            return true;
        }
    }

    inline void Install()
    {
        const auto& site = REL::Module::IsVR() ? detail::kSiteVR :
                           REL::Module::IsAE() ? (util::IsAE1799() ? detail::kSiteAE1104 : detail::kSiteAE1170) :
                                                 detail::kSiteSE;

        if (!detail::HookPostRenderCleanupCall(site)) {
            return;
        }

        REL::Relocation<std::uintptr_t> frustumVtable{ RE::VTABLE_BSShadowFrustumLight[0] };
        REL::Relocation<std::uintptr_t> parabolicVtable{ RE::VTABLE_BSShadowParabolicLight[0] };
        REL::Relocation<std::uintptr_t> directionalVtable{ RE::VTABLE_BSShadowDirectionalLight[0] };

        std::size_t hooked = 0;
        hooked += detail::HookDeletingDestructor<detail::FrustumLight>(frustumVtable, site.frustumLightDeletingDestructor);
        hooked += detail::HookDeletingDestructor<detail::ParabolicLight>(parabolicVtable, site.parabolicLightDeletingDestructor);
        hooked += detail::HookDeletingDestructor<detail::DirectionalLight>(directionalVtable, site.directionalLightDeletingDestructor);

        logger::info("installed shadow light cross-thread free fix ({} light classes)"sv, hooked);
    }
}
