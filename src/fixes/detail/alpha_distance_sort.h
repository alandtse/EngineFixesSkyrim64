#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Fixes::AlphaDistanceSort::policy
{
    struct Position
    {
        float x, y, z;
    };

    /** Classify float bits even under fast floating-point optimization. */
    [[nodiscard]] inline constexpr bool IsFinite(float value) noexcept
    {
        return (std::bit_cast<std::uint32_t>(value) & 0x7F800000u) != 0x7F800000u;
    }

    inline bool IsFinite(Position position) noexcept
    {
        return IsFinite(position.x) && IsFinite(position.y) && IsFinite(position.z);
    }

    inline float ProjectedDepth(Position centre, Position direction) noexcept
    {
        return centre.y * direction.y + centre.x * direction.x + centre.z * direction.z;
    }

    inline float ProjectedGroupKey(Position centre, float radius, Position direction, bool closest) noexcept
    {
        const auto depth = ProjectedDepth(centre, direction);
        // The native group site does not read radius when closest is false.
        return closest ? depth - radius : depth;
    }

    // Zero and subnormal keys form one stable tie with either denormal-handling mode.
    inline std::uint32_t SortKeyBits(float value) noexcept
    {
        const auto bits = std::bit_cast<std::uint32_t>(value);
        return (bits & 0x7F800000u) == 0 ? 0u : bits;
    }

    /** Rotation-independent, finite key; signed squaring preserves nearest-bound ordering. */
    inline float DistanceKey(Position centre, float radius, Position origin, float closest) noexcept
    {
        if (!IsFinite(centre) || !IsFinite(origin) || !IsFinite(radius) || radius < 0 || !IsFinite(closest))
            return 0;
        const double x = double(centre.x) - origin.x;
        const double y = double(centre.y) - origin.y;
        const double z = double(centre.z) - origin.z;
        double       key = x * x + y * y + z * z;
        if (closest != 0) {
            const double distance = std::sqrt(key) - double(closest) * radius;
            key = std::copysign(distance * distance, distance);
        }
        // Keep every key above the native empty-queue sentinel, including extreme bounds.
        constexpr double limit = (std::numeric_limits<float>::max)() / 4.0;
        return std::bit_cast<float>(SortKeyBits(static_cast<float>(std::clamp(key, -limit, limit))));
    }

    /** Stable descending finite-key sort; bounded small-list work, otherwise four linear walks.
     * The caller owns an acyclic list and keeps keys unchanged throughout sorting.
     */
    template <class Pass, class Key>
    void Sort(Pass*& head, Key key) noexcept
    {
        if (!head || !head->passGroupNext)
            return;
        // Small groups should not pay for four 256-bucket scans. Larger lists remain unbounded.
        constexpr std::size_t smallCapacity = 32;
        struct Entry
        {
            Pass* pass;
            float depth;
        };
        std::array<Entry, smallCapacity> entries;
        entries[0].pass = head;
        std::size_t count = 1;
        auto*       remaining = head->passGroupNext;
        while (remaining && count < entries.size()) {
            entries[count++].pass = remaining;
            remaining = remaining->passGroupNext;
        }
        if (!remaining) {
            for (std::size_t i = 0; i < count; ++i) {
                const Entry entry{ entries[i].pass, std::bit_cast<float>(SortKeyBits(key(entries[i].pass))) };
                auto        position = i;
                while (position && entry.depth > entries[position - 1].depth) {
                    entries[position] = entries[position - 1];
                    --position;
                }
                entries[position] = entry;
            }
            head = entries[0].pass;
            for (std::size_t i = 1; i < count; ++i)
                entries[i - 1].pass->passGroupNext = entries[i].pass;
            entries[count - 1].pass->passGroupNext = nullptr;
            return;
        }
        struct Bucket
        {
            Pass* head{};
            Pass* tail{};
        };
        for (unsigned shift = 0; shift < 32; shift += 8) {
            std::array<Bucket, 256> buckets{};
            for (auto* pass = head; pass;) {
                auto*      next = pass->passGroupNext;
                const auto bits = SortKeyBits(key(pass));
                const auto ordered = bits ^ (bits & 0x80000000u ? 0xFFFFFFFFu : 0x80000000u);
                auto&      bucket = buckets[(~ordered >> shift) & 0xFFu];
                if (bucket.tail)
                    bucket.tail->passGroupNext = pass;
                else
                    bucket.head = pass;
                bucket.tail = pass;
                pass = next;
            }
            Pass** link = &head;
            for (const auto& bucket : buckets) {
                if (!bucket.head)
                    continue;
                *link = bucket.head;
                link = &bucket.tail->passGroupNext;
            }
            *link = nullptr;
        }
    }
}
