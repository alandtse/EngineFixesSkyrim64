#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <intrin.h>

#include "memory/allocator.h"

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

        using ScratchBlock = std::array<RE::BSShaderProperty::RenderPassArray, kShadowPassCount>;

        // head is walked and freed by vanilla's own Clear(); storing the scratch block
        // there crashes on that walk. unk08 is confirmed unused by any runtime, so the
        // block's address goes there instead and head stays permanently null.
        inline ScratchBlock* GetOrCreateScratch(RE::BSLightingShaderProperty* a_property)
        {
            auto& passes = a_property->volumetricShadowUtilityPasses;
            if (const auto existing = reinterpret_cast<ScratchBlock*>(*reinterpret_cast<volatile long long*>(&passes.unk08)))
                return existing;

            auto* block = static_cast<ScratchBlock*>(Memory::Allocator::GetAllocator()->AllocateAligned(sizeof(ScratchBlock), alignof(ScratchBlock)));
            std::memset(block, 0, sizeof(ScratchBlock));

            // Two job threads can race to service different indices for the same
            // property before either has published a block; only one may win.
            const auto prior = _InterlockedCompareExchange64(
                reinterpret_cast<volatile long long*>(&passes.unk08),
                reinterpret_cast<long long>(block),
                0);
            if (prior != 0) {
                Memory::Allocator::GetAllocator()->DeallocateAligned(block);
                return reinterpret_cast<ScratchBlock*>(prior);
            }
            return block;
        }

        inline void FreeScratch(RE::BSLightingShaderProperty* a_property)
        {
            auto& passes = a_property->volumetricShadowUtilityPasses;
            if (!passes.unk08)
                return;

            auto* block = reinterpret_cast<ScratchBlock*>(passes.unk08);
            for (auto& passArray : *block) {
                passArray.Clear();
            }
            Memory::Allocator::GetAllocator()->DeallocateAligned(block);
            passes.unk08 = 0;
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

            auto* block = GetOrCreateScratch(a_property);
            auto& passArray = (*block)[index];
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

        inline SafetyHookInline orig_BSLightingShaderProperty_dtor;

        inline void BSLightingShaderProperty_Dtor(RE::BSLightingShaderProperty* a_self)
        {
            FreeScratch(a_self);
            orig_BSLightingShaderProperty_dtor.call(a_self);
        }

        inline SafetyHookInline orig_BSLightingShaderProperty_deleting_dtor;

        inline void BSLightingShaderProperty_Deleting_Dtor(RE::BSLightingShaderProperty* a_self, std::uint8_t a_flags)
        {
            FreeScratch(a_self);
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

            // ClearRenderPassArrays is intentionally left un-hooked: vanilla's own
            // Clear() call there is a head-only no-op for us, and the block only needs
            // to go away once the property itself is destroyed.
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
