#pragma once

#include <cstdint>
#include <cstring>

// Guards the recursive scene-graph visitor (get-children dispatch CALL [RAX+0x18]) against
// a freed/null node's vftable during cell teardown. The visitor re-enters itself at every
// depth, so one guard at its entry (before `TEST RCX,RCX; JZ <exit>`) covers the whole
// subtree walk: valid vftable resumes the original prologue, null/freed takes the same
// pre-existing exit.

namespace Fixes::SceneGraphDetachFreedCrash
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t entryOffset;  // function entry: TEST RCX,RCX; JZ <rel32>
        };

        // sizeof(TEST RCX,RCX) + sizeof(JZ rel32) = 3 + (2 opcode + 4 rel32); resume = entry + 9.
        inline constexpr std::uintptr_t kPrologueLen = 9;

        inline constexpr Site kSiteVR{ 0xDFDCF0 };
        inline constexpr Site kSiteAE{ 0xE87DF0 };
        inline constexpr Site kSiteSE{ 0xDA8D70 };

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_exit)
            {
                Xbyak::Label exitLbl, resumeAddr, exitAddr;

                // Replicate the displaced null check.
                test(rcx, rcx);
                jz(exitLbl);

                // Validate the node's vftable lies inside the main module image.
                mov(rax, ptr[rcx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(exitLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(exitLbl);

                // Live node: resume the original prologue after the displaced bytes.
                jmp(ptr[rip + resumeAddr]);

                // Null or freed node: take the original clean exit (nothing pushed yet).
                L(exitLbl);
                jmp(ptr[rip + exitAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(exitAddr);
                dq(a_exit);
            }
        };
    }

    inline void Install()
    {
        const auto [moduleBase, moduleEnd] = util::GetModuleImageBounds();

        const auto& site = REL::Module::IsVR() ? detail::kSiteVR :
                           REL::Module::IsAE() ? detail::kSiteAE :
                                                 detail::kSiteSE;

        REL::Relocation<std::uintptr_t> entry{ REL::Offset{ site.entryOffset } };

        // Verify the displaced prologue is TEST RCX,RCX; JZ rel32 (48 85 C9 0F 84 ..)
        // before caving it; guards against offset drift corrupting the function entry.
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(entry.address());
        if (!(bytes[0] == 0x48 && bytes[1] == 0x85 && bytes[2] == 0xC9 &&
                bytes[3] == 0x0F && bytes[4] == 0x84)) {
            logger::warn("scene-graph detach crash fix: unexpected prologue at {:X}, skipping"sv,
                site.entryOffset);
            return;
        }

        // Decode the JZ rel32 (bytes 5-8) to derive the exit address directly from the
        // validated prologue, rather than trusting a separately-hardcoded offset.
        std::int32_t rel32;
        std::memcpy(&rel32, bytes + 5, sizeof(rel32));
        const auto exit = entry.address() + detail::kPrologueLen + rel32;

        detail::Patch p{ moduleBase, moduleEnd, entry.address() + detail::kPrologueLen, exit };
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        entry.write_branch<5>(trampoline.allocate(p));

        logger::info("installed scene-graph detach freed-object crash fix"sv);
    }
}
