// Standalone: c++ -std=c++20 -O2 tests/projected_menu_order.cpp -o projected_menu_order
#include "../src/fixes/detail/projected_menu_order.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <numeric>
#include <vector>

using namespace Fixes::MessageBoxMainMenu::detail;

static std::vector<std::size_t> Order(const std::vector<MenuRole>& a_roles)
{
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < a_roles.size(); ++i)
        result.push_back(SelectRenderSlot(a_roles.size(), i, [&](std::size_t j) { return a_roles[j]; }));
    return result;
}

int main()
{
    using enum MenuRole;
    assert((Order({ kMain, kMist, kMessageBox }) == std::vector<std::size_t>{ 0, 2, 1 }));
    assert((Order({ kMain, kMist, kProjected, kMessageBox, kProjected }) == std::vector<std::size_t>{ 0, 2, 3, 1, 4 }));
    assert((Order({ kMain, kMessageBox, kMist }) == std::vector<std::size_t>{ 0, 1, 2 }));
    assert((Order({ kMist, kMessageBox }) == std::vector<std::size_t>{ 0, 1 }));
    assert((Order({ kMain, kMist, kBlocked, kMessageBox }) == std::vector<std::size_t>{ 0, 1, 2, 3 }));
    assert((Order({ kMain, kMist, kMessageBox, kBlocked }) == std::vector<std::size_t>{ 0, 2, 1, 3 }));
    std::size_t cases = 0;
    for (std::size_t count = 0, combinations = 1; count <= 7; ++count, combinations *= 5) {
        for (std::size_t code = 0; code < combinations; ++code) {
            std::vector<MenuRole> roles(count);
            auto                  digits = code;
            for (auto& role : roles) {
                role = static_cast<MenuRole>(digits % 5);
                digits /= 5;
            }
            const auto original = roles;
            const auto order = Order(roles);
            auto       sorted = order;
            std::sort(sorted.begin(), sorted.end());
            for (std::size_t i = 0; i < count; ++i)
                assert(sorted[i] == i);  // every slot exactly once, even with null/unsafe entries
            assert(roles == original);   // input stack stays unchanged
            // Topmost projected producers are blocked, so native begin selection
            // cannot split a valid rotation. Check every remaining possible boundary.
            for (std::size_t start = 0; start < count; ++start) {
                if (start != 0 && (roles[start] == kProjected || roles[start] == kMessageBox))
                    continue;
                std::vector<std::size_t> suffix(order.begin() + start, order.end());
                std::sort(suffix.begin(), suffix.end());
                for (std::size_t i = 0; i < suffix.size(); ++i)
                    assert(suffix[i] == start + i);
            }

            std::vector<std::size_t> expected(count);
            std::iota(expected.begin(), expected.end(), 0);
            if (std::count(roles.begin(), roles.end(), kMain) == 1 &&
                std::count(roles.begin(), roles.end(), kMist) == 1 &&
                std::count(roles.begin(), roles.end(), kMessageBox) == 1) {
                const auto main = std::find(roles.begin(), roles.end(), kMain);
                const auto mist = std::find(roles.begin(), roles.end(), kMist);
                const auto box = std::find(roles.begin(), roles.end(), kMessageBox);
                if (main < mist && mist < box &&
                    std::all_of(mist + 1, box, [](MenuRole r) { return r == kProjected; })) {
                    const auto begin = std::distance(roles.begin(), mist);
                    const auto end = std::distance(roles.begin(), box) + 1;
                    std::rotate(expected.begin() + begin, expected.begin() + begin + 1, expected.begin() + end);
                }
            }
            assert(order == expected);
            assert(SelectRenderSlot(count, count, [](std::size_t) { assert(false); return kBlocked; }) == count);
            ++cases;
        }
    }
    assert(SelectRenderSlot(kMaxMenuCount + 1, 0, [](std::size_t) { assert(false); return kBlocked; }) == 0);
    std::cout << "PASS: " << cases << " exhaustive stacks; permutation, safety barriers, bounds, input immutability\n";
}
