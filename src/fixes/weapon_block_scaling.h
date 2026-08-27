#pragma once

namespace Fixes::WeaponBlockScaling
{
    namespace detail
    {
        // AE and VR's compiled ApplyDamageReduction both hold the replaced inline
        // block's result in xmm7; SE holds it in xmm8.
        inline bool SignatureMatches(std::uintptr_t a_addr)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            // ADD RCX,0xc0
            return p[0] == 0x48 && p[1] == 0x81 && p[2] == 0xC1 && p[3] == 0xC0 && p[4] == 0x00 &&
                   p[5] == 0x00 && p[6] == 0x00;
        }

        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_target)
            {
                // rbx = Actor*

                mov(rcx, rbx);
                mov(rdx, a_target);
                call(rdx);
                if (REL::Module::IsAE() || REL::Module::IsVR())
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
        // VR's compiled layout diverges from SE here (different code cave offset and
        // size), even though it's the same function id and the same source.
        REL::Relocation target{ RELOCATION_ID(42842, 44014), VAR_NUM(0x3B8, 0x3A2, 0x5B9) };

        if (!detail::SignatureMatches(target.address())) {
            logger::warn("weapon block scaling fix: unexpected bytes at patch site, skipping"sv);
            return;
        }

        detail::Patch p(SKSE::stl::unrestricted_cast<std::uintptr_t>(detail::Actor::CalcWeaponDamage));
        p.ready();

        // Write patch bytes followed by NOPs to fill the code cave
        std::array<std::byte, 0x19> buf{};
        buf.fill(static_cast<std::byte>(0x90));
        const auto patchSize = p.getSize();
        std::copy_n(p.getCode<const std::byte*>(), patchSize, buf.begin());
        target.write(std::span{ buf.data(), VAR_NUM(0x19u, 0x17u, 0x17u) });

        logger::info("installed weapon block scaling fix"sv);
    }
}
