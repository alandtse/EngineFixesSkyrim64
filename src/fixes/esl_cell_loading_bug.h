#pragma once
#include <stacktrace>

namespace Fixes::ESLCELLLoadingBugs
{
    namespace detail
    {
        inline SafetyHookInline orig_GetGroupBlockKey;

        inline std::uint32_t TESObjectCELL_GetGroupBlockKey(const RE::TESObjectCELL* a_this)
        {
            if (a_this->IsInteriorCell()) {
                const RE::TESFile* file = a_this->GetFile(0);
                // GetLocalFormID masks 0xFFF for ESL, 0xFFFFFF for regular — requires non-null file
                const auto localFormID = file ? a_this->GetLocalFormID() : (a_this->GetFormID() & 0xFFFFFF);
                return localFormID % 0xA;
            }

            if (a_this->GetRuntimeData().cellData.exterior) {
                return (a_this->GetRuntimeData().cellData.exterior->cellY >> 5) & 0x0000FFFF | (((a_this->GetRuntimeData().cellData.exterior->cellX >> 5) << 16) & 0xFFFF0000);
            }

            return 0;
        }

        inline SafetyHookInline orig_GetGroupSubBlockKey;

        inline std::uint32_t TESObjectCELL_GetGroupSubBlockKey(const RE::TESObjectCELL* a_this)
        {
            if (a_this->IsInteriorCell()) {
                const RE::TESFile* file = a_this->GetFile(0);
                // GetLocalFormID masks 0xFFF for ESL, 0xFFFFFF for regular — requires non-null file
                const auto localFormID = file ? a_this->GetLocalFormID() : (a_this->GetFormID() & 0xFFFFFF);
                return (localFormID % 0x64) / 0xA;
            }

            if (a_this->GetRuntimeData().cellData.exterior) {
                return (a_this->GetRuntimeData().cellData.exterior->cellY >> 3) & 0x0000FFFF | (((a_this->GetRuntimeData().cellData.exterior->cellX >> 3) << 16) & 0xFFFF0000);
            }

            return 0;
        }

        inline void Install()
        {
            orig_GetGroupBlockKey = safetyhook::create_inline(RELOCATION_ID(18454, 18885).address(), TESObjectCELL_GetGroupBlockKey);
            orig_GetGroupSubBlockKey = safetyhook::create_inline(RELOCATION_ID(18455, 18886).address(), TESObjectCELL_GetGroupSubBlockKey);
        }
    }

    inline void Install()
    {
        detail::Install();
        logger::info("installed ESL CELL load bug fix"sv);
    }
}