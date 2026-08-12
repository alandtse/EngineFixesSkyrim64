#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

// Fix for a use-after-free crash in the scene-graph downward-visit traversal,
// exposed by Community Shaders background shader compilation.
//
// Background-compile removes the blocking precompile screen, which also gated world
// rendering/streaming. With it gone, cell teardown (coc/cow -> GridArray::DetachAll ->
// TESObjectREFR::DetachHavok) walks the scene graph via the recursive NiAVObject visitor
// while the cell loader frees nodes. The visitor does `MOV RAX,[RDI]; CALL [RAX+0x18]`
// (RDI->vfunc[3], the get-children dispatch) on a freed-and-zeroed node -> RAX (vftable)
// is null -> AV read @0x18 -> CTD. Debugger/crashlog-confirmed on VR (TreePineForest02
// node hierarchy during a coc storm). Same UAF class as the BSCullingProcess OnVisible
// crashes (culling_freed_object_crash.h) and the renderpass-cache UAF, different subsystem.
//
// The primary visitor is a single recursive function (every node, at every depth,
// re-enters it), so one guard at its entry covers that subtree walk and both internal
// virtual dispatches. A separate cell child-array traversal can still encounter a
// freed child after its own null check, so VR also guards that loop's +0x18 call. The entry
// already begins `TEST RCX,RCX; JZ <exit>` BEFORE any stack setup, so <exit> is a
// proven-safe return from the pre-prologue state. The guard reuses that exact exit: it
// replicates the null check, then validates the node's vftable lies inside the main module
// image (a live vftable is in .rdata; a freed node's is null or heap garbage). Valid ->
// resume the original prologue; null/freed -> jump to the same clean <exit>, skipping the
// entire (recursive) walk.
//
// RAX and R10 are volatile and not argument registers (RCX/RDX/R8/R9), so they are safe to
// clobber at entry; RCX (the node) is preserved. Cross-runtime: the function and its 9-byte
// `TEST RCX,RCX (3) + JZ rel32 (6)` prologue are identical on SE/AE/VR; only the addresses
// differ (resolved per runtime). VR is where this bites (stereo widens the streaming race),
// but the traversal is shared, so SE/AE are covered for completeness.
//
// The exit address is not stored separately: it is decoded from the JZ rel32 at the site's
// own entry, so the exit can never drift out of sync with the prologue bytes the signature
// check already validates.

namespace Fixes::SceneGraphDetachFreedCrash
{
    namespace detail
    {
        struct Site
        {
            std::uintptr_t entryOffset;  // function entry: TEST RCX,RCX; JZ <rel32>
        };

        // sizeof(TEST RCX,RCX) + sizeof(JZ rel32) = 3 + (2 opcode + 4 rel32); resume = entry + 9.
        inline constexpr std::uintptr_t kPrologueLen = 9;

        inline constexpr Site kSiteVR{ 0xDFDCF0 };
        inline constexpr Site kSiteAE{ 0xE87DF0 };
        inline constexpr Site kSiteSE{ 0xDA8D70 };

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume, std::uintptr_t a_exit)
            {
                Xbyak::Label exitLbl, resumeAddr, exitAddr;

                // Replicate the displaced null check.
                test(rcx, rcx);
                jz(exitLbl);

                // Validate the node's vftable lies inside the main module image.
                mov(rax, ptr[rcx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(exitLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(exitLbl);

                // Live node: resume the original prologue after the displaced bytes.
                jmp(ptr[rip + resumeAddr]);

                // Null or freed node: take the original clean exit (nothing pushed yet).
                L(exitLbl);
                jmp(ptr[rip + exitAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(exitAddr);
                dq(a_exit);
            }
        };

        // A second cell-teardown traversal iterates a node's child array and
        // calls vfunc +0x18 before recursing.  A captured Riften transition had
        // a valid parent/child pointer but a null child vftable.  Skip that one
        // child and continue the original loop when its vftable is not in the
        // Skyrim image.
        struct ChildPatch final : Xbyak::CodeGenerator
        {
            ChildPatch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_postCall, std::uintptr_t a_nextChild)
            {
                Xbyak::Label skipLbl, postAddr, nextAddr;

                mov(rax, ptr[rcx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

                call(ptr[rax + 0x18]);
                jmp(ptr[rip + postAddr]);

                L(skipLbl);
                jmp(ptr[rip + nextAddr]);

                L(postAddr);
                dq(a_postCall);
                L(nextAddr);
                dq(a_nextChild);
            }
        };

        inline std::size_t PatchFreedChildTraversalSiteVR(std::uintptr_t a_moduleBase,
            std::uintptr_t a_moduleEnd, std::uintptr_t a_patchOffset,
            std::uintptr_t a_postCallOffset, std::uintptr_t a_nextChildOffset,
            std::span<const std::uint8_t> a_expected)
        {
            REL::Relocation<std::uintptr_t> patch{ REL::Offset{ a_patchOffset } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(patch.address());
            if (!std::equal(a_expected.begin(), a_expected.end(), bytes) ||
                a_patchOffset + a_expected.size() != a_nextChildOffset) {
                logger::warn("scene-graph child crash fix: unexpected bytes at {:X}, skipping site"sv,
                    a_patchOffset);
                return 0;
            }

            ChildPatch p{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_postCallOffset } }.address(),
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_nextChildOffset } }.address() };
            p.ready();
            patch.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            return 1;
        }

        inline void PatchFreedChildTraversalVR(std::uintptr_t a_moduleBase,
            std::uintptr_t                                    a_moduleEnd)
        {
            // Validate each complete block crossed by its invalid-child path,
            // not just the six displaced bytes.  The next-child offset begins
            // at the first instruction after each sequence.
            static constexpr std::uint8_t kTraversal410Expected[] = {
                0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x0B, 0x48, 0x8B, 0xC8,
                0xE8, 0x3D, 0xFF, 0xFF, 0xFF, 0x40, 0x0A, 0xF0
            };
            static constexpr std::uint8_t kTraversal510Expected[] = {
                0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x0B, 0x48, 0x8B, 0xC8,
                0xE8, 0x5D, 0xFF, 0xFF, 0xFF, 0x40, 0x0A, 0xF0
            };
            static constexpr std::uint8_t kTraversal5E0Expected[] = {
                0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0xC8,
                0xE8, 0x2D, 0xFF, 0xFF, 0xFF
            };
            static constexpr std::uint8_t kTraversal6E0Expected[] = {
                0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18, 0x48, 0x8B, 0xF0,
                0x48, 0x85, 0xC0, 0x74, 0x28, 0x48, 0x8B, 0x10,
                0x48, 0x8B, 0xC8, 0xFF, 0x52, 0x10, 0x48, 0x85, 0xC0,
                0x74, 0x12, 0x0F, 0x1F, 0x40, 0x00, 0x48, 0x3B, 0xC5,
                0x74, 0x33, 0x48, 0x8B, 0x40, 0x08, 0x48, 0x85, 0xC0,
                0x75, 0xF2, 0x48, 0x8B, 0xCE, 0xE8, 0x7A, 0xFF, 0xFF, 0xFF
            };
            static constexpr std::uint8_t kTraversal7A0Expected[] = {
                0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x08, 0x48, 0x8B, 0xC8,
                0xE8, 0x7D, 0xFF, 0xFF, 0xFF
            };
            static constexpr std::uint8_t kTraversal850Expected[] = {
                0x48, 0x8B, 0x01, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x0C, 0x48, 0x8B, 0xC8,
                0xE8, 0x8D, 0xFF, 0xFF, 0xFF, 0x84, 0xC0, 0x75, 0x2C
            };
            static_assert(0x29D4C0 + std::size(kTraversal410Expected) == 0x29D4D6);
            static_assert(0x29D5A0 + std::size(kTraversal510Expected) == 0x29D5B6);
            static_assert(0x29D6A0 + std::size(kTraversal5E0Expected) == 0x29D6B3);
            static_assert(0x29D730 + std::size(kTraversal6E0Expected) == 0x29D766);
            static_assert(0x29D810 + std::size(kTraversal7A0Expected) == 0x29D823);
            static_assert(0x29D8B0 + std::size(kTraversal850Expected) == 0x29D8C7);

            std::size_t installed = 0;
            installed += PatchFreedChildTraversalSiteVR(a_moduleBase, a_moduleEnd,
                0x29D4C0, 0x29D4C6, 0x29D4D6, kTraversal410Expected);
            installed += PatchFreedChildTraversalSiteVR(a_moduleBase, a_moduleEnd,
                0x29D5A0, 0x29D5A6, 0x29D5B6, kTraversal510Expected);
            installed += PatchFreedChildTraversalSiteVR(a_moduleBase, a_moduleEnd,
                0x29D6A0, 0x29D6A6, 0x29D6B3, kTraversal5E0Expected);
            installed += PatchFreedChildTraversalSiteVR(a_moduleBase, a_moduleEnd,
                0x29D730, 0x29D736, 0x29D766, kTraversal6E0Expected);
            installed += PatchFreedChildTraversalSiteVR(a_moduleBase, a_moduleEnd,
                0x29D810, 0x29D816, 0x29D823, kTraversal7A0Expected);
            installed += PatchFreedChildTraversalSiteVR(a_moduleBase, a_moduleEnd,
                0x29D8B0, 0x29D8B6, 0x29D8C7, kTraversal850Expected);
            logger::info("installed scene-graph child freed-object crash fix ({} site(s))"sv,
                installed);
        }

        // A third recursive scene traversal dispatches twice through the same
        // node: +0x10 before its type/owner work and +0x18 before descending
        // into children.  Cell teardown can free the node before either call.
        // Both invalid-vftable paths use the function's existing epilogue,
        // avoiding every later read from that node.
        struct RecursiveNodePatchRdx final : Xbyak::CodeGenerator
        {
            RecursiveNodePatchRdx(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_postCall, std::uintptr_t a_exit)
            {
                Xbyak::Label skipLbl, postAddr, exitAddr;

                mov(rax, ptr[rdx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

                mov(rcx, rdx);
                call(ptr[rax + 0x10]);
                jmp(ptr[rip + postAddr]);

                L(skipLbl);
                jmp(ptr[rip + exitAddr]);

                L(postAddr);
                dq(a_postCall);
                L(exitAddr);
                dq(a_exit);
            }
        };

        struct RecursiveNodePatchRdi final : Xbyak::CodeGenerator
        {
            RecursiveNodePatchRdi(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_postCall, std::uintptr_t a_exit)
            {
                Xbyak::Label skipLbl, postAddr, exitAddr;

                mov(rax, ptr[rdi]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(skipLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(skipLbl);

                mov(rcx, rdi);
                call(ptr[rax + 0x18]);
                jmp(ptr[rip + postAddr]);

                L(skipLbl);
                jmp(ptr[rip + exitAddr]);

                L(postAddr);
                dq(a_postCall);
                L(exitAddr);
                dq(a_exit);
            }
        };

        inline void PatchRecursiveNodeTraversalVR(std::uintptr_t a_moduleBase,
            std::uintptr_t                                       a_moduleEnd)
        {
            constexpr std::uintptr_t      kFirstPatch = 0x2AC2C4;
            constexpr std::uintptr_t      kFirstPost = 0x2AC2CD;
            constexpr std::uintptr_t      kSecondPatch = 0x2AC448;
            constexpr std::uintptr_t      kSecondPost = 0x2AC451;
            constexpr std::uintptr_t      kExit = 0x2AC49B;
            static constexpr std::uint8_t kFirstExpected[] = {
                0x48, 0x8B, 0x02, 0x48, 0x8B, 0xCA, 0xFF, 0x50, 0x10
            };
            static constexpr std::uint8_t kSecondExpected[] = {
                0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x18
            };
            // Both guarded calls execute after the function's single
            // sub rsp,0x40 prologue and before its common add rsp,0x40
            // epilogue; no intervening instruction adjusts RSP. Validate the
            // complete shared epilogue before either exceptional path uses it.
            static constexpr std::uint8_t kExitExpected[] = {
                0x48, 0x8B, 0x5C, 0x24, 0x70,
                0x48, 0x83, 0xC4, 0x40,
                0x5F, 0x5E, 0x5D, 0xC3
            };

            REL::Relocation<std::uintptr_t> first{ REL::Offset{ kFirstPatch } };
            REL::Relocation<std::uintptr_t> second{ REL::Offset{ kSecondPatch } };
            REL::Relocation<std::uintptr_t> exitSite{ REL::Offset{ kExit } };
            const auto*                     firstBytes = reinterpret_cast<const std::uint8_t*>(first.address());
            const auto*                     secondBytes = reinterpret_cast<const std::uint8_t*>(second.address());
            const auto*                     exitBytes = reinterpret_cast<const std::uint8_t*>(exitSite.address());
            if (!std::equal(std::begin(kFirstExpected), std::end(kFirstExpected), firstBytes) ||
                !std::equal(std::begin(kSecondExpected), std::end(kSecondExpected), secondBytes) ||
                !std::equal(std::begin(kExitExpected), std::end(kExitExpected), exitBytes)) {
                logger::warn("recursive scene-node crash fix: unexpected bytes, skipping sites"sv);
                return;
            }

            const auto            exit = exitSite.address();
            RecursiveNodePatchRdx firstPatch{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kFirstPost } }.address(), exit };
            RecursiveNodePatchRdi secondPatch{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kSecondPost } }.address(), exit };
            firstPatch.ready();
            secondPatch.ready();

            auto& trampoline = SKSE::GetTrampoline();
            first.write_branch<5>(trampoline.allocate(firstPatch));
            second.write_branch<5>(trampoline.allocate(secondPatch));
            logger::info("installed recursive scene-node freed-object crash fix (2 sites)"sv);
        }

        // The multibound/water scene helper is reached from the same cell teardown
        // traversal with an auxiliary scene object in RDX.  During an exterior coc
        // storm that object was already zeroed while its enclosing Riften multibound
        // graph was still being visited; the helper immediately dispatched vfunc
        // +0x48 through the null vftable at SkyrimVR+0x4DA56F.  Every normal path in
        // this helper returns zero, so rejecting a null/freed argument at entry has
        // the same failure semantics as its existing internal checks.
        struct MultiBoundHelperPatch final : Xbyak::CodeGenerator
        {
            MultiBoundHelperPatch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume)
            {
                Xbyak::Label invalidLbl, resumeAddr;

                test(rdx, rdx);
                jz(invalidLbl);

                mov(rax, ptr[rdx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(invalidLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(invalidLbl);

                // Replicate the complete six-byte prologue displaced by the
                // five-byte branch, then resume at the first body instruction.
                push(rbx);
                sub(rsp, 0x20);
                jmp(ptr[rip + resumeAddr]);

                L(invalidLbl);
                xor_(eax, eax);
                ret();

                L(resumeAddr);
                dq(a_resume);
            }
        };

        inline void PatchMultiBoundHelperVR(std::uintptr_t a_moduleBase,
            std::uintptr_t                                 a_moduleEnd)
        {
            constexpr std::uintptr_t      kEntryOffset = 0x4DA560;
            constexpr std::uintptr_t      kResumeOffset = 0x4DA566;
            static constexpr std::uint8_t kExpected[] = {
                0x40, 0x53,              // push rbx
                0x48, 0x83, 0xEC, 0x20,  // sub rsp,20h
                0x48, 0x8B, 0x02,        // mov rax,[rdx]
                0x48, 0x8B, 0xCA,        // mov rcx,rdx
                0x48, 0x8B, 0xDA,        // mov rbx,rdx
                0xFF, 0x50, 0x48         // call [rax+48h]
            };

            REL::Relocation<std::uintptr_t> entry{ REL::Offset{ kEntryOffset } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(entry.address());
            if (!std::equal(std::begin(kExpected), std::end(kExpected), bytes)) {
                logger::warn("multibound scene helper crash fix: unexpected bytes at {:X}, skipping"sv,
                    kEntryOffset);
                return;
            }

            MultiBoundHelperPatch p{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kResumeOffset } }.address() };
            p.ready();
            entry.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed multibound scene helper freed-object crash fix"sv);
        }

        // Three recursive multibound callers invoke +0x18 on their scene-node
        // input immediately after consulting the guarded helper above.  The
        // helper can safely reject its own stale RDX argument, but does not own
        // or validate the caller's RCX node.  A captured Riften-to-Markarth
        // transition reached the middle caller with a stale BSMultiBoundNode
        // vftable.  Invalid inputs reproduce a null virtual result and resume
        // at the caller's native post-call null check.
        struct MultiBoundCallerPatch final : Xbyak::CodeGenerator
        {
            MultiBoundCallerPatch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_postCall, bool a_objectInRdi)
            {
                Xbyak::Label invalidLbl, postAddr;

                if (a_objectInRdi) {
                    mov(ptr[rsp + 0x50], rbp);
                    mov(rax, ptr[rdi]);
                } else {
                    mov(ptr[rsp + 0x20], rbp);
                    mov(rax, ptr[rsi]);
                }

                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(invalidLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(invalidLbl);

                if (a_objectInRdi)
                    mov(rcx, rdi);
                else
                    mov(rcx, rsi);
                call(ptr[rax + 0x18]);
                jmp(ptr[rip + postAddr]);

                L(invalidLbl);
                xor_(eax, eax);
                jmp(ptr[rip + postAddr]);

                L(postAddr);
                dq(a_postCall);
            }
        };

        inline void PatchMultiBoundCallersVR(std::uintptr_t a_moduleBase,
            std::uintptr_t                                  a_moduleEnd)
        {
            struct CallerSite
            {
                std::uintptr_t patchOffset;
                std::uintptr_t postCallOffset;
                bool           objectInRdi;
                std::uint8_t   objectModRm;
                std::uint8_t   stackDisplacement;
            };

            static constexpr std::array<CallerSite, 3> kSites{ {
                { 0x4D9BE1, 0x4D9BEF, false, 0x06, 0x20 },
                { 0x4D9CB1, 0x4D9CBF, false, 0x06, 0x20 },
                { 0x4D9D83, 0x4D9D91, true, 0x07, 0x50 },
            } };

            auto&       trampoline = SKSE::GetTrampoline();
            std::size_t installed = 0;
            for (const auto& site : kSites) {
                REL::Relocation<std::uintptr_t> patch{ REL::Offset{ site.patchOffset } };
                const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(patch.address());
                const std::uint8_t              expected[] = {
                    0x48, 0x8B, site.objectModRm,  // mov rax,[rsi/rdi]
                    0x48, 0x8B,                    // mov rcx,rsi/rdi
                    static_cast<std::uint8_t>(site.objectModRm + 0xC8),
                    0x48, 0x89, 0x6C, 0x24, site.stackDisplacement,
                    0xFF, 0x50, 0x18  // call [rax+18h]
                };
                if (!std::equal(std::begin(expected), std::end(expected), bytes) ||
                    site.patchOffset + std::size(expected) != site.postCallOffset) {
                    logger::warn("multibound caller crash fix: unexpected bytes at {:X}, skipping site"sv,
                        site.patchOffset);
                    continue;
                }

                MultiBoundCallerPatch p{ a_moduleBase, a_moduleEnd,
                    REL::Relocation<std::uintptr_t>{ REL::Offset{ site.postCallOffset } }.address(),
                    site.objectInRdi };
                p.ready();
                patch.write_branch<5>(trampoline.allocate(p));
                ++installed;
            }
            logger::info("installed multibound caller freed-object crash fix ({} site(s))"sv,
                installed);
        }

        // This recursive ObjectLOD visitor invokes vfunc +0x38 before walking
        // a node's children.  A stress transition retained a departed
        // MountainTrimSlab beneath ObjectLODRoot whose reused heap vftable held
        // the value 1 in that slot, producing an execute-at-0x1 crash at
        // SkyrimVR+0x13021E0.  Guarding the recursive function's entry covers
        // both its initial dispatch and every descendant visit.  Its native
        // null-input path returns zero, which is also the safe result here.
        struct ObjectLODVisitorPatch final : Xbyak::CodeGenerator
        {
            ObjectLODVisitorPatch(std::uintptr_t a_moduleBase, std::uintptr_t a_moduleEnd,
                std::uintptr_t a_resume)
            {
                Xbyak::Label invalidLbl, resumeAddr;

                test(rcx, rcx);
                jz(invalidLbl);

                mov(rax, ptr[rcx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(invalidLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(invalidLbl);

                // Replicate the displaced pre-prologue home-space store.
                mov(ptr[rsp + 0x10], rdx);
                jmp(ptr[rip + resumeAddr]);

                L(invalidLbl);
                xor_(eax, eax);
                ret();

                L(resumeAddr);
                dq(a_resume);
            }
        };

        inline void PatchObjectLODVisitorVR(std::uintptr_t a_moduleBase,
            std::uintptr_t                                 a_moduleEnd)
        {
            constexpr std::uintptr_t      kEntryOffset = 0x13021C0;
            constexpr std::uintptr_t      kResumeOffset = 0x13021C5;
            static constexpr std::uint8_t kExpected[] = {
                0x48, 0x89, 0x54, 0x24, 0x10,        // mov [rsp+10h],rdx
                0x53,                                // push rbx
                0x56,                                // push rsi
                0x57,                                // push rdi
                0x48, 0x83, 0xEC, 0x20,              // sub rsp,20h
                0x33, 0xDB,                          // xor ebx,ebx
                0x48, 0x8B, 0xF2,                    // mov rsi,rdx
                0x48, 0x8B, 0xF9,                    // mov rdi,rcx
                0x48, 0x85, 0xC9,                    // test rcx,rcx
                0x0F, 0x84, 0x89, 0x00, 0x00, 0x00,  // je native zero-result exit
                0x48, 0x8B, 0x01,                    // mov rax,[rcx]
                0xFF, 0x50, 0x38                     // call [rax+38h]
            };

            REL::Relocation<std::uintptr_t> entry{ REL::Offset{ kEntryOffset } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(entry.address());
            if (!std::equal(std::begin(kExpected), std::end(kExpected), bytes)) {
                logger::warn("ObjectLOD recursive visitor crash fix: unexpected bytes at {:X}, skipping"sv,
                    kEntryOffset);
                return;
            }

            ObjectLODVisitorPatch p{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kResumeOffset } }.address() };
            p.ready();
            entry.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed ObjectLOD recursive visitor freed-object crash fix"sv);
        }

        // Two more ObjectLOD readers were exposed after the recursive visitor
        // guards had completed a full 30-leg route.  One walks a linked parent
        // chain and dispatches +0x10 twice on a matching node.  The other is a
        // two-function child-array family that dispatches +0x38 and, on the
        // fallback path, +0x18.  Captured stale objects carried heap/reused
        // vftables, producing execute-at-zero/three crashes.  Each exceptional
        // path uses the reader's existing no-match/next-child continuation.
        struct ObjectLODReaderDispatchPatch final : Xbyak::CodeGenerator
        {
            ObjectLODReaderDispatchPatch(std::uintptr_t a_moduleBase,
                std::uintptr_t a_moduleEnd, std::uintptr_t a_postCall,
                std::uintptr_t a_invalid, bool a_objectInRdi,
                std::uint8_t a_vfuncOffset)
            {
                Xbyak::Label invalidLbl, postAddr, invalidAddr;

                if (a_objectInRdi)
                    mov(rax, ptr[rdi]);
                else
                    mov(rax, ptr[rbx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(invalidLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(invalidLbl);

                if (a_objectInRdi)
                    mov(rcx, rdi);
                else
                    mov(rcx, rbx);
                call(ptr[rax + a_vfuncOffset]);
                jmp(ptr[rip + postAddr]);

                L(invalidLbl);
                jmp(ptr[rip + invalidAddr]);

                L(postAddr);
                dq(a_postCall);
                L(invalidAddr);
                dq(a_invalid);
            }
        };

        inline bool PatchObjectLODReaderDispatchVR(std::uintptr_t a_moduleBase,
            std::uintptr_t a_moduleEnd, std::uintptr_t a_patchOffset,
            std::uintptr_t a_postCallOffset, std::uintptr_t a_invalidOffset,
            std::span<const std::uint8_t> a_expected, bool a_objectInRdi,
            std::uint8_t a_vfuncOffset)
        {
            REL::Relocation<std::uintptr_t> patch{ REL::Offset{ a_patchOffset } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(patch.address());
            if (!std::equal(a_expected.begin(), a_expected.end(), bytes)) {
                logger::warn("ObjectLOD reader crash fix: unexpected bytes at {:X}, skipping site"sv,
                    a_patchOffset);
                return false;
            }

            ObjectLODReaderDispatchPatch p{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_postCallOffset } }.address(),
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_invalidOffset } }.address(),
                a_objectInRdi, a_vfuncOffset };
            p.ready();
            patch.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            return true;
        }

        inline void PatchAdditionalObjectLODReadersVR(std::uintptr_t a_moduleBase,
            std::uintptr_t                                           a_moduleEnd)
        {
            static constexpr std::uint8_t kLinkedFirstExpected[] = {
                0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x10,
                0x48, 0x3B, 0xC7, 0x74, 0x16, 0x48, 0x8B, 0x5B, 0x30,
                0x48, 0x85, 0xDB, 0x75, 0xE9
            };
            static constexpr std::uint8_t kLinkedSecondExpected[] = {
                0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x10,
                0x48, 0x3B, 0xC7, 0x75, 0xE5
            };
            static constexpr std::uint8_t kLinkedExitExpected[] = {
                0x33, 0xC0, 0x48, 0x8B, 0x5C, 0x24, 0x30,
                0x48, 0x83, 0xC4, 0x20, 0x5F, 0xC3
            };

            static constexpr std::uint8_t kOuterFirstExpected[] = {
                0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x38,
                0x48, 0x85, 0xC0, 0x74, 0x1F,
                0x48, 0x8B, 0x87, 0x68, 0x01, 0x00, 0x00,
                0x48, 0x85, 0xC0, 0x74, 0x2C, 0x48, 0x8B, 0x48, 0x70,
                0x48, 0x85, 0xC9, 0x74, 0x23, 0x48, 0x8B, 0xD7,
                0xE8, 0x80, 0xC7, 0x05, 0x00, 0xEB, 0x19,
                0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x0B, 0x48, 0x8B, 0xD0,
                0x48, 0x8B, 0xCD, 0xE8, 0xF5, 0x3F, 0x00, 0x00
            };
            static constexpr std::uint8_t kOuterSecondExpected[] = {
                0x48, 0x8B, 0x07, 0x48, 0x8B, 0xCF, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x0B, 0x48, 0x8B, 0xD0,
                0x48, 0x8B, 0xCD, 0xE8, 0xF5, 0x3F, 0x00, 0x00
            };
            static constexpr std::uint8_t kInnerFirstExpected[] = {
                0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x38,
                0x48, 0x85, 0xC0, 0x74, 0x1F,
                0x48, 0x8B, 0x83, 0x68, 0x01, 0x00, 0x00,
                0x48, 0x85, 0xC0, 0x74, 0x2C, 0x48, 0x8B, 0x48, 0x70,
                0x48, 0x85, 0xC9, 0x74, 0x23, 0x48, 0x8B, 0xD3,
                0xE8, 0x05, 0x87, 0x05, 0x00, 0xEB, 0x19,
                0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x0B, 0x48, 0x8B, 0xD0,
                0x48, 0x8B, 0xCD, 0xE8, 0x7A, 0xFF, 0xFF, 0xFF
            };
            static constexpr std::uint8_t kInnerSecondExpected[] = {
                0x48, 0x8B, 0x03, 0x48, 0x8B, 0xCB, 0xFF, 0x50, 0x18,
                0x48, 0x85, 0xC0, 0x74, 0x0B, 0x48, 0x8B, 0xD0,
                0x48, 0x8B, 0xCD, 0xE8, 0x7A, 0xFF, 0xFF, 0xFF
            };

            static_assert(0x136004E + std::size(kLinkedFirstExpected) == 0x1360065);
            static_assert(0x1360072 + std::size(kLinkedSecondExpected) == 0x1360080);
            static_assert(0x1360065 + std::size(kLinkedExitExpected) == 0x1360072);
            static_assert(0x12F8035 + std::size(kOuterFirstExpected) == 0x12F807B);
            static_assert(0x12F8062 + std::size(kOuterSecondExpected) == 0x12F807B);
            static_assert(0x12FC0B0 + std::size(kInnerFirstExpected) == 0x12FC0F6);
            static_assert(0x12FC0DD + std::size(kInnerSecondExpected) == 0x12FC0F6);

            REL::Relocation<std::uintptr_t> linkedExit{ REL::Offset{ 0x1360065 } };
            const auto*                     linkedExitBytes = reinterpret_cast<const std::uint8_t*>(linkedExit.address());
            if (!std::equal(std::begin(kLinkedExitExpected), std::end(kLinkedExitExpected), linkedExitBytes)) {
                logger::warn("ObjectLOD linked-reader crash fix: unexpected exit bytes, skipping sites"sv);
            } else {
                std::size_t installed = 0;
                installed += PatchObjectLODReaderDispatchVR(a_moduleBase, a_moduleEnd,
                    0x136004E, 0x1360057, 0x1360065, kLinkedFirstExpected, false, 0x10);
                installed += PatchObjectLODReaderDispatchVR(a_moduleBase, a_moduleEnd,
                    0x1360072, 0x136007B, 0x1360065, kLinkedSecondExpected, false, 0x10);
                logger::info("installed ObjectLOD linked-reader freed-object crash fix ({} site(s))"sv,
                    installed);
            }

            std::size_t installed = 0;
            installed += PatchObjectLODReaderDispatchVR(a_moduleBase, a_moduleEnd,
                0x12F8035, 0x12F803E, 0x12F807B, kOuterFirstExpected, true, 0x38);
            installed += PatchObjectLODReaderDispatchVR(a_moduleBase, a_moduleEnd,
                0x12F8062, 0x12F806B, 0x12F807B, kOuterSecondExpected, true, 0x18);
            installed += PatchObjectLODReaderDispatchVR(a_moduleBase, a_moduleEnd,
                0x12FC0B0, 0x12FC0B9, 0x12FC0F6, kInnerFirstExpected, false, 0x38);
            installed += PatchObjectLODReaderDispatchVR(a_moduleBase, a_moduleEnd,
                0x12FC0DD, 0x12FC0E6, 0x12FC0F6, kInnerSecondExpected, false, 0x18);
            logger::info("installed ObjectLOD child-reader freed-object crash fix ({} site(s))"sv,
                installed);
        }

        // NiNode::ProcessClone walks the source node's child array and invokes
        // ProcessClone (vfunc +0xB8) on every non-null child.  A streamed Riften
        // reload retained a non-null Coin01 child whose object had already been
        // reclaimed: its vftable was heap data and the +0xB8 slot contained -1.
        // The native loop already skips null source children and leaves the
        // corresponding preallocated destination slot empty.  Treat a child
        // with a non-image vftable identically, then continue with its sibling.
        struct NiNodeCloneChildPatch final : Xbyak::CodeGenerator
        {
            NiNodeCloneChildPatch(std::uintptr_t a_moduleBase,
                std::uintptr_t a_moduleEnd, std::uintptr_t a_postCall,
                std::uintptr_t a_nextChild)
            {
                Xbyak::Label invalidLbl, postAddr, nextAddr;

                mov(rax, ptr[rcx]);
                mov(r10, a_moduleBase);
                cmp(rax, r10);
                jb(invalidLbl);
                mov(r10, a_moduleEnd);
                cmp(rax, r10);
                jae(invalidLbl);

                mov(rdx, rbp);
                call(ptr[rax + 0xB8]);
                jmp(ptr[rip + postAddr]);

                L(invalidLbl);
                jmp(ptr[rip + nextAddr]);

                L(postAddr);
                dq(a_postCall);
                L(nextAddr);
                dq(a_nextChild);
            }
        };

        inline void PatchNiNodeCloneChildVR(std::uintptr_t a_moduleBase,
            std::uintptr_t                                a_moduleEnd)
        {
            constexpr std::uintptr_t      kPatchOffset = 0xC9C870;
            constexpr std::uintptr_t      kPostCallOffset = 0xC9C87C;
            constexpr std::uintptr_t      kNextChildOffset = 0xC9C894;
            static constexpr std::uint8_t kExpected[] = {
                0x48, 0x8B, 0x01,                    // mov rax,[rcx]
                0x48, 0x8B, 0xD5,                    // mov rdx,rbp
                0xFF, 0x90, 0xB8, 0x00, 0x00, 0x00   // call [rax+B8h]
            };
            static_assert(kPatchOffset + std::size(kExpected) == kPostCallOffset);

            REL::Relocation<std::uintptr_t> patch{ REL::Offset{ kPatchOffset } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(patch.address());
            if (!std::equal(std::begin(kExpected), std::end(kExpected), bytes)) {
                logger::warn("NiNode clone child crash fix: unexpected bytes at {:X}, skipping site"sv,
                    kPatchOffset);
                return;
            }

            NiNodeCloneChildPatch p{ a_moduleBase, a_moduleEnd,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kPostCallOffset } }.address(),
                REL::Relocation<std::uintptr_t>{ REL::Offset{ kNextChildOffset } }.address() };
            p.ready();
            patch.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed NiNode clone child freed-object crash fix"sv);
        }
    }

    inline void Install()
    {
        const auto [moduleBase, moduleEnd] = util::GetModuleImageBounds();

        const auto& site = REL::Module::IsVR() ? detail::kSiteVR :
                           REL::Module::IsAE() ? detail::kSiteAE :
                                                 detail::kSiteSE;

        REL::Relocation<std::uintptr_t> entry{ REL::Offset{ site.entryOffset } };

        // Verify the displaced prologue is TEST RCX,RCX; JZ rel32 (48 85 C9 0F 84 ..)
        // before caving it; guards against offset drift corrupting the function entry.
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(entry.address());
        if (!(bytes[0] == 0x48 && bytes[1] == 0x85 && bytes[2] == 0xC9 &&
                bytes[3] == 0x0F && bytes[4] == 0x84)) {
            logger::warn("scene-graph detach crash fix: unexpected prologue at {:X}, skipping"sv,
                site.entryOffset);
            return;
        }

        // Decode the JZ rel32 (bytes 5-8) to derive the exit address directly from the
        // validated prologue, rather than trusting a separately-hardcoded offset.
        std::int32_t rel32;
        std::memcpy(&rel32, bytes + 5, sizeof(rel32));
        const auto exit = entry.address() + detail::kPrologueLen + rel32;

        detail::Patch p{ moduleBase, moduleEnd, entry.address() + detail::kPrologueLen, exit };
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        entry.write_branch<5>(trampoline.allocate(p));

        if (REL::Module::IsVR()) {
            detail::PatchFreedChildTraversalVR(moduleBase, moduleEnd);
            detail::PatchRecursiveNodeTraversalVR(moduleBase, moduleEnd);
            detail::PatchMultiBoundHelperVR(moduleBase, moduleEnd);
            detail::PatchMultiBoundCallersVR(moduleBase, moduleEnd);
            detail::PatchObjectLODVisitorVR(moduleBase, moduleEnd);
            detail::PatchAdditionalObjectLODReadersVR(moduleBase, moduleEnd);
            detail::PatchNiNodeCloneChildVR(moduleBase, moduleEnd);
        }

        logger::info("installed scene-graph detach freed-object crash fix"sv);
    }
}
