#pragma once

#include <algorithm>
#include <array>
#include <string_view>

// The only names EngineFixes_IsFixInstalled answers for. Append-only: a retired capability keeps
// its constant and simply stops being registered, so consumers read false instead of breaking.
//
// A name here promises what `true` means, not just that a fix ran. Add one only for a contract
// another plugin can observe, state that contract beside the constant, and register it only at
// the site that owns it (or after every mandatory site has installed).
namespace InstalledFixes::Public
{
    using namespace std::string_view_literals;

    // BatchRenderer alpha-group slot allocation is bounds-checked.
    inline constexpr auto kBatchRendererAlphaGeometryGroupOverflow = "BatchRendererAlphaGeometryGroupOverflow"sv;

    // BSCullingProcess::AppendVirtual drops the append when its shadow-caster pool is exhausted.
    inline constexpr auto kCullingProcessAppendVirtualPoolGuard = "CullingProcessAppendVirtualPoolGuard"sv;

    // BSRenderPass sceneLights storage holds 64 slots per pass.
    inline constexpr auto kRenderPassCacheSceneLights64 = "RenderPassCacheSceneLights64"sv;

    inline constexpr std::array kAll{
        kBatchRendererAlphaGeometryGroupOverflow,
        kCullingProcessAppendVirtualPoolGuard,
        kRenderPassCacheSceneLights64,
    };

    inline constexpr bool IsPublic(std::string_view a_name)
    {
        return std::ranges::find(kAll, a_name) != kAll.end();
    }
}
