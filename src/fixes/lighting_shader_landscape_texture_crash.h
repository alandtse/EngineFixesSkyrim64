#pragma once

namespace Fixes::LightingShaderLandscapeTextureCrash
{
    // AE inlines this landscape-blend helper into SetupMaterial (4 sites, resolved via
    // BSLightingShader's vtable so the fix survives AE point releases moving
    // SetupMaterial's absolute address); SE/VR call it standalone (id 100588).
    namespace detail
    {
        inline constexpr std::uintptr_t kDisplacedBytes = 0x7;

        inline bool SignatureMatches(std::uintptr_t a_addr, std::uint8_t a_modrm)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            // MOV dst,[src+0x48]; first byte (REX.W) of the following TEST dst,dst
            return p[0] == 0x48 && p[1] == 0x8B && p[2] == a_modrm && p[3] == 0x48 && p[4] == 0x48;
        }

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume, const Xbyak::Reg64& a_dst, const Xbyak::Reg64& a_src)
            {
                test(a_src, a_src);
                jz("null");
                mov(a_dst, qword[a_src + 0x48]);
                jmp("cont");
                L("null");
                xor_(a_dst.cvt32(), a_dst.cvt32());
                L("cont");
                test(a_dst, a_dst);
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };

        inline void InstallSite(REL::Relocation<std::uintptr_t> a_site, const Xbyak::Reg64& a_dst,
            const Xbyak::Reg64& a_src, SKSE::Trampoline& a_trampoline)
        {
            Patch p(a_site.address() + kDisplacedBytes, a_dst, a_src);
            p.ready();
            a_site.write_branch<5>(a_trampoline.allocate(p));
        }

        // AE: 4 sites inlined into BSLightingShader::SetupMaterial. SetupMaterial's own
        // absolute address moves every AE point release (differs between 1.6.1170 and
        // 1.7.99), but its internal layout -- and therefore these 4 patch sites' offsets
        // -- does not.
        inline void InstallAE()
        {
            REL::Relocation<std::uintptr_t> vtbl{ RE::BSLightingShader::VTABLE[0] };
            const auto                      setupMaterialAddr =
                *reinterpret_cast<std::uintptr_t*>(vtbl.address() + sizeof(void*) * 4);
            REL::Relocation<std::uintptr_t> setupMaterial{ setupMaterialAddr };
            auto&                           trampoline = SKSE::GetTrampoline();

            const bool ok = SignatureMatches(setupMaterial.address() + 0x2E8, 0x40) &&
                            SignatureMatches(setupMaterial.address() + 0x315, 0x40) &&
                            SignatureMatches(setupMaterial.address() + 0x36F, 0x48) &&
                            SignatureMatches(setupMaterial.address() + 0x3DE, 0x40);

            if (!ok) {
                logger::warn("lighting shader landscape texture crash fix: unexpected bytes at an AE patch site, skipping"sv);
                return;
            }

            InstallSite(REL::Relocation<std::uintptr_t>{ setupMaterial.address() + 0x2E8 }, Xbyak::util::rax,
                Xbyak::util::rax, trampoline);
            InstallSite(REL::Relocation<std::uintptr_t>{ setupMaterial.address() + 0x315 }, Xbyak::util::rax,
                Xbyak::util::rax, trampoline);
            InstallSite(REL::Relocation<std::uintptr_t>{ setupMaterial.address() + 0x36F }, Xbyak::util::rcx,
                Xbyak::util::rax, trampoline);
            InstallSite(REL::Relocation<std::uintptr_t>{ setupMaterial.address() + 0x3DE }, Xbyak::util::rax,
                Xbyak::util::rax, trampoline);

            logger::info("installed lighting shader landscape texture crash fix (ae)"sv);
        }

        // SE/VR: same 4 sites, but as a standalone function (address-library id 100588)
        // rather than inlined into SetupMaterial.
        inline void InstallStandalone()
        {
            REL::Relocation<std::uintptr_t> func{ REL::ID(100588) };
            auto&                           trampoline = SKSE::GetTrampoline();

            const bool ok = SignatureMatches(func.address() + 0x7, 0x40) &&
                            SignatureMatches(func.address() + 0x2F, 0x40) &&
                            SignatureMatches(func.address() + 0x7E, 0x48) &&
                            SignatureMatches(func.address() + 0xE6, 0x40);

            if (!ok) {
                logger::warn("lighting shader landscape texture crash fix: unexpected bytes at a standalone patch site, skipping"sv);
                return;
            }

            InstallSite(REL::Relocation<std::uintptr_t>{ REL::ID(100588), 0x7 }, Xbyak::util::rax, Xbyak::util::rax,
                trampoline);
            InstallSite(REL::Relocation<std::uintptr_t>{ REL::ID(100588), 0x2F }, Xbyak::util::rax, Xbyak::util::rax,
                trampoline);
            InstallSite(REL::Relocation<std::uintptr_t>{ REL::ID(100588), 0x7E }, Xbyak::util::rcx, Xbyak::util::rax,
                trampoline);
            InstallSite(REL::Relocation<std::uintptr_t>{ REL::ID(100588), 0xE6 }, Xbyak::util::rax, Xbyak::util::rax,
                trampoline);

            logger::info("installed lighting shader landscape texture crash fix (se/vr)"sv);
        }
    }

    inline void Install()
    {
        if (REL::Module::IsAE()) {
            detail::InstallAE();
        } else {
            detail::InstallStandalone();
        }
    }
}
