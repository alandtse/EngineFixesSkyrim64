#pragma once

namespace util
{
    // Main-module image bounds [base, end). Used by the freed-object crash guards to
    // validate that a dispatched vtable pointer lies inside the executable's .rdata
    // (a live vftable is in-module; a freed object's is null or heap garbage).
    inline std::pair<std::uintptr_t, std::uintptr_t> GetModuleImageBounds()
    {
        const auto  base = REL::Module::get().base();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        return { base, base + nt->OptionalHeader.SizeOfImage };
    }

    // .text section bounds [base, end): use this to validate a call TARGET, not a
    // vtable pointer -- GetModuleImageBounds() also admits .rdata/.data, which are
    // not callable.
    inline std::pair<std::uintptr_t, std::uintptr_t> GetTextSectionBounds()
    {
        const auto  base = REL::Module::get().base();
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (std::uint16_t i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
            const auto& sec = sections[i];
            if (std::memcmp(sec.Name, ".text", 5) == 0) {
                const auto start = base + sec.VirtualAddress;
                const auto size = std::max<std::uint32_t>(sec.Misc.VirtualSize, sec.SizeOfRawData);
                return { start, start + size };
            }
        }
        return GetModuleImageBounds();
    }
}
