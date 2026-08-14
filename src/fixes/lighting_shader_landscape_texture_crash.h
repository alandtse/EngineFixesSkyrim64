#pragma once

namespace Fixes::LightingShaderLandscapeTextureCrash
{
    // Same texture-pointer-deref-before-null-check bug as LightingShaderNullTextureCrash,
    // present in all three runtimes. AE's compiler inlines the landscape-blend helper into
    // BSLightingShader::SetupMaterial (4 sites, patched below via raw offsets into that
    // function). SE/VR keep it as a standalone function (address-library id 100588, 4 sites
    // at its own entry+0x7/0x2F/0x7E/0xE6).
    namespace detail
    {
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
                mov(a_dst, qword[a_src + 0x48]);  // re-run displaced load
                jmp("cont");
                L("null");
                xor_(a_dst.cvt32(), a_dst.cvt32());
                L("cont");
                test(a_dst, a_dst);  // re-run borrowed byte's instruction
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };

        // AE: 4 sites inlined into BSLightingShader::SetupMaterial.
        inline void InstallAE()
        {
            REL::Relocation<std::uintptr_t> setupMaterial{ REL::Offset{ 0x14DC310 } };
            auto&                           trampoline = SKSE::GetTrampoline();

            // base layer: diffuseTexture (+0x48) and normalTexture (+0x58), both MOV RAX,[RAX+0x48];
            // additional layers: landscapeDiffuseTexture[i] (MOV RCX,[RAX+0x48]) and
            // landscapeNormalTexture[i] (MOV RAX,[RAX+0x48]), inside the per-layer loop.
            const bool ok = SignatureMatches(setupMaterial.address() + 0x2E8, 0x40) &&
                            SignatureMatches(setupMaterial.address() + 0x315, 0x40) &&
                            SignatureMatches(setupMaterial.address() + 0x36F, 0x48) &&
                            SignatureMatches(setupMaterial.address() + 0x3DE, 0x40);

            if (!ok) {
                logger::warn("lighting shader landscape texture crash fix: unexpected bytes at an AE patch site, skipping"sv);
                return;
            }

            REL::Relocation<std::uintptr_t> diffuseBase{ REL::Offset{ 0x14DC310 + 0x2E8 } };
            Patch                           pDiffuseBase(diffuseBase.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
            pDiffuseBase.ready();
            diffuseBase.write_branch<5>(trampoline.allocate(pDiffuseBase));

            REL::Relocation<std::uintptr_t> normalBase{ REL::Offset{ 0x14DC310 + 0x315 } };
            Patch                           pNormalBase(normalBase.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
            pNormalBase.ready();
            normalBase.write_branch<5>(trampoline.allocate(pNormalBase));

            REL::Relocation<std::uintptr_t> diffuseLayer{ REL::Offset{ 0x14DC310 + 0x36F } };
            Patch                           pDiffuseLayer(diffuseLayer.address() + 0x7, Xbyak::util::rcx, Xbyak::util::rax);
            pDiffuseLayer.ready();
            diffuseLayer.write_branch<5>(trampoline.allocate(pDiffuseLayer));

            REL::Relocation<std::uintptr_t> normalLayer{ REL::Offset{ 0x14DC310 + 0x3DE } };
            Patch                           pNormalLayer(normalLayer.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
            pNormalLayer.ready();
            normalLayer.write_branch<5>(trampoline.allocate(pNormalLayer));

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

            REL::Relocation<std::uintptr_t> diffuseBase{ REL::ID(100588), 0x7 };
            Patch                           pDiffuseBase(diffuseBase.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
            pDiffuseBase.ready();
            diffuseBase.write_branch<5>(trampoline.allocate(pDiffuseBase));

            REL::Relocation<std::uintptr_t> normalBase{ REL::ID(100588), 0x2F };
            Patch                           pNormalBase(normalBase.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
            pNormalBase.ready();
            normalBase.write_branch<5>(trampoline.allocate(pNormalBase));

            REL::Relocation<std::uintptr_t> diffuseLayer{ REL::ID(100588), 0x7E };
            Patch                           pDiffuseLayer(diffuseLayer.address() + 0x7, Xbyak::util::rcx, Xbyak::util::rax);
            pDiffuseLayer.ready();
            diffuseLayer.write_branch<5>(trampoline.allocate(pDiffuseLayer));

            REL::Relocation<std::uintptr_t> normalLayer{ REL::ID(100588), 0xE6 };
            Patch                           pNormalLayer(normalLayer.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
            pNormalLayer.ready();
            normalLayer.write_branch<5>(trampoline.allocate(pNormalLayer));

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
