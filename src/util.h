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

    // Validates a loaded vtable slot against the general loaded-image address range rather
    // than this module specifically -- write_vfunc can legitimately point into the hooking
    // plugin's own DLL, not just the game executable. Result left in r11; jumps to
    // a_invalid on null or out-of-range.
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
