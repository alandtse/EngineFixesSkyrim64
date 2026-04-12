#pragma once

namespace Fixes::IsPlayerInRegionParentCellCheck
{
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(const std::uintptr_t a_target)
            {
                Xbyak::Label contAddr;
                Xbyak::Label skipAddr;
                Xbyak::Label skipTargetAddr;

                // Mirror the later SE caller-side fix:
                // load player -> null check -> load parent cell -> null check -> call GetRegionList.
                mov(rax, reinterpret_cast<std::uintptr_t>(&RE::PlayerCharacter::GetSingleton));
                call(rax);
                test(rax, rax);
                jz(skipAddr, Xbyak::CodeGenerator::T_NEAR);

                mov(rcx, ptr[rax + 0x60]);
                test(rcx, rcx);
                jz(skipAddr, Xbyak::CodeGenerator::T_NEAR);

                xor_(edx, edx);
                mov(rax, RELOCATION_ID(18540, 18999).address());
                call(rax);
                jmp(ptr[rip + contAddr]);

                L(contAddr);
                dq(a_target + 0x12);

                L(skipAddr);
                jmp(ptr[rip + skipTargetAddr]);

                L(skipTargetAddr);
                dq(a_target + 0x3B);
            }
        };

        inline void Install()
        {
            // VR lacks the caller-side parent-cell null checks that SE added in IsPlayerInRegion.
            REL::Relocation target{ REL::ID(21322), 0x22 };

            Patch patch(target.address());
            patch.ready();

            auto& trampoline = SKSE::GetTrampoline();
            trampoline.write_branch<5>(target.address(), trampoline.allocate(patch));
        }
    }

    inline void Install()
    {
        if (!REL::Module::IsVR()) {
            logger::info("skipping is player in region parent cell check fix on non-vr runtime"sv);
            return;
        }

        detail::Install();
        logger::info("installed is player in region parent cell check fix"sv);
    }
}