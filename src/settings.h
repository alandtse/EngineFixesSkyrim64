#pragma once

#include "settings_schema.h"

namespace Settings
{
    // The REX::TOML bindings are generated from settings_schema.h so they can
    // never drift from the TOML the generator writes. Section is fixed per list;
    // each X(type, key, default, comment) becomes one inline setting object.

    namespace General
    {
#define X(type, key, def, comment) inline REX::TOML::type key("General", #key, def);
        EF_SETTINGS_GENERAL(X)
#undef X
    }

    namespace Fixes
    {
#define X(type, key, def, comment) inline REX::TOML::type key("Fixes", #key, def);
        EF_SETTINGS_FIXES(X)
#undef X
    }

    namespace Patches
    {
#define X(type, key, def, comment) inline REX::TOML::type key("Patches", #key, def);
        EF_SETTINGS_PATCHES(X)
#undef X
    }

    namespace MemoryManager
    {
#define X(type, key, def, comment) inline REX::TOML::type key("MemoryManager", #key, def);
        EF_SETTINGS_MEMORYMANAGER(X)
#undef X
    }

    namespace Warnings
    {
#define X(type, key, def, comment) inline REX::TOML::type key("Warnings", #key, def);
        EF_SETTINGS_WARNINGS(X)
#undef X
    }

    namespace Debug
    {
#define X(type, key, def, comment) inline REX::TOML::type key("Debug", #key, def);
        EF_SETTINGS_DEBUG(X)
#undef X
    }

    // Regenerates Data/SKSE/Plugins/EngineFixes.toml so every declared setting is
    // present (self-healing: missing keys are added with their defaults and
    // descriptions; existing values are preserved), then loads base + custom TOML.
    void Load();
}
