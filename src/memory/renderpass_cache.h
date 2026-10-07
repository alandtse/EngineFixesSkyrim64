#pragma once
#include "installed_fixes.h"
#include "memory/allocator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace Memory::RenderPassCache
{
    namespace detail
    {
        // Deferred-free quarantine for freed BSRenderPasses.
        //
        // EngineFixes replaces the engine's dedicated BSRenderPass pool with
        // general-allocator alloc/free. The engine's draw path (BSBatchRenderer
        // -> BeginPass -> a_shader->SetupTechnique) can read a BSRenderPass after
        // it was freed: a stale entry left in a pass bucket is iterated, and with
        // immediate free the slot has already been handed to an unrelated
        // allocation, so Pass->shader (offset 0) reads back a garbage vftable ->
        // execute-AV CTD. This is the unfixed root of the Community Shaders conflict
        // (CS issue #1601 -- the CS-side guards only validate Pass->sceneLights[],
        // never Pass->shader). CS background shader compilation removes the
        // render-thread compile stall that previously made the window rare, exposing
        // it on essentially every cell load.
        //
        // Freed passes are parked intact (including their sceneLights) in a FIFO
        // ring tagged with the engine frame (BSGraphics::State::frameCount) and
        // physically freed only once kQuarantineFrames frames have elapsed -- past
        // any in-flight draw that could still hold a stale reference, so a stale
        // read still sees the original valid shader/lights. The age test is an
        // unsigned subtraction (wrap-safe) and does not assume the counter advances
        // by one per call, so a frozen counter (loading screen, pause) holds passes
        // longer rather than freeing them early. kMaxQuarantined bounds memory: if
        // the ring fills (extreme churn or a long frozen-counter span) the oldest
        // pass is force-freed. Allocation-free (fixed ring) so the render hot path
        // adds no heap traffic; restores the safety of the engine's original pool
        // (freed memory stays pass-shaped) while keeping EF's dynamic growth.
        inline constexpr std::uint32_t kQuarantineFrames = 3;
        inline constexpr std::size_t   kMaxQuarantined = 16384;
        inline constexpr std::uint32_t kRetiredTag = 0xD1ED0FF5u;  // pad44 sentinel: pass is quarantined

        struct RetiredPass
        {
            RE::BSRenderPass* pass;
            std::uint32_t     frame;
        };
        inline std::array<RetiredPass, kMaxQuarantined> s_ring;
        inline std::size_t                              s_head = 0;   // next write slot
        inline std::size_t                              s_count = 0;  // live entries
        inline util::SpinLock                           s_retireLock;

        inline std::uint32_t CurrentFrame()
        {
            const auto* state = RE::BSGraphics::State::GetSingleton();
            return state ? state->frameCount : 0;
        }

        // Keep formatting and logging out of the replaced free and the draw hook: the engine can enter them
        // with an unaligned stack, and an SSE spill there faults.
        namespace Diagnostics
        {
            inline constexpr std::size_t kAgeBins = 8;
            inline constexpr std::size_t kLastAgeBin = kAgeBins - 1;
            inline constexpr std::size_t kLoggedHits = 16;
            inline constexpr DWORD       kReportMs = 60000;

            struct HitRecord
            {
                const void*   pass;
                const void*   shader;
                std::uint32_t age;
                bool          freed;
            };

            inline bool                                             s_enabled = false;
            inline std::atomic<std::uint64_t>                       s_draws{ 0 };
            inline std::array<std::atomic<std::uint64_t>, kAgeBins> s_retiredHits{};
            inline std::atomic<std::uint64_t>                       s_postFreeHits{ 0 };
            inline std::atomic<std::uint64_t>                       s_valveFrees{ 0 };
            inline std::atomic<std::size_t>                         s_peakQuarantined{ 0 };
            inline std::array<HitRecord, kLoggedHits>               s_lockedHits{};
            inline std::size_t                                      s_lockedHitsWritten = 0;
            inline std::size_t                                      s_lockedHitsLogged = 0;

            inline void RecordRetiredHitLocked(RE::BSRenderPass* a_renderPass, std::uint32_t a_now)
            {
                bool          freed = true;
                std::uint32_t age = 0;
                for (std::size_t i = 0; i < s_count; ++i) {
                    const auto& entry = s_ring[(s_head + kMaxQuarantined - s_count + i) % kMaxQuarantined];
                    if (entry.pass == a_renderPass) {
                        age = a_now - entry.frame;
                        freed = false;
                        break;
                    }
                }

                if (freed)
                    s_postFreeHits.fetch_add(1, std::memory_order_relaxed);
                else
                    s_retiredHits[(std::min)(static_cast<std::size_t>(age), kLastAgeBin)].fetch_add(1, std::memory_order_relaxed);

                if (s_lockedHitsWritten < kLoggedHits)
                    s_lockedHits[s_lockedHitsWritten++] = { a_renderPass, a_renderPass->shader, age, freed };
            }

            inline void OnPassDraw(RE::BSRenderPass* a_renderPass)
            {
                if (!s_enabled)
                    return;

                s_draws.fetch_add(1, std::memory_order_relaxed);
                if (!a_renderPass || a_renderPass->pad44 != kRetiredTag)
                    return;

                std::scoped_lock lock(s_retireLock);
                RecordRetiredHitLocked(a_renderPass, CurrentFrame());
            }

            inline void Report()
            {
                std::array<HitRecord, kLoggedHits> pending;
                std::size_t                        pendingCount = 0;
                {
                    std::scoped_lock lock(s_retireLock);
                    while (s_lockedHitsLogged < s_lockedHitsWritten)
                        pending[pendingCount++] = s_lockedHits[s_lockedHitsLogged++];
                }

                for (std::size_t i = 0; i < pendingCount; ++i) {
                    const auto& hit = pending[i];
                    if (hit.freed)
                        logger::warn("draw reached retired render pass {:p} (shader {:p}) after it was freed"sv, hit.pass, hit.shader);
                    else
                        logger::warn("draw reached retired render pass {:p} (shader {:p}), {} frames old"sv, hit.pass, hit.shader, hit.age);
                }

                std::string ages;
                for (std::size_t i = 0; i < kAgeBins; ++i)
                    ages += std::format("{}{}", i ? "," : "", s_retiredHits[i].load(std::memory_order_relaxed));
                logger::info("render pass quarantine: {} draws, retired hits by age in frames [{}], already freed {}, peak held {}/{}, valve frees {}"sv,
                    s_draws.load(std::memory_order_relaxed), ages, s_postFreeHits.load(std::memory_order_relaxed),
                    s_peakQuarantined.load(std::memory_order_relaxed), kMaxQuarantined, s_valveFrees.load(std::memory_order_relaxed));
            }

            inline void Start()
            {
                s_enabled = true;
                std::thread([] {
                    for (;;) {
                        ::Sleep(kReportMs);
                        Report();
                    }
                }).detach();
            }
        }

        inline void FreeNow(RE::BSRenderPass* a_renderPass)
        {
            if (a_renderPass->sceneLights != nullptr)
                Allocator::GetAllocator()->DeallocateAligned(a_renderPass->sceneLights);
            Allocator::GetAllocator()->DeallocateAligned(a_renderPass);
        }

        // Free and drop the oldest quarantined pass. Caller holds s_retireLock.
        inline void FreeOldest()
        {
            FreeNow(s_ring[(s_head + kMaxQuarantined - s_count) % kMaxQuarantined].pass);
            --s_count;
        }

        // The engine gives every BSRenderPass a FIXED 16-entry sceneLights array.
        // BSRenderPassCache::Init carves the pool's light block into 0x80-byte slices
        // (0x80 / 8 == 16 pointers), one per pass, and stores the slice pointer at
        // BSRenderPass+0x38; the array is never resized and never individually freed.
        // The engine's own SetLights writes numLights entries and then explicitly
        // zero-fills the remaining slots out to 16 (the cmp dl, 0x10 / jae tail at
        // SetLights+0x37). Engine code therefore relies on all 16 slots existing and
        // being initialised, and reads past numLights: shadow lights live in the same
        // array, counted by numShadowLights at +0x20.
        //
        // Sizing this array to numLights, as this function previously did, turns every
        // such read into a heap overread past the end of a small allocation, handing
        // the engine a garbage BSLight* that it then refcounts -- a corruption path
        // with no relationship to when anything is freed. Community Shaders hardened
        // its own sceneLights loops against exactly this (runtime pointer validation
        // and an SEH guard), which is independent corroboration that the overread is
        // encountered in practice rather than being theoretical.
        //
        // Storage is therefore a fixed kSceneLightsMax slots, allocated once and never
        // resized, with every slot zeroed: a consumer indexing past numLights reads a
        // null BSLight* out of memory we own rather than a live pointer off the end of
        // a smaller allocation. kSceneLightsMax exceeds the engine's 16 so that a mod
        // which widens the native slice is also served without a second binary; the
        // copy is clamped to it, so a caller's count can never run off the end.
        inline constexpr std::size_t kSceneLightsMax = 64;

        inline void SetLights(RE::BSRenderPass* a_renderPass, uint8_t a_numLights, RE::BSLight** a_lights)
        {
            if (a_renderPass->sceneLights == nullptr) {
                a_renderPass->sceneLights = static_cast<RE::BSLight**>(
                    Allocator::GetAllocator()->AllocateAligned(sizeof(RE::BSLight*) * kSceneLightsMax, 8));
                std::memset(a_renderPass->sceneLights, 0, sizeof(RE::BSLight*) * kSceneLightsMax);
            }

            const auto copy = (std::min)(static_cast<std::size_t>(a_numLights), kSceneLightsMax);
            for (std::size_t i = 0; i < copy; ++i)
                a_renderPass->sceneLights[i] = a_lights[i];

            // The array is reused when a pass is refilled, so a shorter refill must not
            // leave live pointers from the previous use readable past the new count.
            std::memset(a_renderPass->sceneLights + copy, 0, sizeof(RE::BSLight*) * (kSceneLightsMax - copy));

            if (copy != a_numLights) {
                // Only reachable if a caller asks for more than kSceneLightsMax, which no
                // known configuration produces. Shadow lights occupy the tail of the
                // numLights range, so once the total is truncated the old shadow boundary
                // no longer describes the copied layout: fail closed rather than leave a
                // falsely indexed shadow segment.
                a_renderPass->numShadowLights = 0;
                logger::error(
                    "render pass requested {} scene lights, more than the {} slots allocated; "
                    "truncating the count and clearing shadow lights"sv,
                    a_numLights, kSceneLightsMax);
            }

            // The published count and the storage must describe the same safe range.
            a_renderPass->numLights = static_cast<std::uint8_t>(copy);
        }

        inline void Set(RE::BSRenderPass* a_renderPass, RE::BSShader* a_shader, RE::BSShaderProperty* a_property, RE::BSGeometry* a_geometry, uint32_t a_passEnum, uint8_t a_numLights, RE::BSLight** a_lights)
        {
            a_renderPass->shader = a_shader;
            a_renderPass->shaderProperty = a_property;
            a_renderPass->geometry = a_geometry;
            a_renderPass->passEnum = a_passEnum;
            a_renderPass->accumulationHint = 0;
            SetLights(a_renderPass, a_numLights, a_lights);
        }

        inline RE::BSRenderPass* Allocate(RE::BSShader* a_shader, RE::BSShaderProperty* a_property, RE::BSGeometry* a_geometry, uint32_t a_passEnum, uint8_t a_numLights, RE::BSLight** a_lights)
        {
            constexpr std::size_t size = sizeof(RE::BSRenderPass);
            auto*                 data = Allocator::GetAllocator()->AllocateAligned(size, 8);
            memset(data, 0, size);

            auto* renderPass = static_cast<RE::BSRenderPass*>(data);

            renderPass->shader = a_shader;
            renderPass->shaderProperty = a_property;
            renderPass->geometry = a_geometry;
            renderPass->passEnum = a_passEnum;
            renderPass->accumulationHint = 0;
            renderPass->extraParam = 0;
            renderPass->LODMode.index = 3;
            renderPass->LODMode.singleLevel = false;
            renderPass->numShadowLights = 0;
            renderPass->next = nullptr;
            renderPass->passGroupNext = nullptr;
            renderPass->cachePoolId = 0xFEFEDEAD;
            renderPass->pad44 = 0;

            SetLights(renderPass, a_numLights, a_lights);

            return renderPass;
        }

        inline void Deallocate(RE::BSRenderPass* a_renderPass)
        {
            // Do NOT touch a_renderPass's payload here: a late/concurrent draw may
            // still dereference it. Park it intact; it is physically freed only once
            // kQuarantineFrames frames have elapsed. See the quarantine note above.
            std::scoped_lock lock(s_retireLock);

            // Skip a double Deallocate of the same pass (would double-free on drain).
            // Allocate stamps pad44 as 0; we restamp it on retire below.
            if (a_renderPass->pad44 == kRetiredTag)
                return;

            // Sample the frame under the lock: a pre-lock read could be backdated by
            // contention, under-quarantining this pass (drained before kQuarantineFrames).
            const auto now = CurrentFrame();

            // Drain everything old enough to be past any in-flight reference.
            while (s_count > 0) {
                const auto& oldest = s_ring[(s_head + kMaxQuarantined - s_count) % kMaxQuarantined];
                if (now - oldest.frame < kQuarantineFrames)
                    break;
                FreeOldest();
            }

            // Safety valve: never exceed the ring (extreme churn / frozen counter).
            if (s_count == kMaxQuarantined) {
                static bool warned = false;
                if (!warned) {
                    warned = true;
                    logger::warn("render pass quarantine full ({}); force-freeing oldest"sv, kMaxQuarantined);
                }
                FreeOldest();
                Diagnostics::s_valveFrees.fetch_add(1, std::memory_order_relaxed);
            }

            a_renderPass->pad44 = kRetiredTag;
            s_ring[s_head] = { a_renderPass, now };
            s_head = (s_head + 1) % kMaxQuarantined;
            ++s_count;

            if (Diagnostics::s_enabled && s_count > Diagnostics::s_peakQuarantined.load(std::memory_order_relaxed))
                Diagnostics::s_peakQuarantined.store(s_count, std::memory_order_relaxed);
        }

        inline void Install()
        {
            if (Settings::Debug::bRenderPassQuarantineDiagnostics.GetValue()) {
                Diagnostics::Start();
                logger::info("render pass quarantine diagnostics enabled; summary every {}s"sv, Diagnostics::kReportMs / 1000);
            }
            if (Diagnostics::s_enabled && !Settings::Fixes::bBSLightingShaderForceAlphaTest.GetValue())
                logger::warn("bRenderPassQuarantineDiagnostics counts draws from the bBSLightingShaderForceAlphaTest hook, which is disabled; no draws will be checked"sv);

            REL::Relocation allocate{ RELOCATION_ID(100717, 107497) };
            REL::Relocation deallocate{ RELOCATION_ID(100718, 107498) };
            REL::Relocation setlights{ RELOCATION_ID(100711, 107490) };
            REL::Relocation init{ RELOCATION_ID(100720, 107500) };
            REL::Relocation kill{ RELOCATION_ID(100721, 107501) };
            allocate.replace_func(VAR_NUM(0x9A, 0xF9), Allocate);
            deallocate.replace_func(VAR_NUM(0x60, 0x68), Deallocate);
            setlights.replace_func(0x69, SetLights);
            if (!REL::Module::IsAE()) {
                REL::Relocation set{ REL::ID(100710) };
                set.replace_func(0x90, Set);
            }

            init.write_fill(REL::INT3, VAR_NUM(0x1BD, 0x1BF));
            init.write(REL::RET);
            kill.write_fill(REL::INT3, 0xAB);
            kill.write(REL::RET);
            REL::Relocation clear{ RELOCATION_ID(100722, 107502) };
            clear.write_fill(REL::INT3, VAR_NUM(0xE7, 0x16B, 0xB7));
            clear.write(REL::RET);
        }
    }

    inline void Install()
    {
        detail::Install();
        EF_INSTALLED(InstalledFixes::Public::kRenderPassCacheSceneLights64, "installed render pass cache patch"sv);
    }
}
