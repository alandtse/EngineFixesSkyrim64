#pragma once

#include <array>
#include <cstdint>

namespace Fixes::EffectShaderDataNullTextureCrash
{
    // BSEffectShader::sub_SE100748_AE107529 reads a BSShaderProperty's BSEffectShaderData
    // base/palette texture pointers and dereferences their +0x48 renderer-data field without
    // null-checking the pointer first. Creation Kit supports color-fill-only effects with no
    // texture assigned, so either pointer may be null and the unguarded load faults. The
    // following TEST RAX,RAX already treats a null result as "no resource view bound", so
    // null-checking the pointer and leaving RAX = 0 is the correct, minimal fix.
    namespace detail
    {
        // Per-runtime RVAs (relative to the module base) of the two patch sites.
        struct RuntimeInfo
        {
            std::uintptr_t baseTextureSite;     // MOV RAX,[RSI+0x10]
            std::uintptr_t paletteTextureSite;  // MOV RAX,[RSI+0x18]
        };

        inline constexpr RuntimeInfo kSE{ 0x130084F, 0x13008AD };
        inline constexpr RuntimeInfo kAE1170{ 0x14EBDAA, 0x14EBE08 };
        inline constexpr RuntimeInfo kAE1799{ 0x15580CA, 0x1558128 };
        inline constexpr RuntimeInfo kVR{ 0x13430DD, 0x134314C };

        // Displaced bytes at each site. Site 1 (base texture) and site 2 (palette texture)
        // differ only in the field offset (0x10 vs 0x18). VR's site 1 has an interleaved
        // MOV RBP,[RSP+0xA8] the compiler scheduled between the two loads.
        inline constexpr std::array<std::uint8_t, 8>  kBaseTextureBytes{ 0x48, 0x8B, 0x46, 0x10, 0x48, 0x8B, 0x40, 0x48 };
        inline constexpr std::array<std::uint8_t, 8>  kPaletteTextureBytes{ 0x48, 0x8B, 0x46, 0x18, 0x48, 0x8B, 0x40, 0x48 };
        inline constexpr std::array<std::uint8_t, 16> kVRBaseTextureBytes{ 0x48, 0x8B, 0x46, 0x10, 0x48, 0x8B, 0xAC, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x40, 0x48 };

        inline bool BytesMatch(std::uintptr_t a_addr, const std::uint8_t* a_expected, std::size_t a_len)
        {
            return std::equal(a_expected, a_expected + a_len, reinterpret_cast<const std::uint8_t*>(a_addr));
        }

        struct Patch final : Xbyak::CodeGenerator
        {
            // Null-checks the texture pointer before the +0x48 load; a null texture leaves RAX = 0,
            // which the resume target's own TEST RAX,RAX already treats as "no resource view".
            // a_vrInterleave replicates the VR-scheduled MOV RBP,[RSP+0xA8] between the two loads.
            Patch(std::uintptr_t a_resume, std::uint32_t a_fieldOffset, bool a_vrInterleave)
            {
                Xbyak::Label skip;
                mov(rax, ptr[rsi + a_fieldOffset]);
                if (a_vrInterleave) {
                    mov(rbp, ptr[rsp + 0xA8]);
                }
                test(rax, rax);
                jz(skip);
                mov(rax, ptr[rax + 0x48]);
                L(skip);
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };

        inline void InstallSite(std::uintptr_t a_site, const std::uint8_t* a_expected, std::size_t a_len,
            std::uint32_t a_fieldOffset, bool a_vrInterleave, SKSE::Trampoline& a_trampoline)
        {
            if (!BytesMatch(a_site, a_expected, a_len)) {
                logger::warn("effect shader data null texture crash fix: unexpected bytes at patch site, skipping"sv);
                return;
            }

            Patch p(a_site + a_len, a_fieldOffset, a_vrInterleave);
            p.ready();
            REL::Relocation<std::uintptr_t> target{ a_site };
            target.write_branch<5>(a_trampoline.allocate(p));
        }

        inline void InstallForRuntime(const RuntimeInfo& a_info, bool a_isVR)
        {
            auto&      trampoline = SKSE::GetTrampoline();
            const auto base = REL::Module::get().base();

            // Site 1: base texture (always executes).
            if (a_isVR) {
                InstallSite(base + a_info.baseTextureSite, kVRBaseTextureBytes.data(), kVRBaseTextureBytes.size(),
                    0x10, true, trampoline);
            } else {
                InstallSite(base + a_info.baseTextureSite, kBaseTextureBytes.data(), kBaseTextureBytes.size(),
                    0x10, false, trampoline);
            }

            // Site 2: palette texture (flag-gated; byte-identical in all runtimes, no interleave).
            InstallSite(base + a_info.paletteTextureSite, kPaletteTextureBytes.data(), kPaletteTextureBytes.size(),
                0x18, false, trampoline);

            logger::info("installed effect shader data null texture crash fix"sv);
        }
    }

    inline void Install()
    {
        const auto version = REL::Module::get().version();
        if (version == SKSE::RUNTIME_SSE_1_5_97) {
            detail::InstallForRuntime(detail::kSE, false);
        } else if (version == SKSE::RUNTIME_SSE_1_6_1170) {
            detail::InstallForRuntime(detail::kAE1170, false);
        } else if (version == SKSE::RUNTIME_SSE_1_7_99) {
            detail::InstallForRuntime(detail::kAE1799, false);
        } else if (version == SKSE::RUNTIME_VR_1_4_15) {
            detail::InstallForRuntime(detail::kVR, true);
        }
    }
}
