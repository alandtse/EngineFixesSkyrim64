#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

// Records which fixes actually patched at least one site, so other plugins can ask through the
// EngineFixes_IsFixInstalled export instead of guessing from the settings file.
namespace InstalledFixes
{
    inline std::unordered_set<std::string>& Registry()
    {
        static std::unordered_set<std::string> registry;
        return registry;
    }

    inline void MarkInstalled(std::string_view a_name)
    {
        Registry().emplace(a_name);
    }

    inline bool IsInstalled(std::string_view a_name)
    {
        return Registry().contains(std::string{ a_name });
    }
}
