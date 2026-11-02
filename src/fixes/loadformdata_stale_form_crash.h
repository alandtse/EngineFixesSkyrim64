#pragma once

#include "installed_fixes.h"

namespace Fixes::LoadFormDataStaleFormCrash
{
    // Queued load entries hold an unreferenced TESForm* until InitAndFinishLoadForms consumes
    // them; re-resolve by id instead of trusting a pointer that may have been freed.
    namespace detail
    {
        using Flags = RE::BGSLoadFormData::Flags;

        constexpr std::uint32_t kMaxStaleLogs = 5;
        constexpr std::size_t   kMaxQueuedRefs = 1u << 20;

        inline SafetyHookInline orig_GetForm;
        inline SafetyHookInline orig_GetReference;
        inline SafetyHookInline orig_QueueReferenceLoad;
        inline SafetyHookInline orig_InitAndFinishLoadForms;

        inline std::mutex                                         queuedMutex;
        inline std::unordered_map<const RE::TESForm*, RE::FormID> queuedRefIDs;

        inline void LogStale(RE::FormID a_formID)
        {
            static std::atomic_uint32_t staleCount{ 0 };
            if (++staleCount <= kMaxStaleLogs)
                logger::warn("replaced a stale cached form pointer for form {:X}"sv, a_formID);
        }

        inline std::optional<RE::FormID> QueuedFormID(const RE::TESForm* a_form)
        {
            std::scoped_lock lock{ queuedMutex };
            const auto       it = queuedRefIDs.find(a_form);
            return it != queuedRefIDs.end() ? std::optional{ it->second } : std::nullopt;
        }

        inline RE::TESForm* GetForm(RE::BGSLoadFormData* a_this)
        {
            if (a_this->flags.all(Flags::kFormPending)) {
                const auto resolved = RE::TESForm::LookupByID(a_this->formID);
                if (a_this->form && a_this->form != resolved)
                    LogStale(a_this->formID);
                a_this->form = resolved;
            } else if (a_this->form) {
                if (const auto formID = QueuedFormID(a_this->form)) {
                    const auto resolved = RE::TESForm::LookupByID(*formID);
                    if (resolved != a_this->form) {
                        LogStale(*formID);
                        a_this->form = resolved;
                    }
                }
            }
            return a_this->form;
        }

        inline RE::TESObjectREFR* GetReference(RE::BGSLoadFormData* a_this)
        {
            const auto form = GetForm(a_this);
            return form ? const_cast<RE::TESObjectREFR*>(form->AsReference2()) : nullptr;
        }

        inline bool QueueReferenceLoad(void* a_this, RE::TESObjectREFR* a_ref, void* a_buffer)
        {
            const bool queued = orig_QueueReferenceLoad.call<bool>(a_this, a_ref, a_buffer);
            if (queued && a_ref) {
                const auto formID = a_ref->GetFormID();
                if (formID != 0 && RE::TESForm::LookupByID(formID) == a_ref) {
                    std::scoped_lock lock{ queuedMutex };
                    if (queuedRefIDs.size() >= kMaxQueuedRefs)
                        queuedRefIDs.clear();
                    queuedRefIDs[a_ref] = formID;
                }
            }
            return queued;
        }

        inline void InitAndFinishLoadForms(void* a_this, bool a_flushIO)
        {
            orig_InitAndFinishLoadForms.call(a_this, a_flushIO);
            std::scoped_lock lock{ queuedMutex };
            queuedRefIDs.clear();
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
        static constexpr std::array<std::uint8_t, 18> kQueueReferenceLoad{ 0x4C, 0x8B, 0xDC, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x49, 0xC7, 0x43, 0xB8, 0xFE };
        static constexpr std::array<std::uint8_t, 18> kInitFlat{ 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x6C, 0x24, 0xD9, 0x48 };
        static constexpr std::array<std::uint8_t, 18> kInitAE{ 0x48, 0x8B, 0xC4, 0x55, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0x68, 0xA1, 0x48, 0x81 };

        const auto getForm = RELOCATION_ID(35095, 35987).address();
        const auto getReference = RELOCATION_ID(35096, 35988).address();
        const auto queueReferenceLoad = RELOCATION_ID(34647, 35569).address();
        const auto initAndFinishLoadForms = RELOCATION_ID(34645, 35567).address();

        const bool isAE = REL::Module::IsAE();
        if (!detail::Matches(getForm, isAE ? kGetFormAE : kGetFormFlat) ||
            !detail::Matches(getReference, isAE ? kGetReferenceAE : kGetReferenceFlat) ||
            !detail::Matches(queueReferenceLoad, kQueueReferenceLoad) ||
            !detail::Matches(initAndFinishLoadForms, isAE ? kInitAE : kInitFlat)) {
            logger::warn("skipping BGSLoadFormData stale form guard: function signature mismatch"sv);
            return;
        }

        detail::orig_GetForm = safetyhook::create_inline(getForm, detail::GetForm);
        detail::orig_GetReference = safetyhook::create_inline(getReference, detail::GetReference);
        detail::orig_QueueReferenceLoad = safetyhook::create_inline(queueReferenceLoad, detail::QueueReferenceLoad);
        detail::orig_InitAndFinishLoadForms = safetyhook::create_inline(initAndFinishLoadForms, detail::InitAndFinishLoadForms);
        EF_INSTALLED("LoadFormDataStaleFormCrash"sv, "installed BGSLoadFormData stale form guard"sv);
    }
}
