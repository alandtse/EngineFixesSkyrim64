#pragma once

#include <algorithm>
#include <optional>
#include <span>

namespace util
{
    // True from 1.7.99 on; AE codegen shifted offsets/ids starting here.
    inline bool IsAE1799()
    {
        return REL::Module::IsAtLeast(SKSE::RUNTIME_SSE_1_7_99);
    }

    // Per-version value for a guard whose offsets/bytes shift across AE recompiles.
    template <class T>
    struct VersionedValue
    {
        REL::Version minVersion;
        T            value;
    };

    // a_table must be sorted ascending by minVersion; returns the highest entry <= running version.
    template <class T, std::size_t N>
    [[nodiscard]] const T& SelectForVersion(const std::array<VersionedValue<T>, N>& a_table)
    {
        static_assert(N >= 1, "must provide at least 1 versioned value");
        const auto version = REL::Module::get().version();
        const T*   best = std::addressof(a_table.front().value);
        for (const auto& entry : a_table) {
            if (version >= entry.minVersion) {
                best = std::addressof(entry.value);
            } else {
                break;
            }
        }
        return *best;
    }

    // Main-module [base, end); used to validate a dispatched vtable pointer is in-module.
    inline std::pair<std::uintptr_t, std::uintptr_t> GetModuleImageBounds()
    {
        const auto  base = REL::Module::get().base();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        return { base, base + nt->OptionalHeader.SizeOfImage };
    }

    // Relocation-resilient fallback: finds a_pattern's one unique match in [a_base, a_end).
    inline std::optional<std::uintptr_t> FindUniqueSignature(
        std::span<const std::uint8_t> a_pattern, std::uintptr_t a_base, std::uintptr_t a_end)
    {
        if (a_pattern.empty() || a_end <= a_base || a_end - a_base < a_pattern.size()) {
            return std::nullopt;
        }

        const auto*                   begin = reinterpret_cast<const std::uint8_t*>(a_base);
        const auto*                   last = reinterpret_cast<const std::uint8_t*>(a_end) - a_pattern.size();
        std::optional<std::uintptr_t> found;
        for (const auto* p = begin; p <= last; ++p) {
            if (std::equal(a_pattern.begin(), a_pattern.end(), p)) {
                if (found) {
                    return std::nullopt;
                }
                found = reinterpret_cast<std::uintptr_t>(p);
            }
        }
        return found;
    }

    // Validates a slot against the general loaded-image range (not just this module); result in r11.
    inline void EmitLoadedSlotGuard(Xbyak::CodeGenerator& a_gen, const Xbyak::Address& a_slotAddr,
        const Xbyak::Label& a_invalid)
    {
        a_gen.mov(Xbyak::util::r11, a_slotAddr);
        a_gen.test(Xbyak::util::r11, Xbyak::util::r11);
        a_gen.jz(a_invalid);
        a_gen.mov(Xbyak::util::r10, 0x00007ff000000000ULL);
        a_gen.cmp(Xbyak::util::r11, Xbyak::util::r10);
        a_gen.jb(a_invalid);
        a_gen.mov(Xbyak::util::r10, 0x0000800000000000ULL);
        a_gen.cmp(Xbyak::util::r11, Xbyak::util::r10);
        a_gen.jae(a_invalid);
    }
}
