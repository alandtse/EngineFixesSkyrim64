#pragma once

#include "tree_lod_reference_caching.h"

// notes on form caching
// 14688 - SetAt - inserts form to map, replaces if it already exists
// 14710 - RemoveAt - removes form from map
// g_FormMap xrefs
// 13689 - TESDataHandler::dtor - clears entire form map, called by TES::dtor, this only happens on game shutdown
// 13754 - TESDataHandler::ClearData - clears entire form map, called on game shutdown but also on Main::PerformGameReset, hook to clear our cache
// 13785 - HotLoadPlugin command handler - no one should be using this, so don't worry about it
// 14593 - TESForm::ctor - calls RemoveAt and SetAt, so handled by those hooks
// 14594 - TESForm::dtor_1 - deallocs the form map if there are zero forms
// 14617 - TESForm::GetFormByNumericId - form lookup, hooked
// 14627 - TESForm::RemoveFromDataStructures - calls RemoveAt, handled by that hook
// 14666 - TESForm::SetFormId - changes formid of form, if form is NOT temporary, removes old id from map and adds new one, calls SetAt/RemoveAt
// 441564 - TESForm::ReleaseFormDataStructures - deletes form map, inlined in TESForm dtors
// 14669 - TESForm::InitializeFormDataStructures - creates new empty form map, hook to clear our cache
// 14703 - TESForm::dtor_2 - deallocs the form map if there are zero forms
// 22839 - ConsoleFunc::Help - inlined form reader
// 22869 - ConsoleFunc::TestCode - inlined form reader
// 35865 - LoadGameCleanup - inlined form reader

namespace Patches::FormCaching
{
    namespace detail
    {
        using HashMap = tbb::concurrent_hash_map<std::uint32_t, RE::TESForm*>;

        inline HashMap g_formCache[256];

        inline RE::TESForm* TESForm_GetFormByNumericId(RE::FormID a_formId)
        {
            RE::TESForm* formPointer = nullptr;

            const std::uint8_t  masterId = (a_formId & 0xFF000000) >> 24;
            const std::uint32_t baseId = (a_formId & 0x00FFFFFF);

            // lookup form in our cache first
            {
                HashMap::const_accessor a;

                if (g_formCache[masterId].find(a, baseId)) {
                    formPointer = a->second;
                    return formPointer;
                }
            }

            // lookup form in bethesda's map
            formPointer = RE::TESForm::LookupByID(a_formId);

            if (formPointer != nullptr) {
                g_formCache[masterId].emplace(baseId, formPointer);
            }

            return formPointer;
        }

        // Shared cache helpers — called by both AE and SE/VR hook variants

        inline void OnFormRemoved(RE::FormID* a_formIdPtr, bool a_removed)
        {
            if (a_removed) {
                const std::uint8_t  masterId = (*a_formIdPtr & 0xFF000000) >> 24;
                const std::uint32_t baseId   = (*a_formIdPtr & 0x00FFFFFF);
                g_formCache[masterId].erase(baseId);
                TreeLodReferenceCaching::detail::RemoveCachedForm(baseId);
            }
        }

        // the functor stores the TESForm to set as the first field
        inline void OnFormSet(RE::FormID* a_formIdPtr, RE::TESForm* a_form)
        {
            if (a_form) {
                const std::uint8_t  masterId = (*a_formIdPtr & 0xFF000000) >> 24;
                const std::uint32_t baseId   = (*a_formIdPtr & 0x00FFFFFF);
                HashMap::accessor a;
                if (!g_formCache[masterId].emplace(a, baseId, a_form)) {
                    logger::trace("replacing an existing form in form cache"sv);
                    a->second = a_form;
                }
                TreeLodReferenceCaching::detail::RemoveCachedForm(baseId);
            }
        }

        inline SafetyHookInline g_hk_RemoveAt{};

        // AE: RemoveAt(BSTHashMap*, FormID*, functor*) -> uint64
        inline std::uint64_t FormMap_RemoveAt_AE(
            RE::BSTHashMap<RE::FormID, RE::TESForm*>* a_self,
            RE::FormID*                               a_formIdPtr,
            void*                                     a_prevValueFunctor)
        {
            const auto result = g_hk_RemoveAt.call<std::uint64_t>(a_self, a_formIdPtr, a_prevValueFunctor);
            OnFormRemoved(a_formIdPtr, result == 1);
            return result;
        }

        // SE/VR: RemoveAt(self, arg2, crc, FormID*, functor*) -> uint32
        inline std::uint32_t FormMap_RemoveAt_SE(
            std::uintptr_t a_self,
            std::uintptr_t a_arg2,
            std::uint32_t  a_crc,
            RE::FormID*    a_formIdPtr,
            void*          a_prevValueFunctor)
        {
            const auto result = g_hk_RemoveAt.call<std::uint32_t>(a_self, a_arg2, a_crc, a_formIdPtr, a_prevValueFunctor);
            OnFormRemoved(a_formIdPtr, result == 1);
            return result;
        }

        // AE: SetAt(self, FormID*, TESForm**, unk*) -> bool
        static inline REL::Relocation<bool(std::uintptr_t, RE::FormID*, RE::TESForm**, void*)> orig_FormScatterTable_SetAt_AE;

        inline bool FormScatterTable_SetAt_AE(
            std::uintptr_t a_self,
            RE::FormID*    a_formIdPtr,
            RE::TESForm**  a_valueFunctor,
            void*          a_unk)
        {
            OnFormSet(a_formIdPtr, *a_valueFunctor);
            return orig_FormScatterTable_SetAt_AE(a_self, a_formIdPtr, a_valueFunctor, a_unk);
        }

        // SE/VR: SetAt(self, arg2, crc, FormID*, TESForm**, unk*) -> bool
        static inline REL::Relocation<bool(std::uintptr_t, std::uintptr_t, std::uint32_t, RE::FormID*, RE::TESForm**, void*)> orig_FormScatterTable_SetAt_SE;

        inline bool FormScatterTable_SetAt_SE(
            std::uintptr_t a_self,
            std::uintptr_t a_arg2,
            std::uint32_t  a_crc,
            RE::FormID*    a_formIdPtr,
            RE::TESForm**  a_valueFunctor,
            void*          a_unk)
        {
            OnFormSet(a_formIdPtr, *a_valueFunctor);
            return orig_FormScatterTable_SetAt_SE(a_self, a_arg2, a_crc, a_formIdPtr, a_valueFunctor, a_unk);
        }

        inline SafetyHookInline g_hk_ClearData;

        // the game does not lock the form table on these clears so we won't either
        // maybe fix later if it causes issues
        inline void TESDataHandler_ClearData(RE::TESDataHandler* a_self)
        {
            for (auto& map : g_formCache)
                map.clear();

            TreeLodReferenceCaching::detail::ClearCache();

            g_hk_ClearData.call(a_self);
        }

        inline SafetyHookInline g_hk_InitializeFormDataStructures;

        inline void TESForm_InitializeFormDataStructures()
        {
            for (auto& map : g_formCache)
                map.clear();

            TreeLodReferenceCaching::detail::ClearCache();

            g_hk_InitializeFormDataStructures.call();
        }

        inline void ReplaceFormMapFunctions()
        {
            REL::Relocation getForm{ RELOCATION_ID(14461, 14617) };
            getForm.replace_func(0x9E, TESForm_GetFormByNumericId);

            const REL::Relocation RemoveAt{ RELOCATION_ID(14514, 14710) };

            // AE and SE/VR have different calling conventions for RemoveAt and SetAt.
            // AE dropped the hash/crc params from the scatter table API.
            if (REL::Module::IsAE()) {
                g_hk_RemoveAt = safetyhook::create_inline(RemoveAt.address(), FormMap_RemoveAt_AE);

                // there is one call that is not the form table so we will callsite hook
                constexpr std::array todoSetAt = {
                    std::pair(14593, 0x2B0),
                    std::pair(14593, 0x301),
                    std::pair(14666, 0xFE),
                };
                for (auto& [id, offset] : todoSetAt) {
                    REL::Relocation target{ REL::ID(id), offset };
                    orig_FormScatterTable_SetAt_AE = target.write_call<5>(FormScatterTable_SetAt_AE);
                }
            } else {
                g_hk_RemoveAt = safetyhook::create_inline(RemoveAt.address(), FormMap_RemoveAt_SE);

                constexpr std::array todoSetAt = {
                    std::pair(14438, 0x1DE),
                    std::pair(14438, 0x214),
                    std::pair(14508, 0x16C),
                    std::pair(14508, 0x1A2),
                };
                for (auto& [id, offset] : todoSetAt) {
                    REL::Relocation target{ REL::ID(id), offset };
                    orig_FormScatterTable_SetAt_SE = target.write_call<5>(FormScatterTable_SetAt_SE);
                }
            }

            const REL::Relocation ClearData{ RELOCATION_ID(13646, 13754) };
            g_hk_ClearData = safetyhook::create_inline(ClearData.address(), TESDataHandler_ClearData);

            const REL::Relocation InitializeFormDataStructures{ RELOCATION_ID(14511, 14669) };
            g_hk_InitializeFormDataStructures = safetyhook::create_inline(InitializeFormDataStructures.address(), TESForm_InitializeFormDataStructures);
        }
    }

    inline void Install()
    {
        detail::ReplaceFormMapFunctions();

        logger::info("installed form caching patch"sv);
    }
}
