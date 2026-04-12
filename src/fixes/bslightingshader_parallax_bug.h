#pragma once
namespace Fixes::BSLightingShaderParallaxBug
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(const std::uintptr_t a_target)
            {
                if (REL::Module::IsAE()) {
                    // orig code
                    test(eax, 0x21C00);
                    mov(r9d, 1);
                    cmovnz(ecx, r9d);

                    // new code
                    cmp(dword[rbp + 0x1D0 - 0x210], static_cast<std::uint32_t>(RE::BSShaderMaterial::Feature::kParallax));
                    cmovz(ecx, r9d);                       // set eye update true

                    // jmp out
                    jmp(ptr[rip]);
                    dq(a_target + 0xF);
                } else {
                    // orig code
                    and_(eax, 0x21C00);
                    cmovnz(edx, r8d);

                    // new code: technique register differs — r14d in VR, ebx in SE
                    if (REL::Module::IsVR())
                        cmp(r14d, static_cast<std::uint32_t>(RE::BSShaderMaterial::Feature::kParallax));  // VR: technique ID in r14d
                    else
                        cmp(ebx, static_cast<std::uint32_t>(RE::BSShaderMaterial::Feature::kParallax));   // SE: technique ID in ebx
                    cmovz(edx, r8d);  // set eye update true

                    // jmp out
                    jmp(ptr[rip]);
                    dq(a_target + 0x9);
                }
            }
        };
    }

    inline void Install()
    {
        const auto handle = REX::W32::GetModuleHandleA("CommunityShaders.dll");

        if (handle) {
            logger::info("community shaders detected - disabling bslightingshader parallax fix as it conflicts and is unecessary"sv);
            return;
        }

        REL::Relocation target{ RELOCATION_ID(100565, 107300), VAR_NUM(0x577, 0xB5D, 0x652) };

        detail::Patch p(target.address());
        auto&         trampoline = SKSE::GetTrampoline();
        trampoline.write_branch<6>(target.address(), trampoline.allocate(p));

        logger::info("installed bslightingshader parallax bug fix"sv);
    }
}
