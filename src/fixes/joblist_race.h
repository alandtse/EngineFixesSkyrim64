#pragma once

#include "installed_fixes.h"

#include <mutex>

namespace Fixes::JobListRace
{
    namespace detail
    {
        inline std::recursive_mutex listLock;

        inline SafetyHookInline orig_UpdateNonHighActors;
        inline SafetyHookInline orig_JobTesAnimation;

        // Both functions submit, finish and clear the same job list; the lock must cover all of that
        inline void UpdateNonHighActors(void* a_processLists, float a_elapsedSeconds, std::uint8_t a_unk2, bool a_flags)
        {
            std::scoped_lock lock{ listLock };
            orig_UpdateNonHighActors.call<void>(a_processLists, a_elapsedSeconds, a_unk2, a_flags);
        }

        inline void JobTesAnimation()
        {
            std::scoped_lock lock{ listLock };
            orig_JobTesAnimation.call<void>();
        }

        inline std::uintptr_t Address(const REL::RelocationID a_id, const std::uintptr_t a_vrOffset)
        {
            return REL::Module::IsVR() ? REL::Relocation<std::uintptr_t>{ REL::Offset{ a_vrOffset } }.address() : a_id.address();
        }

        inline bool Install()
        {
            orig_UpdateNonHighActors = safetyhook::create_inline(Address(RELOCATION_ID(40291, 41301), 0x6FB570), UpdateNonHighActors);
            orig_JobTesAnimation = safetyhook::create_inline(Address(RELOCATION_ID(38090, 39045), 0x649060), JobTesAnimation);
            if (!orig_UpdateNonHighActors || !orig_JobTesAnimation) {
                logger::warn("job list race fix skipped: could not hook UpdateNonHighActors or Job_TES_animation"sv);
                return false;
            }
            return true;
        }
    }

    inline void Install()
    {
        if (detail::Install())
            EF_INSTALLED("JobListRace"sv, "installed job list 7 race fix"sv);
    }
}
