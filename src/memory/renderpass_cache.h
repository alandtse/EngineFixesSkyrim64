#pragma once
#include "memory/allocator.h"

#include <array>
#include <mutex>

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
        inline constexpr std::uint32_t kRetiredTag = 0xD1ED0FF5u;  // cachePoolId sentinel: pass is quarantined

        struct RetiredPass
        {
            RE::BSRenderPass* pass;
            std::uint32_t     frame;
        };
        inline std::array<RetiredPass, kMaxQuarantined> s_ring;
        inline std::size_t                              s_head = 0;   // next write slot
        inline std::size_t                              s_count = 0;  // live entries
        inline std::mutex                               s_retireMutex;

        inline std::uint32_t CurrentFrame()
        {
            const auto* state = RE::BSGraphics::State::GetSingleton();
            return state ? state->frameCount : 0;
        }

        inline void FreeNow(RE::BSRenderPass* a_renderPass)
        {
            if (a_renderPass->sceneLights != nullptr)
                Allocator::GetAllocator()->DeallocateAligned(a_renderPass->sceneLights);
            Allocator::GetAllocator()->DeallocateAligned(a_renderPass);
        }

        // Free and drop the oldest quarantined pass. Caller holds s_retireMutex.
        inline void FreeOldest()
        {
            FreeNow(s_ring[(s_head + kMaxQuarantined - s_count) % kMaxQuarantined].pass);
            --s_count;
        }

        inline void SetLights(RE::BSRenderPass* a_renderPass, uint8_t a_numLights, RE::BSLight** a_lights)
        {
            if (a_numLights != a_renderPass->numLights) {
                if (a_renderPass->sceneLights) {
                    Allocator::GetAllocator()->DeallocateAligned(a_renderPass->sceneLights);
                    a_renderPass->sceneLights = nullptr;
                }
                if (a_numLights != 0) {
                    a_renderPass->sceneLights = static_cast<RE::BSLight**>(Allocator::GetAllocator()->AllocateAligned(sizeof(RE::BSLight*) * a_numLights, 8));
                }
                a_renderPass->numLights = a_numLights;
            }

            for (uint32_t i = 0; i < a_numLights; i++)
                a_renderPass->sceneLights[i] = a_lights[i];
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

            SetLights(renderPass, a_numLights, a_lights);

            return renderPass;
        }

        inline void Deallocate(RE::BSRenderPass* a_renderPass)
        {
            // Do NOT touch a_renderPass's payload here: a late/concurrent draw may
            // still dereference it. Park it intact; it is physically freed only once
            // kQuarantineFrames frames have elapsed. See the quarantine note above.
            const auto       now = CurrentFrame();
            std::scoped_lock lock(s_retireMutex);

            // Skip a double Deallocate of the same pass (would double-free on drain).
            // Allocate stamps cachePoolId 0xFEFEDEAD; we restamp it on retire below.
            if (a_renderPass->cachePoolId == kRetiredTag)
                return;

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
            }

            a_renderPass->cachePoolId = kRetiredTag;
            s_ring[s_head] = { a_renderPass, now };
            s_head = (s_head + 1) % kMaxQuarantined;
            ++s_count;
        }

        inline void Install()
        {
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
        logger::info("installed render pass cache patch"sv);
    }
}
