#pragma once

#include <algorithm>

namespace Fixes::KeyboardPollScancodeOOBCrash
{
    // Poll trusts dwOfs as an unchecked byte index into the 256-byte prevState/curState
    // arrays; a malformed dwOfs is an out-of-bounds access.
    namespace detail
    {
        inline bool SignatureMatchesFlat(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x44, 0x8B, 0x74, 0xC3, 0x78 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        inline bool SignatureMatchesVR(std::uintptr_t a_addr)
        {
            static constexpr std::uint8_t kExpected[] = { 0x44, 0x8B, 0xB4, 0xCB, 0x80, 0x00, 0x00, 0x00 };
            const auto*                   p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(std::begin(kExpected), std::end(kExpected), p);
        }

        struct DwOfsBoundsPatch final : Xbyak::CodeGenerator
        {
            DwOfsBoundsPatch(bool a_isVR, std::uintptr_t a_resume, std::uintptr_t a_skip)
            {
                if (a_isVR) {
                    mov(r14d, dword[rbx + rcx * 8 + 0x80]);
                } else {
                    mov(r14d, dword[rbx + rax * 8 + 0x78]);
                }
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

        // VR's extra BSIInputDevice field shifts diObjData from +0x78 to +0x80, forcing the
        // disp32 encoding (8-byte load) instead of flat's disp8 (5-byte load).
        detail::DwOfsBoundsPatch p{ isVR, patch.address() + (isVR ? 0x8 : 0x5), skip.address() };
        p.ready();
        patch.write_branch<5>(SKSE::GetTrampoline().allocate(p));

        logger::info("installed keyboard poll scancode out-of-bounds crash fix"sv);
    }
}
