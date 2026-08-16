#pragma once

namespace Fixes::LightingShaderNullTextureCrash
{
    // SetupTexture dereferences its texture-pointer arg's +0x48 field before null-checking
    // the pointer itself; some SetupMaterial call sites pass it unguarded.
    namespace detail
    {
        inline constexpr std::uintptr_t kDisplacedBytes = 0x8;

        inline bool SignatureMatches(std::uintptr_t a_addr)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            // MOV RAX,[RDX+0x48]; (MOV EDX,[R8+0x70] begins with 0x41)
            return p[0] == 0x48 && p[1] == 0x8B && p[2] == 0x42 && p[3] == 0x48 && p[4] == 0x41;
        }

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume)
            {
                test(rdx, rdx);
                jz("null");
                mov(rax, qword[rdx + 0x48]);  // re-run displaced load: rax = a_texture->rendererData
                jmp("cont");
                L("null");
                xor_(eax, eax);
                L("cont");
                mov(edx, dword[r8 + 0x70]);  // re-run displaced load: edx = a_material->textureClampMode
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };
    }

    inline void Install()
    {
        REL::Relocation<std::uintptr_t> target{ RELOCATION_ID(100587, 107329) };

        if (detail::SignatureMatches(target.address())) {
            detail::Patch p(target.address() + detail::kDisplacedBytes);
            p.ready();
            auto& trampoline = SKSE::GetTrampoline();
            target.write_branch<5>(trampoline.allocate(p));

            logger::info("installed lighting shader null texture crash fix"sv);
        } else {
            logger::warn("lighting shader null texture crash fix: unexpected bytes at patch site, skipping"sv);
        }
    }
}
