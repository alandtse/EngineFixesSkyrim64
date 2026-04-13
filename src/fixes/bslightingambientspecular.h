#pragma once

namespace Fixes::BSLightingAmbientSpecular
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_ambientSpecularAndFresnel, std::uintptr_t a_addAmbientSpecularToSetupGeometry)
            {
                Xbyak::Label jmpOut;
                // hook: in middle of SetupGeometry, right before if (rawTechnique & RAW_FLAG_SPECULAR)
                // register layout differs: VR uses rbx for rawTechnique (SE/AE use r13),
                //   VR/AE use rdi for m_PerGeometry (SE uses r15),
                //   PixelShader stack offset differs per runtime
                const bool isVR = REL::Module::IsVR();
                const bool isAE = REL::Module::IsAE();

                // rawTechnique register: r13 (SE/AE), rbx (VR)
                if (isVR)
                    test(dword[rbx + 0x94], static_cast<std::uint32_t>(RE::BSLightingShader::TechniqueFlag::kAmbientSpecular));
                else
                    test(dword[r13 + 0x94], static_cast<std::uint32_t>(RE::BSLightingShader::TechniqueFlag::kAmbientSpecular));
                jz(jmpOut);
                push(rax);
                push(rdx);
                mov(rax, a_ambientSpecularAndFresnel);
                movups(xmm0, ptr[rax]);
                // PixelShader ptr: AE rsp+0x70, VR rsp+0x58, SE rsp+0x60
                if (isAE)
                    mov(rax, qword[rsp + 0x2D0 - 0x260 + 0x10]);
                else if (isVR)
                    mov(rax, qword[rsp + 0x170 - 0x120 + 0x8]);
                else
                    mov(rax, qword[rsp + 0x170 - 0x120 + 0x10]);
                movzx(edx, byte[rax + 0x46]);  // m_ConstantOffsets[6] (AmbientSpecularTintAndFresnelPower)
                // m_PerGeometry buffer: rdi (AE/VR), r15 (SE)
                if (isAE || isVR)
                    mov(rax, ptr[rdi + 8]);
                else
                    mov(rax, ptr[r15 + 8]);
                movups(ptr[rax + rdx * 4], xmm0);
                pop(rdx);
                pop(rax);
                // original code continues with rawTechnique check
                L(jmpOut);
                if (isVR)
                    test(dword[rbx + 0x94], static_cast<std::uint32_t>(RE::BSLightingShader::TechniqueFlag::kSpecular));
                else
                    test(dword[r13 + 0x94], static_cast<std::uint32_t>(RE::BSLightingShader::TechniqueFlag::kSpecular));
                jmp(ptr[rip]);
                // test [r13+0x94], imm32 = 11 bytes (SE/AE: REX.B + F7 + ModRM + disp32 + imm32)
                // test [rbx+0x94], imm32 = 10 bytes (VR: no REX prefix needed for rbx)
                dq(a_addAmbientSpecularToSetupGeometry + (isVR ? 0xA : 0xB));
            }
        };
    }

    inline void Install()
    {
        // remove invalid code from BSLightingShader::SetupMaterial
        REL::Relocation materialTarget{ RELOCATION_ID(100563, 107298), VAR_NUM(0x713, 0x8CF) };
        materialTarget.write_fill(REL::NOP, 0x20);

        // add new code to BSLightingShader::SetupGeometry
        const REL::Relocation geometryTarget{ RELOCATION_ID(100565, 107300), VAR_NUM(0xBAD, 0x1271, 0xC59) };
        const REL::Relocation constant{ RELOCATION_ID(513256, 390997) };

        detail::Patch p(constant.address(), geometryTarget.address());
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        trampoline.write_branch<5>(geometryTarget.address(), trampoline.allocate(p));

        logger::info("installed BSLightingAmbientSpecular fix"sv);
    }
}