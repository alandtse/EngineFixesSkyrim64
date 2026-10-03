#pragma once

#include "installed_fixes.h"

namespace Fixes::PerkFragmentIsRunning
{
    namespace detail
    {
        inline bool IsRunning(const RE::Actor* a_self)
        {
            return a_self ? a_self->IsRunning() : false;
        }
    }

    inline void Install()
    {
        REL::Relocation target{ RELOCATION_ID(21119, 21571), 0x22 };
        target.write_call<5>(detail::IsRunning);

        EF_INSTALLED("PerkFragmentIsRunning"sv, "installed perk fragment is running fix"sv);
    }
}
