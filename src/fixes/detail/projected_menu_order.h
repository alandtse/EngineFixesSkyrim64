#pragma once

#include <cstddef>

namespace Fixes::MessageBoxMainMenu::detail
{
    enum class MenuRole
    {
        kBlocked,
        kProjected,
        kMain,
        kMist,
        kMessageBox,
        kConsole
    };

    inline constexpr std::size_t kMaxMenuCount = 128;

    template <class RoleAt>
    std::size_t SelectRenderSlot(std::size_t a_count, std::size_t a_current, RoleAt a_roleAt) noexcept
    {
        if (a_count > kMaxMenuCount || a_current >= a_count)
            return a_current;

        std::size_t main = a_count;
        std::size_t mist = a_count;
        std::size_t last = a_count;
        for (std::size_t i = 0; i < a_count; ++i) {
            const auto role = a_roleAt(i);
            if (role == MenuRole::kMessageBox || role == MenuRole::kConsole) {
                last = i;
                continue;
            }
            auto* found = role == MenuRole::kMain ? &main : role == MenuRole::kMist ? &mist :
                                                                                      nullptr;
            if (found) {
                if (*found != a_count)
                    return a_current;
                *found = i;
            }
        }
        if (!(main < mist && mist < last && last < a_count))
            return a_current;

        for (std::size_t i = mist + 1; i <= last; ++i) {
            const auto role = a_roleAt(i);
            if (role != MenuRole::kProjected && role != MenuRole::kMessageBox && role != MenuRole::kConsole)
                return a_current;
        }

        // Rotate only the render iterator: projected producers precede Mist's texture consumer.
        if (a_current >= mist && a_current < last)
            return a_current + 1;
        return a_current == last ? mist : a_current;
    }
}
