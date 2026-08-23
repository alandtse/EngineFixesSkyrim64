#pragma once

#include <array>
#include <cstdint>

// BSSubIndexTriShape::CreateFromShapeData allocates the shape object, then uses the
// result completely unguarded: a segment-population loop, an unconditional finalize
// call, a virtual call through its vtable, and a shaderProperty field write. All four
// dereference the pointer. The allocator can return null under memory pressure (e.g.
// heavy LOD streaming), and nothing here checks for it. CreateFromShapeData's own
// caller already tolerates a null return (it has a fallback-allocate path), so
// returning null cleanly here is a safe, non-crashing outcome.
namespace Fixes::SubIndexTriShapeCreateNullCrash
{
    namespace detail
    {
        struct RuntimeInfo
        {
            REL::Version   version;
            std::uintptr_t hookRva;  // start of the displaced `MOV reg,RAX; MOV EBX,R12D` pair
            std::uintptr_t resumeRva;
            std::uintptr_t epilogueRva;  // shared epilogue's `ADD RSP,frameSize`
            bool           isRDI;        // true = RDI holds the shape pointer here, false = RSI
        };

        inline constexpr RuntimeInfo kRuntimes[] = {
            { SKSE::RUNTIME_SSE_1_5_97, 0xD7AA64, 0xD7AA6A, 0xD7AC78, true },
            { SKSE::RUNTIME_SSE_1_6_1170, 0xE55B24, 0xE55B2A, 0xE55D38, false },
            { SKSE::RUNTIME_SSE_1_7_99, 0x101B1A4, 0x101B1AA, 0x101B3B8, false },
            { SKSE::RUNTIME_VR_1_4_15, 0xDCE417, 0xDCE41D, 0xDCE6F6, true },
        };

        // MOV reg,RAX (48 8B /r) + MOV EBX,R12D (41 8B DC -- REX.B, R12D is the source).
        inline std::array<std::uint8_t, 6> ExpectedBytes(bool a_isRDI)
        {
            return a_isRDI ? std::array<std::uint8_t, 6>{ 0x48, 0x8B, 0xF8, 0x41, 0x8B, 0xDC } :
                             std::array<std::uint8_t, 6>{ 0x48, 0x8B, 0xF0, 0x41, 0x8B, 0xDC };
        }

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume, std::uintptr_t a_epilogue, const Xbyak::Reg64& a_reg)
            {
                Xbyak::Label nullLbl, resumeAddr, epilogueAddr;

                // RAX holds Allocate()'s return here -- `a_reg` hasn't been assigned yet,
                // that's the displaced instruction this patch replaces.
                test(rax, rax);
                jz(nullLbl);

                mov(a_reg, rax);  // replicate displaced MOV reg,RAX
                mov(ebx, r12d);   // replicate displaced MOV EBX,R12D
                jmp(ptr[rip + resumeAddr]);

                L(nullLbl);
                // RAX is already 0 here (that's why we branched); jumping straight into
                // the epilogue's ADD RSP is correct without touching any other register --
                // the callee-saved pops that follow restore them from the stack regardless
                // of what CreateFromShapeData did to them before this point.
                jmp(ptr[rip + epilogueAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(epilogueAddr);
                dq(a_epilogue);
            }
        };

        inline void InstallForRuntime(const RuntimeInfo& a_info)
        {
            REL::Relocation<std::uintptr_t> hook{ REL::Offset{ a_info.hookRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(hook.address());
            const auto                      expected = ExpectedBytes(a_info.isRDI);
            if (!std::equal(expected.begin(), expected.end(), bytes)) {
                logger::warn("skipping SubIndexTriShapeCreateNullCrash guard: unexpected bytes at {:X}"sv, a_info.hookRva);
                return;
            }

            const Xbyak::Reg64 reg = a_info.isRDI ? Xbyak::util::rdi : Xbyak::util::rsi;
            Patch              p{
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_info.resumeRva } }.address(),
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_info.epilogueRva } }.address(),
                reg
            };
            p.ready();
            hook.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed SubIndexTriShapeCreateNullCrash guard"sv);
        }
    }

    inline void Install()
    {
        const auto currentVersion = REL::Module::get().version();
        for (const auto& info : detail::kRuntimes) {
            if (currentVersion == info.version) {
                detail::InstallForRuntime(info);
                return;
            }
        }
        logger::warn("skipping SubIndexTriShapeCreateNullCrash guard: unsupported runtime"sv);
    }
}
