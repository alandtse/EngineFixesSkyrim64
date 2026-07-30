#pragma once

namespace Fixes::LightingShaderLandscapeTextureCrash
{
    // AE-only: the same texture-pointer-deref-before-null-check bug as
    // LightingShaderNullTextureCrash, but AE's compiler separately inlined a
    // distinct landscape-blend helper into SetupMaterial with 4 more occurrences
    // of it. See PR description for site addresses and reachability rationale.
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
    }

    inline void Install()
    {
        if (!REL::Module::IsAE()) {
            return;  // this inlining only happens in AE's build of SetupMaterial
        }

        REL::Relocation<std::uintptr_t> setupMaterial{ REL::Offset{ 0x14DC310 } };
        auto&                           trampoline = SKSE::GetTrampoline();

        // base layer: diffuseTexture (+0x48) and normalTexture (+0x58), both MOV RAX,[RAX+0x48];
        // additional layers: landscapeDiffuseTexture[i] (MOV RCX,[RAX+0x48]) and
        // landscapeNormalTexture[i] (MOV RAX,[RAX+0x48]), inside the per-layer loop.
        const bool ok = detail::SignatureMatches(setupMaterial.address() + 0x2E8, 0x40) &&
                        detail::SignatureMatches(setupMaterial.address() + 0x315, 0x40) &&
                        detail::SignatureMatches(setupMaterial.address() + 0x36F, 0x48) &&
                        detail::SignatureMatches(setupMaterial.address() + 0x3DE, 0x40);

        if (!ok) {
            logger::warn("lighting shader landscape texture crash fix: unexpected bytes at a patch site, skipping"sv);
            return;
        }

        REL::Relocation<std::uintptr_t> diffuseBase{ REL::Offset{ 0x14DC310 + 0x2E8 } };
        detail::Patch                   pDiffuseBase(diffuseBase.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
        pDiffuseBase.ready();
        diffuseBase.write_branch<5>(trampoline.allocate(pDiffuseBase));

        REL::Relocation<std::uintptr_t> normalBase{ REL::Offset{ 0x14DC310 + 0x315 } };
        detail::Patch                   pNormalBase(normalBase.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
        pNormalBase.ready();
        normalBase.write_branch<5>(trampoline.allocate(pNormalBase));

        REL::Relocation<std::uintptr_t> diffuseLayer{ REL::Offset{ 0x14DC310 + 0x36F } };
        detail::Patch                   pDiffuseLayer(diffuseLayer.address() + 0x7, Xbyak::util::rcx, Xbyak::util::rax);
        pDiffuseLayer.ready();
        diffuseLayer.write_branch<5>(trampoline.allocate(pDiffuseLayer));

        REL::Relocation<std::uintptr_t> normalLayer{ REL::Offset{ 0x14DC310 + 0x3DE } };
        detail::Patch                   pNormalLayer(normalLayer.address() + 0x7, Xbyak::util::rax, Xbyak::util::rax);
        pNormalLayer.ready();
        normalLayer.write_branch<5>(trampoline.allocate(pNormalLayer));

        logger::info("installed lighting shader landscape texture crash fix"sv);
    }
}
