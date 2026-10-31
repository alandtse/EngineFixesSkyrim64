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
        kMessageBox
    };

    inline constexpr std::size_t kMaxMenuCount = 128;

    template <class RoleAt>
    std::size_t SelectRenderSlot(std::size_t a_count, std::size_t a_current, RoleAt a_roleAt) noexcept
    {
        if (a_count > kMaxMenuCount || a_current >= a_count)
            return a_current;

        std::size_t main = a_count;
        std::size_t mist = a_count;
        std::size_t message = a_count;
        for (std::size_t i = 0; i < a_count; ++i) {
            const auto role = a_roleAt(i);
            auto*      found = role == MenuRole::kMain       ? &main :
                               role == MenuRole::kMist       ? &mist :
                               role == MenuRole::kMessageBox ? &message :
                                                               nullptr;
            if (found) {
                if (*found != a_count)
                    return a_current;
                *found = i;
            }
        }
        if (!(main < mist && mist < message && message < a_count))
            return a_current;

        for (std::size_t i = mist + 1; i <= message; ++i) {
            const auto role = a_roleAt(i);
            if (role != MenuRole::kProjected && role != MenuRole::kMessageBox)
                return a_current;
        }

        // Rotate only the render iterator: projected producers precede Mist's texture consumer.
        if (a_current >= mist && a_current < message)
            return a_current + 1;
        return a_current == message ? mist : a_current;
    }
}
