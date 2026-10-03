#pragma once

#include "installed_fixes.h"

namespace Fixes::GHeapLeakDetectionCrash
{
    inline void Install()
    {
        REL::Relocation target{ RELOCATION_ID(85757, 87837), 0x4B };
        target.write_fill(REL::NOP, 0x11);

        EF_INSTALLED("GHeapLeakDetectionCrash"sv, "installed gheap leak detection crash fix"sv);
    }
}
