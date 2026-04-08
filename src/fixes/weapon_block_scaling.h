#pragma once

namespace Fixes::WeaponBlockScaling
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_target)
            {
                // rbx = Actor*

                mov(rcx, rbx);
                mov(rdx, a_target);
                call(rdx);
                if (REL::Module::IsAE())
                    movaps(xmm7, xmm0);
                else
                    movaps(xmm8, xmm0);
            }
        };

        struct Actor
        {
            static float CalcWeaponDamage(RE::Actor* a_target)
            {
                auto weap = GetWeaponData(a_target);
                if (weap)
                    return static_cast<float>(weap->GetAttackDamage());
                else
                    return 0.0F;
            }

            static RE::TESObjectWEAP* GetWeaponData(RE::Actor* a_actor)
            {
                const auto proc = a_actor->GetActorRuntimeData().currentProcess;
                if (!proc || !proc->middleHigh) {
                    return nullptr;
                }

                const auto       middleProc = proc->middleHigh;
                const std::array entries{
                    middleProc->bothHands,
                    middleProc->rightHand,
                    middleProc->leftHand
                };

                for (const auto& entry : entries) {
                    if (entry) {
                        const auto obj = entry->object;
                        if (obj && obj->Is(RE::FormType::Weapon)) {
                            return static_cast<RE::TESObjectWEAP*>(obj);
                        }
                    }
                }

                return nullptr;
            }
        };
    }

    inline void Install()
    {
        REL::Relocation target{ RELOCATION_ID(42842, 44014), VAR_NUM(0x3B8, 0x3A2) };

        detail::Patch p(SKSE::stl::unrestricted_cast<std::uintptr_t>(detail::Actor::CalcWeaponDamage));
        p.ready();

        // Write patch bytes followed by NOPs to fill the code cave
        std::array<std::byte, 0x19> buf{};
        buf.fill(static_cast<std::byte>(0x90));
        const auto patchSize = p.getSize();
        std::copy_n(p.getCode<const std::byte*>(), patchSize, buf.begin());
        target.write(std::span{ buf.data(), VAR_NUM(0x19u, 0x17u) });

        logger::info("installed weapon block scaling fix"sv);
    }
}
