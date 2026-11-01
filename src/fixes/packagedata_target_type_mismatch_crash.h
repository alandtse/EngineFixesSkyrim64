#pragma once

#include "installed_fixes.h"

namespace Fixes::PackageDataTargetTypeMismatchCrash
{
    // EvaluatePackageDataTarget calls vfunc 1 of `entry - 8`, which is only valid for
    // pointer-template data; single-vtable types such as BGSPackageDataBool yield garbage.
    namespace detail
    {
        constexpr std::ptrdiff_t kBindingsOffset = 0x8;   // BGSProcedureBase: data bindings
        constexpr std::ptrdiff_t kDataListOffset = 0x10;  // evaluation context: BGSPackageDataList*

        using Evaluate_t = void*(void*, void**, std::uint32_t, void**);
        using GetBound_t = void*(void*, std::uint32_t, void*);

        inline SafetyHookInline orig_Evaluate;
        inline GetBound_t*      GetBoundPackageData{ nullptr };

        inline bool IsVtablePointer(std::uintptr_t a_value) noexcept
        {
            const auto& rdata = REL::Module::get().segment(REL::Segment::rdata);
            return a_value >= rdata.address() && a_value < rdata.address() + rdata.size();
        }

        inline bool HasPrimaryVtable(void* a_entry) noexcept
        {
            __try {
                return IsVtablePointer(*reinterpret_cast<std::uintptr_t*>(static_cast<std::byte*>(a_entry) - sizeof(void*)));
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                return false;
            }
        }

        inline void* Evaluate(void* a_this, void** a_context, std::uint32_t a_index, void** a_outTarget)
        {
            auto* const dataList = *reinterpret_cast<void**>(static_cast<std::byte*>(*a_context) + kDataListOffset);
            auto* const entry = GetBoundPackageData(static_cast<std::byte*>(a_this) + kBindingsOffset, a_index, dataList);
            if (entry && !HasPrimaryVtable(entry)) {
                static std::atomic_bool warned{ false };
                if (!warned.exchange(true))
                    logger::warn("skipped a package procedure whose data input {} is not a target type (package data type mismatch)"sv, a_index);
                *a_outTarget = nullptr;
                return nullptr;
            }

            return orig_Evaluate.call<void*>(a_this, a_context, a_index, a_outTarget);
        }

        template <std::size_t N>
        inline bool Matches(std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_expected)
        {
            return std::memcmp(reinterpret_cast<const void*>(a_address), a_expected.data(), N) == 0;
        }
    }

    inline void Install()
    {
        static constexpr std::array<std::uint8_t, 14> kEvaluateFlat{ 0x40, 0x53, 0x56, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x89, 0x6C, 0x24, 0x60 };
        static constexpr std::array<std::uint8_t, 14> kEvaluateAE{ 0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x56, 0x57, 0x41, 0x54 };
        static constexpr std::array<std::uint8_t, 14> kGetBound{ 0x80, 0x79, 0x18, 0x00, 0x48, 0x8B, 0x01, 0x8B, 0xD2, 0x4C, 0x8D, 0x0C, 0xD0, 0x74 };

        const auto evaluate = RELOCATION_ID(27464, 28181).address();
        const auto getBound = RELOCATION_ID(27467, 28187).address();

        const auto& expectedEvaluate = REL::Module::IsAE() ? kEvaluateAE : kEvaluateFlat;
        if (!detail::Matches(evaluate, expectedEvaluate) || !detail::Matches(getBound, kGetBound)) {
            logger::warn("skipping package data target type mismatch guard: function signature mismatch"sv);
            return;
        }

        detail::GetBoundPackageData = reinterpret_cast<detail::GetBound_t*>(getBound);
        detail::orig_Evaluate = safetyhook::create_inline(evaluate, detail::Evaluate);
        EF_INSTALLED("PackageDataTargetTypeMismatchCrash"sv, "installed package data target type mismatch guard"sv);
    }
}
