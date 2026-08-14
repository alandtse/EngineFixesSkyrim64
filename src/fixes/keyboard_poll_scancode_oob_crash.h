#pragma once

#include <algorithm>

namespace Fixes::KeyboardPollScancodeOOBCrash
{
    // Poll trusts dwOfs (from DirectInput) as an unchecked byte index into the 256-byte
    // prevState/curState arrays; a malformed dwOfs causes an out-of-bounds access. VR's extra
    // BSIInputDevice field shifts diObjData from +0x78 to +0x80, changing the load's displacement
    // encoding from disp8 to disp32 (5 vs 8 bytes), hence the separate Flat/VR patch variants.
    namespace detail
    {
        // Full 5-byte flat (SE/AE) block: MOV R14D,[RBX+RAX*8+0x78]. REX.R (0x44)
        // selects r14 as the reg field; disp8 fits since 0x78 <= 0x7F.
        inline bool SignatureMatchesFlat(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x44, 0x8B, 0x74, 0xC3, 0x78 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        // Full 8-byte VR block: MOV R14D,[RBX+RCX*8+0x80]. Displacement 0x80
        // overflows imm8's signed range, forcing the disp32 encoding (mod=10)
        // instead of flat's disp8 (mod=01) -- the extra 3 bytes noted above.
        inline bool SignatureMatchesVR(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x44, 0x8B, 0xB4, 0xCB, 0x80, 0x00, 0x00, 0x00 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        struct FlatPatch final : Xbyak::CodeGenerator
        {
            FlatPatch(std::uintptr_t a_resume, std::uintptr_t a_skip)
            {
                mov(r14d, dword[rbx + rax * 8 + 0x78]);  // re-run displaced load: r14d = diObjData[i].dwOfs
                cmp(r14d, 0x100);
                jae("invalid");
                jmp(ptr[rip]);
                dq(a_resume);
                L("invalid");
                jmp(ptr[rip]);
                dq(a_skip);
            }
        };

        struct VRPatch final : Xbyak::CodeGenerator
        {
            VRPatch(std::uintptr_t a_resume, std::uintptr_t a_skip)
            {
                mov(r14d, dword[rbx + rcx * 8 + 0x80]);  // re-run displaced load: r14d = diObjData[i].dwOfs
                cmp(r14d, 0x100);
                jae("invalid");
                jmp(ptr[rip]);
                dq(a_resume);
                L("invalid");
                jmp(ptr[rip]);
                dq(a_skip);
            }
        };
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(67472, 68782), VAR_NUM(0xDB, 0xDC, 0xE0) };
        REL::Relocation<std::uintptr_t> skip{ RELOCATION_ID(67472, 68782), VAR_NUM(0x232, 0x2F0, 0x1D9) };

        const bool isVR = REL::Module::IsVR();
        if (isVR ? !detail::SignatureMatchesVR(patch.address()) : !detail::SignatureMatchesFlat(patch.address())) {
            logger::warn("keyboard poll scancode out-of-bounds crash fix: unexpected bytes at patch site, skipping"sv);
            return;
        }

        auto& trampoline = SKSE::GetTrampoline();

        if (isVR) {
            detail::VRPatch p(patch.address() + 0x8, skip.address());
            p.ready();
            patch.write_branch<5>(trampoline.allocate(p));
        } else {
            detail::FlatPatch p(patch.address() + 0x5, skip.address());
            p.ready();
            patch.write_branch<5>(trampoline.allocate(p));
        }

        logger::info("installed keyboard poll scancode out-of-bounds crash fix"sv);
    }
}
