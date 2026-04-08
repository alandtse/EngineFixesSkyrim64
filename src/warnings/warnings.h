#pragma once

namespace Warnings
{
    void WarnActiveRefrHandleCount(std::uint32_t warnCount);

    // Warns when duplicate BGSAddonNode records are loaded (all runtimes)
    namespace WarnDupeAddonNodes
    {
        void Install();
    }
}