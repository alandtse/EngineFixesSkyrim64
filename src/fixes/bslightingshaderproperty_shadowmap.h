#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace BSLightingShaderPropertyShadowMap
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(const std::uintptr_t a_target, const std::uintptr_t a_function)
            {
                mov(rcx, rsi);                       // BSLightingShaderProperty*
                mov(rdx, qword[rsp + 0xE8 + 0x10]);  // BSGeometry*
                mov(r8, a_function);
                push(rbx);
                sub(rsp, 0x20);
                call(r8);
                add(rsp, 0x20);
                pop(rbx);
                mov(r15, rax);
                jmp(ptr[rip]);
                dq(a_target + VAR_NUM(0x7EB, 0x802));
            }
        };

        inline constexpr std::uint32_t kShadowPassCount = 4;

        // AccumulateShadowMap carries a job index and can overlap with other
        // shadow work. A process-global slot lets one light redirect another
        // light's GetRenderPasses call. Keep the active descriptor index local
        // to the calling thread and restore it around nested accumulation.
        inline thread_local std::uint32_t g_currentIndex = 0;
        inline std::atomic_bool           g_loggedInvalidIndex = false;

        // head is a real BSRenderPass* the engine's destructor unconditionally
        // clears -- keep our 4 arrays in this side table instead, keyed by pointer.
        // Each slot is a genuine RenderPassArray (not a bare BSRenderPass*), so the
        // detour's return value has the exact struct shape callers expect, and
        // Clear()/EmplacePass() give us vanilla's own chain-walk semantics instead
        // of a hand-rolled single-pointer replace.
        inline std::mutex                                                                                                             g_utilityPassesMutex;
        inline std::unordered_map<RE::BSLightingShaderProperty*, std::array<RE::BSShaderProperty::RenderPassArray, kShadowPassCount>> g_utilityPasses;

        // Retired arrays stay alive long enough for any job-thread reader that
        // already fetched the pointer to finish.
        inline constexpr std::size_t   kQuarantineCapacity = 4096;  // Property destruction can spike into the thousands during a mass cell unload.
        inline constexpr std::uint32_t kQuarantineFrames = 3;       // Three frames is past any in-flight draw job.
        using PassesNode = decltype(g_utilityPasses)::node_type;

        struct QuarantinedProperty
        {
            PassesNode    node;
            std::uint32_t frame;
        };

        inline std::array<QuarantinedProperty, kQuarantineCapacity> s_quarantineRing;
        inline std::size_t                                          s_quarantineHead = 0;
        inline std::size_t                                          s_quarantineCount = 0;
        inline std::atomic_bool                                     g_loggedQuarantineForceFree = false;

        inline std::uint32_t CurrentFrame()
        {
            return RE::BSGraphics::State::GetSingleton()->frameCount;
        }

        inline void DrainQuarantine(std::uint32_t a_minAge = kQuarantineFrames)
        {
            const auto frame = CurrentFrame();
            while (s_quarantineCount != 0) {
                const auto age = frame - s_quarantineRing[s_quarantineHead].frame;
                if (age < a_minAge)
                    break;

                s_quarantineRing[s_quarantineHead] = {};
                s_quarantineHead = (s_quarantineHead + 1) % kQuarantineCapacity;
                --s_quarantineCount;
            }
        }

        inline void ForceFreeOldestQuarantineEntry()
        {
            if (s_quarantineCount == 0)
                return;

            s_quarantineRing[s_quarantineHead] = {};
            s_quarantineHead = (s_quarantineHead + 1) % kQuarantineCapacity;
            --s_quarantineCount;
        }

        inline void RetireAllocatedArrays(RE::BSLightingShaderProperty* a_self)
        {
            std::scoped_lock lock(g_utilityPassesMutex);

            const auto it = g_utilityPasses.find(a_self);
            if (it == g_utilityPasses.end())
                return;

            for (auto& passArray : it->second) {
                passArray.Clear();
            }

            auto node = g_utilityPasses.extract(it);
            DrainQuarantine();

            if (s_quarantineCount == kQuarantineCapacity) {
                DrainQuarantine(1);
                if (s_quarantineCount == kQuarantineCapacity) {
                    if (!g_loggedQuarantineForceFree.exchange(true, std::memory_order_relaxed)) {
                        logger::warn("bslightingshaderproperty shadowmap quarantine overflow; force-freeing oldest entry (capacity {})"sv,
                            kQuarantineCapacity);
                    }
                    ForceFreeOldestQuarantineEntry();
                }
            }

            const auto tail = (s_quarantineHead + s_quarantineCount) % kQuarantineCapacity;
            s_quarantineRing[tail].node = std::move(node);
            s_quarantineRing[tail].frame = CurrentFrame();
            ++s_quarantineCount;
        }

        inline std::uint32_t GetShadowmapIndex(const void* a_data)
        {
            if (a_data == nullptr)
                return 0;

            const auto* bytes = static_cast<const std::byte*>(a_data);
            const auto  offset = REL::Module::IsVR() ? offsetof(RE::BSShadowLight::ShadowmapDescriptorVR, shadowmapIndex) :
                                                       offsetof(RE::BSShadowLight::ShadowmapDescriptor, shadowmapIndex);
            return *reinterpret_cast<const std::uint32_t*>(bytes + offset);
        }

        inline RE::BSShaderProperty::RenderPassArray* BSLightingShaderProperty_GetRenderPasses_ShadowMapOrMask_Detour(RE::BSLightingShaderProperty* a_property, RE::BSGeometry* a_geometry)
        {
            // Defence in depth: this index is used for direct heap addressing.
            // Never permit a malformed/stale VR descriptor to write beyond the
            // four-array allocation below.
            const auto index = g_currentIndex < kShadowPassCount ? g_currentIndex : 0;

            std::scoped_lock lock(g_utilityPassesMutex);
            DrainQuarantine();

            auto& passArray = g_utilityPasses[a_property][index];
            // free last frame's render pass(es); mirrors vanilla's own Clear()
            passArray.Clear();

            // create new one
            std::uint32_t technique = a_property->DetermineUtilityShaderDecl() | 0xC000;
            const auto*   alphaProperty = reinterpret_cast<RE::NiAlphaProperty*>(a_geometry->GetGeometryRuntimeData().alphaProperty.get());
            if (alphaProperty && (alphaProperty->alphaFlags & 0x200) != 0) {
                technique |= 0x80;
            }
            if (a_property->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kLODObjects) || a_property->flags.all(RE::BSShaderProperty::EShaderPropertyFlag::kHDLODObjects))
                technique |= 0x8000000;

            RE::BSRenderPass* pass = passArray.EmplacePass(RE::BSUtilityShader::GetSingleton(), a_property, a_geometry, technique + 0x2B);
            pass->accumulationHint = 8;
            if ((a_geometry->GetFlags().underlying() & 0x8000000) != 0 && a_property->fadeNode != nullptr) {
                pass->LODMode.index = a_property->fadeNode->GetRuntimeData().unk152 & 0xF;
            } else {
                pass->LODMode.index = 3;
            }
            pass->LODMode.singleLevel = false;
            return &passArray;
        }

        inline SafetyHookInline orig_BSShadowLight_AccumulateShadowMap;

        inline void BSShadowLight_AccumulateShadowMap(RE::BSShadowLight* a_self, void* a_data, std::uint32_t* a_pShadowMaskChannel, RE::BSTArray<RE::BSCullingProcess*>* a_cullingProcessArray, const std::uint32_t a_jobIndex)
        {
            // VR uses a different shadow descriptor layout, so read the index by runtime offset.
            auto index = GetShadowmapIndex(a_data);
            if (index >= kShadowPassCount) {
                if (!g_loggedInvalidIndex.exchange(true, std::memory_order_relaxed)) {
                    logger::error("shadow map descriptor index {} exceeds {} slots; using slot 0 to prevent heap corruption"sv,
                        index,
                        kShadowPassCount);
                }
                index = 0;
            }

            const auto previousIndex = g_currentIndex;
            g_currentIndex = index;
            orig_BSShadowLight_AccumulateShadowMap.call(a_self, a_data, a_pShadowMaskChannel, a_cullingProcessArray, a_jobIndex);
            g_currentIndex = previousIndex;
        }

        inline SafetyHookInline orig_BSLightingShaderProperty_ClearRenderPassArrays;

        inline void BSLightingShaderProperty_ClearRenderPassArrays(RE::BSLightingShaderProperty* a_self)
        {
            // Retire, not Clear()-in-place: mutating a live entry's pass chain races
            // whatever engine code is still walking a previously-returned pointer.
            RetireAllocatedArrays(a_self);
            orig_BSLightingShaderProperty_ClearRenderPassArrays.call(a_self);
        }

        inline SafetyHookInline orig_BSLightingShaderProperty_dtor;

        inline void BSLightingShaderProperty_Dtor(RE::BSLightingShaderProperty* a_self)
        {
            RetireAllocatedArrays(a_self);
            orig_BSLightingShaderProperty_dtor.call(a_self);
        }

        inline SafetyHookInline orig_BSLightingShaderProperty_deleting_dtor;

        inline void BSLightingShaderProperty_Deleting_Dtor(RE::BSLightingShaderProperty* a_self, std::uint8_t a_flags)
        {
            RetireAllocatedArrays(a_self);
            orig_BSLightingShaderProperty_deleting_dtor.call(a_self, a_flags);
        }

        inline void Install()
        {
            const REL::Relocation _AccumulateShadowMap{ RELOCATION_ID(100818, 107602) };
            orig_BSShadowLight_AccumulateShadowMap = safetyhook::create_inline(_AccumulateShadowMap.address(), BSShadowLight_AccumulateShadowMap);

            REL::Relocation GetRenderPasses_ShadowMapOrMask{ RELOCATION_ID(99872, 106517), VAR_NUM(0x291, 0x295) };
            auto&           trampoline = SKSE::GetTrampoline();
            Patch           p(GetRenderPasses_ShadowMapOrMask.address(), SKSE::stl::unrestricted_cast<std::uintptr_t>(BSLightingShaderProperty_GetRenderPasses_ShadowMapOrMask_Detour));
            p.ready();
            GetRenderPasses_ShadowMapOrMask.write_branch<5>(trampoline.allocate(p));

            const REL::Relocation ClearArrays{ RELOCATION_ID(99881, 106526) };
            orig_BSLightingShaderProperty_ClearRenderPassArrays = safetyhook::create_inline(ClearArrays.address(), BSLightingShaderProperty_ClearRenderPassArrays);

            const REL::Relocation dtor{ RELOCATION_ID(99855, 106500) };
            orig_BSLightingShaderProperty_dtor = safetyhook::create_inline(dtor.address(), BSLightingShaderProperty_Dtor);

            if (REL::Module::IsAE()) {
                const REL::Relocation deleting_dtor{ REL::ID(106534) };
                orig_BSLightingShaderProperty_deleting_dtor = safetyhook::create_inline(deleting_dtor.address(), BSLightingShaderProperty_Deleting_Dtor);
            }
        }
    }

    inline void Install()
    {
        detail::Install();
        logger::info("installed bslightingshaderproperty shadowmap fix"sv);
    }
}
