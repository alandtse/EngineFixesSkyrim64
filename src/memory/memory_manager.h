#pragma once

namespace Memory::MemoryManager
{
    namespace detail
    {
        // when the global memory manager has a zero-size allocation it returns scratch memory
        // replicate this behavior
        inline std::byte* g_ZeroAddress{ nullptr };

        // MemoryManager::AutoScrapBuffer impl
        // ScrapHeap RAII wrapper
        struct AutoScrapBuffer
        {
            static void Ctor(AutoScrapBuffer* a_self, const std::size_t a_size, const std::size_t a_alignment)
            {
                if (a_size == 0) {
                    a_self->p_Memory = g_ZeroAddress;
                } else {
                    a_self->p_Memory = Allocator::GetAllocator()->AllocateAligned(a_size, a_alignment);
                }
            }

            static void Dtor(AutoScrapBuffer* a_self)
            {
                if (a_self->p_Memory != g_ZeroAddress) {
                    Allocator::GetAllocator()->DeallocateAligned(a_self->p_Memory);
                }
                a_self->p_Memory = nullptr;
            }

            // VR: ported from EngineFixesVR (rollingrock). Spot-patch Ctor/Dtor in place rather than
            // wholesale-replacing them.
            //
            // Vanilla VR uses a rotating-cookie sentinel for zero-size AutoScrapBuffers (XOR-rotates a
            // global at 0x1430C1F78 by 0x10 on each zero-size Ctor; Dtor compares the stored slot to
            // both the current and rotated cookie to decide skip-vs-free). We replace the cookie scheme
            // with a simpler nullptr sentinel:
            //   Ctor +0x1D..+0x32 (15 bytes): cookie rotation → NOP. Falls through to +0x32
            //     (mov [rcx], rdx) with rdx==size==0, so the slot is zeroed.
            //   Dtor +0x12..+0x2F (29 bytes): cookie comparison → `xor rax,rax; cmp rbx,rax` + NOP fill,
            //     and the jnz at +0x2F is flipped to jz, meaning "skip free if slot is null".
            // The non-zero allocation paths in both Ctor and Dtor are unmodified — they still call
            // GetThreadScrapHeap()->{Allocate,Deallocate}, which are TBB-redirected by ScrapHeap::Install().
            //
            // Why not the wholesale replace_func path used on SE/AE? Empirically wholesale corrupts
            // the heap on VR while it ships fine on SE/AE. Static analysis shows the surrounding
            // MemoryManager surface is structurally identical between SE and VR (same routing in
            // Free, same cookie sentinel scheme at PTR_142f69ea8 / DAT_1430c1f78, same shutdown
            // dispatcher), so the SE-vs-VR delta isn't visible at the instruction level — likely a
            // runtime-only behavior difference (TBB internals, plugin load ordering, or a stack-RAII
            // unwind/SEH detail not captured by Ghidra). The spot patch matches what EngineFixesVR
            // ships and is community-validated for VR. If you can identify the runtime mechanism and
            // make wholesale safe on VR, this branch should be removed.
            static void InstallVRSpotPatches()
            {
                struct DtorPatch final : Xbyak::CodeGenerator
                {
                    DtorPatch()
                    {
                        xor_(rax, rax);  // 3 bytes: 48 33 C0
                        cmp(rbx, rax);   // 3 bytes: 48 3B D8
                    }
                };

                // AutoScrapBuffer::Ctor: NOP cookie rotation block [+0x1D..+0x32) (15 bytes).
                REL::Relocation<std::uintptr_t> ctorBase{ RELOCATION_ID(66853, 68108) };
                REL::safe_fill(ctorBase.address() + 0x1D, REL::NOP, 0x32 - 0x1D);

                // AutoScrapBuffer::Dtor: replace cookie compare [+0x12..+0x2F) with `xor rax,rax; cmp rbx,rax`,
                // NOP-fill remainder.
                REL::Relocation<std::uintptr_t> dtorBase{ RELOCATION_ID(66854, 68109) };
                REL::safe_fill(dtorBase.address() + 0x12, REL::NOP, 0x2F - 0x12);

                DtorPatch dp;
                dp.ready();
                assert(dp.getSize() <= 0x2F - 0x12);
                REL::safe_write(
                    dtorBase.address() + 0x12,
                    std::span{ dp.getCode<const std::byte*>(), dp.getSize() });

                // AutoScrapBuffer::Dtor: flip jnz (0x75) to jz (0x74) at +0x2F so the "skip free"
                // branch fires when the slot is null (post-patch sentinel) instead of when it matches
                // the cookie.
                REL::safe_write<std::uint8_t>(dtorBase.address() + 0x2F, 0x74);
            }

            static void Install()
            {
                if (REL::Module::IsVR()) {
                    InstallVRSpotPatches();
                    return;
                }

                REL::Relocation ctor{ RELOCATION_ID(66853, 68108) };
                REL::Relocation dtor{ RELOCATION_ID(66854, 68109) };

                ctor.replace_func(0x7B, Ctor);
                dtor.replace_func(VAR_NUM(0x58, 0x54), Dtor);
            }

            void* p_Memory;
        };

        inline void* Allocate(RE::MemoryManager*, const std::size_t a_size, const std::uint32_t a_alignment, const bool a_alignmentRequired)
        {
            if (a_size > 0) {
                return a_alignmentRequired ? Allocator::GetAllocator()->AllocateAligned(a_size, a_alignment) : Allocator::GetAllocator()->Allocate(a_size);
            }

            return g_ZeroAddress;
        }

        inline void* Reallocate(RE::MemoryManager* a_self, void* a_oldMem, const std::size_t a_newSize, const std::uint32_t a_alignment, const bool a_alignmentRequired)
        {
            if (a_oldMem == g_ZeroAddress)
                return Allocate(a_self, a_newSize, a_alignment, a_alignmentRequired);

            return a_alignmentRequired ? Allocator::GetAllocator()->ReallocateAligned(a_oldMem, a_newSize, a_alignment) : Allocator::GetAllocator()->Reallocate(a_oldMem, a_newSize);
        }

        inline void Deallocate(RE::MemoryManager*, void* a_mem, const bool a_alignmentRequired)
        {
            if (a_mem != g_ZeroAddress && (a_mem != nullptr || !REL::Module::IsVR())) {
                if (a_alignmentRequired)
                    Allocator::GetAllocator()->DeallocateAligned(a_mem);
                else
                    Allocator::GetAllocator()->Deallocate(a_mem);
            }
        }

        inline std::size_t Size(RE::MemoryManager*, void* a_mem)
        {
            if (a_mem == g_ZeroAddress)
                return 0;

            return Allocator::GetAllocator()->Size(a_mem);
        }

        inline void ReplaceAllocRoutines()
        {
            REL::Relocation allocate{ RELOCATION_ID(66859, 68115) };
            REL::Relocation reallocate{ RELOCATION_ID(66860, 68116) };
            REL::Relocation deallocate{ RELOCATION_ID(66861, 68117) };
            REL::Relocation size{ RELOCATION_ID(66849, 68100) };

            allocate.replace_func(0x248, Allocate);
            reallocate.replace_func(VAR_NUM(0xA7, 0x1F6), Reallocate);
            deallocate.replace_func(0x114, Deallocate);
            size.replace_func(VAR_NUM(0x12A, 0x156), Size);
        }

        inline void StubInit()
        {
            REL::Relocation target{ RELOCATION_ID(66862, 68121) };

            target.write_fill(REL::INT3, VAR_NUM(0x9F, 0x1A7));
            target.write(REL::RET);

            REL::Relocation<std::uint32_t*> initFence{ RELOCATION_ID(514112, 400190) };
            *initFence = 2;
        }

        inline void Install()
        {
            g_ZeroAddress = new std::byte[1u << 10]{ static_cast<std::byte>(0) };

            StubInit();

            // VR: MemoryManager::Initialize (called during game startup) invokes an OS
            // allocator init routine at raw offset 0x5A2CB0. Without this patch that
            // routine runs AFTER our TBB hooks are installed and re-initialises the
            // original allocator, causing later frees to arrive at TBB with pointers it
            // never allocated → crash.  Make it return immediately.
            // Ported from EngineFixesVR memorymanager.cpp patchInit().
            if (REL::Module::IsVR()) {
                REL::Relocation<std::uintptr_t> allocatorInit{ REL::Offset{ 0x5A2CB0 } };
                allocatorInit.write(REL::RET);
                REL::Relocation<std::uintptr_t>{ REL::Offset{ 0x5A2CB1 } }.write(REL::NOP);
            }

            ReplaceAllocRoutines();
            RE::MemoryManager::GetSingleton()->RegisterMemoryManager();
            RE::BSThreadEvent::InitSDM();
        }
    }

    inline void Install()
    {
        detail::Install();
        detail::AutoScrapBuffer::Install();
        logger::info("installed global memory manager patch"sv);
    }
}
