#pragma once

#include "installed_fixes.h"

#include <cstring>
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

        // push rbx; sub rsp,0x30; mov qword [rsp+0x20],-2; mov ecx,7 (job list 7)
        inline constexpr std::uint8_t kJobTesAnimationPrologue[] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0xB9, 0x07, 0x00, 0x00, 0x00 };
        // SE/VR: push rsi/rdi/r14; sub rsp,0x50; mov qword [rsp+0x20],-2
        inline constexpr std::uint8_t kUpdateNonHighActorsPrologueFlat[] = { 0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x50, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF };
        // AE: mov rax,rsp; push rsi/rdi/r14; sub rsp,0x60; mov qword [rax-0x50],-2
        inline constexpr std::uint8_t kUpdateNonHighActorsPrologueAE[] = { 0x48, 0x8B, 0xC4, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x60, 0x48, 0xC7, 0x40, 0xB0, 0xFE, 0xFF, 0xFF, 0xFF };

        template <std::size_t N>
        inline bool PrologueMatches(const std::uintptr_t a_addr, const std::uint8_t (&a_expected)[N])
        {
            return std::memcmp(reinterpret_cast<const void*>(a_addr), a_expected, N) == 0;
        }

        inline bool Install()
        {
            const auto updateAddr = RELOCATION_ID(40291, 41301).address();
            const auto animAddr = RELOCATION_ID(38090, 39045).address();
            const bool updateMatches = REL::Module::IsAE() ? PrologueMatches(updateAddr, kUpdateNonHighActorsPrologueAE) : PrologueMatches(updateAddr, kUpdateNonHighActorsPrologueFlat);
            if (!updateMatches || !PrologueMatches(animAddr, kJobTesAnimationPrologue)) {
                logger::warn("job list race fix skipped: UpdateNonHighActors or Job_TES_animation prologue does not match"sv);
                return false;
            }
            orig_UpdateNonHighActors = safetyhook::create_inline(updateAddr, UpdateNonHighActors);
            orig_JobTesAnimation = safetyhook::create_inline(animAddr, JobTesAnimation);
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
