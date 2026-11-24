#pragma once

#include "installed_fixes.h"

namespace Fixes::NavMeshObstacleNullGuard
{
    // NavMeshObstacleManager::RelinkPortalEdges reads each neighbouring ObstacleTaskData's working
    // clone (task+0x38) for `clone->triangles.size` before testing the clone for null, so two of its
    // three inlined lookups fault when the clone has not been created yet.
    namespace detail
    {
        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume, std::uintptr_t a_fail)
            {
                Xbyak::Label fail;

                test(rax, rax);  // rax = record->navmesh, loaded just before the patch site
                jz(fail);
                mov(ecx, dword[rax + 0x38]);
                movzx(eax, r15w);
                jmp(ptr[rip]);
                dq(a_resume);  // the original `cmp eax, ecx`

                L(fail);
                jmp(ptr[rip]);
                dq(a_fail);  // the original size-check failure path
            }
        };

        struct Site
        {
            std::uintptr_t load;    // mov ecx, [rax+0x38]; movzx eax, r15w; cmp eax, ecx; jnc
            std::uintptr_t resume;  // cmp eax, ecx
            std::uintptr_t fail;    // jnc target
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;  // sites verified on VR 1.4.15 only

        if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
            logger::warn("skipping NavMesh obstacle null guard: unsupported VR runtime"sv);
            return;
        }

        static constexpr std::array<detail::Site, 2>  kSites{ {
            { 0x47886c, 0x478873, 0x4788be },
            { 0x478ed0, 0x478ed7, 0x478f2c },
        } };
        static constexpr std::array<std::uint8_t, 10> kExpected{ 0x8B, 0x48, 0x38, 0x41, 0x0F, 0xB7, 0xC7, 0x3B, 0xC1, 0x73 };

        std::size_t installed = 0;
        for (const auto& site : kSites) {
            REL::Relocation<std::uintptr_t> target{ REL::Offset(site.load) };
            if (!std::equal(kExpected.begin(), kExpected.end(), reinterpret_cast<const std::uint8_t*>(target.address()))) {
                logger::warn("skipping NavMesh obstacle null guard site 0x{:X}: signature mismatch"sv, site.load);
                continue;
            }

            REL::Relocation<std::uintptr_t> resume{ REL::Offset(site.resume) };
            REL::Relocation<std::uintptr_t> fail{ REL::Offset(site.fail) };

            detail::Patch p(resume.address(), fail.address());
            p.ready();

            auto& trampoline = SKSE::GetTrampoline();
            target.write_branch<6>(trampoline.allocate(p));
            ++installed;
        }

        if (installed == 0)
            return;

        EF_INSTALLED("NavMeshObstacleNullGuard"sv, "installed NavMesh obstacle-task null navmesh guard (VR)"sv);
    }
}
