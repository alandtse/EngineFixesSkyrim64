#pragma once

namespace Fixes::BSLightingShaderForceAlphaTest
{
    namespace detail
    {
        inline SafetyHookInline g_hk_BSBatchRenderer_SetupAndDrawPass;

        inline void BSBatchRenderer_SetupAndDrawPass(RE::BSRenderPass* a_self, std::uint32_t a_technique, bool a_alphaTest, std::uint32_t a_renderFlags)
        {
            if (*SKSE::stl::unrestricted_cast<std::uintptr_t*>(a_self->shader) == RE::VTABLE_BSLightingShader[0].address() && a_alphaTest) {
                const auto rawTechnique = a_technique - RE::BSLightingShader::kTechniqueIDBase;
                const auto feature = static_cast<RE::BSShaderMaterial::Feature>((rawTechnique >> 24) & 0x3F);
                if (feature != RE::BSShaderMaterial::Feature::kEye &&
                    feature != RE::BSShaderMaterial::Feature::kEnvironmentMap &&
                    feature != RE::BSShaderMaterial::Feature::kMultilayerParallax &&
                    feature != RE::BSShaderMaterial::Feature::kParallax) {
                    a_technique = a_technique | static_cast<std::uint32_t>(RE::BSLightingShader::TechniqueFlag::kDoAlphaTest);
                    a_self->passEnum = a_technique;
                }
            }

            g_hk_BSBatchRenderer_SetupAndDrawPass.call(a_self, a_technique, a_alphaTest, a_renderFlags);
        }
    }

    inline void Install()
    {
        REL::Relocation target{ RELOCATION_ID(100854, 107644) };
        detail::g_hk_BSBatchRenderer_SetupAndDrawPass = safetyhook::create_inline(target.address(), detail::BSBatchRenderer_SetupAndDrawPass);

        logger::info("installed bslightingshader force alpha test fix"sv);
    }
}