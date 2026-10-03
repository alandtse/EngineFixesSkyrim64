#pragma once

#include <string>
#include <string_view>
#include <unordered_set>

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

// Record a capability and log the install line together. Use at the point a fix has verified
// it actually installed, past every early-out, so the registry can never claim a fix that
// silently skipped -- and so the capability name and the log line cannot drift apart.
//
// A macro rather than a function because logger::info takes std::source_location::current()
// as a defaulted argument: wrapped in a function, every install line in the log would report
// installed_fixes.h instead of the fix's own file.
//
// Every fix, patch and memory override registers under its namespace's leaf name, so other
// plugins can query it through EngineFixes_IsFixInstalled. A fix with several independent sites
// registers on each site that installs, so it reads as installed when at least one did.
// MarkInstalled stays available for a fix that needs to register without logging.
#define EF_INSTALLED(a_capability, ...)                \
    do {                                               \
        ::InstalledFixes::MarkInstalled(a_capability); \
        logger::info(__VA_ARGS__);                     \
    } while (false)
