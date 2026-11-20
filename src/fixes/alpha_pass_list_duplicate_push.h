#pragma once

#include "installed_fixes.h"

#include <atomic>
#include <cstdint>
#include <cstring>

// GeometryGroup::AppendPass links a pass into a linked (alpha) group through the single
// BSRenderPass::passGroupNext field with no membership check, so re-pushing a pass that is still
// queued truncates the queue behind it (back insert) or closes a ring (front insert).
namespace Fixes::AlphaPassListDuplicatePush
{
    namespace detail
    {
        using Group = RE::BSBatchRenderer::GeometryGroup;
        using Pass = RE::BSRenderPass;
        using Append_t = void (*)(Group*, Pass*, std::uint8_t);

        // Identical on SE 1.5.97, AE 1.6.1170, AE 1.7.104 and VR 1.4.15.
        inline constexpr std::uint8_t kExpectedPrologue[] = {
            0x40, 0x56, 0x57, 0x41, 0x56, 0x48, 0x83, 0xEC, 0x30, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF,
            0xFF, 0xFF, 0x48, 0x89, 0x5C, 0x24, 0x58, 0x48, 0x8B, 0xF2, 0x48, 0x8B, 0xF9, 0x44, 0x8B, 0x0D
        };

        inline constexpr std::uint8_t kLinkedGroupFlag = 0x1;

        inline SafetyHookInline           orig_Append;
        inline std::atomic<std::uint64_t> dropped{ 0 };

        // The group's own count is the number of pushes since it was last consumed, so it bounds the walk.
        inline bool IsQueued(const Group* a_group, const Pass* a_pass)
        {
            if (!a_group->passList.head) {
                return false;
            }
            if (a_group->passList.tail == a_pass) {
                return true;
            }

            std::size_t budget = a_group->count;
            for (const Pass* node = a_group->passList.head; node && budget; node = node->passGroupNext, --budget) {
                if (node == a_pass) {
                    return true;
                }
            }
            return false;
        }

        inline void Append(Group* a_group, Pass* a_pass, std::uint8_t a_front)
        {
            if (a_group && a_pass && (a_group->flags & kLinkedGroupFlag) && IsQueued(a_group, a_pass)) {
                const auto total = dropped.fetch_add(1, std::memory_order_relaxed) + 1;
                if ((total & (total - 1)) == 0) {
                    logger::warn("alpha pass list: dropped duplicate push of pass {} into linked group {} ({} queued); {} dropped so far"sv,
                        static_cast<const void*>(a_pass), static_cast<const void*>(a_group), a_group->count, total);
                }
                return;
            }
            orig_Append.call<void>(a_group, a_pass, a_front);
        }
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(100838, 107626) };  // GeometryGroup::AppendPass

        if (std::memcmp(reinterpret_cast<const void*>(target.address()), detail::kExpectedPrologue, sizeof(detail::kExpectedPrologue)) != 0) {
            logger::warn("alpha pass list duplicate push fix: unexpected bytes at {:X}, skipping"sv, target.address());
            return;
        }

        detail::orig_Append = safetyhook::create_inline(target.address(), detail::Append);

        EF_INSTALLED("AlphaPassListDuplicatePush"sv, "installed alpha pass list duplicate push fix"sv);
    }
}
