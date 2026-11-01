#pragma once

#include "installed_fixes.h"

namespace Fixes::LoadFormDataStaleFormCrash
{
    // GetForm caches an unreferenced TESForm* while kFormPending is set; re-resolve by id
    // instead of trusting a pointer that may have been freed.
    namespace detail
    {
        using Flags = RE::BGSLoadFormData::Flags;

        constexpr std::uint32_t kMaxStaleLogs = 5;

        inline SafetyHookInline orig_GetForm;
        inline SafetyHookInline orig_GetReference;

        inline RE::TESForm* GetForm(RE::BGSLoadFormData* a_this)
        {
            if (a_this->flags.all(Flags::kFormPending)) {
                const auto resolved = RE::TESForm::LookupByID(a_this->formID);
                if (a_this->form && a_this->form != resolved) {
                    static std::atomic_uint32_t staleCount{ 0 };
                    if (++staleCount <= kMaxStaleLogs)
                        logger::warn("replaced a stale cached form pointer for form {:X}"sv, a_this->formID);
                }
                a_this->form = resolved;
            }
            return a_this->form;
        }

        inline RE::TESObjectREFR* GetReference(RE::BGSLoadFormData* a_this)
        {
            const auto form = GetForm(a_this);
            return form ? const_cast<RE::TESObjectREFR*>(form->AsReference2()) : nullptr;
        }

        template <std::size_t N>
        inline bool Matches(std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_expected)
        {
            return std::memcmp(reinterpret_cast<const void*>(a_address), a_expected.data(), N) == 0;
        }
    }

    inline void Install()
    {
        static constexpr std::array<std::uint8_t, 18> kGetFormFlat{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x79, 0x10, 0x00, 0x48, 0x8B, 0xD9, 0x75, 0x1B, 0x0F, 0xB6 };
        static constexpr std::array<std::uint8_t, 18> kGetFormAE{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x41, 0x10, 0x48, 0x8B, 0xD9, 0x48, 0x85, 0xC0, 0x75, 0x1D };
        static constexpr std::array<std::uint8_t, 18> kGetReferenceFlat{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x83, 0x79, 0x10, 0x00, 0x48, 0x8B, 0xD9, 0x75, 0x15, 0x0F, 0xB6 };
        static constexpr std::array<std::uint8_t, 18> kGetReferenceAE{ 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x49, 0x10, 0x48, 0x85, 0xC9, 0x75, 0x1D };

        const auto getForm = RELOCATION_ID(35095, 35987).address();
        const auto getReference = RELOCATION_ID(35096, 35988).address();

        const bool isAE = REL::Module::IsAE();
        if (!detail::Matches(getForm, isAE ? kGetFormAE : kGetFormFlat) ||
            !detail::Matches(getReference, isAE ? kGetReferenceAE : kGetReferenceFlat)) {
            logger::warn("skipping BGSLoadFormData stale form guard: function signature mismatch"sv);
            return;
        }

        detail::orig_GetForm = safetyhook::create_inline(getForm, detail::GetForm);
        detail::orig_GetReference = safetyhook::create_inline(getReference, detail::GetReference);
        EF_INSTALLED("LoadFormDataStaleFormCrash"sv, "installed BGSLoadFormData stale form guard"sv);
    }
}
