#pragma once

namespace Fixes::ArcheryDownwardAiming
{
    namespace detail
    {
        struct Projectile
        {
            static void Move(RE::Projectile* a_self, RE::NiPoint3& a_from, const RE::NiPoint3& a_to)
            {
                const auto refShooter = a_self->GetProjectileRuntimeData().shooter.get();
                if (refShooter && refShooter->Is(RE::FormType::ActorCharacter)) {
                    const auto                    akShooter = static_cast<RE::Actor*>(refShooter.get());  // NOLINT(*-pro-type-static-cast-downcast)
                    [[maybe_unused]] RE::NiPoint3 direction;
                    akShooter->GetEyeVector(a_from, direction, true);
                }

                _Move(a_self, a_from, a_to);
            }

            static inline REL::Relocation<decltype(Move)> _Move;
        };
    }

    inline void Install()
    {
        // AE1799 recompile shifted this call site from +0x434 to +0x445 (same CALL
        // target, no other layout change).
        REL::Relocation target{ RELOCATION_ID(42852, 44027), VAR_NUM(0x3E9, util::IsAE1799() ? 0x445 : 0x434) };
        detail::Projectile::_Move = target.write_call<5>(detail::Projectile::Move);

        logger::info("installed archery downward aiming fix"sv);
    }
}
