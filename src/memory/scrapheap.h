#pragma once

#include "allocator.h"

namespace Memory::ScrapHeap
{
    namespace detail
    {
        // the scrapheap allocator will never allocate less than 0x10 bytes or less than 0x8 alignment
        // this prevents any zero-allocs as in the global memory manager
        inline void* Allocate(RE::ScrapHeap*, std::size_t a_size, std::size_t a_alignment)
        {
            if (a_size < 0x10) {
                a_size = 0x10;
            }
            if (a_alignment < 0x8) {
                a_alignment = 0x8;
            }

            return Allocator::GetAllocator()->AllocateAligned(a_size, a_alignment);
        }

        inline RE::ScrapHeap* Ctor(RE::ScrapHeap* a_self)
        {
            std::memset(a_self, 0, sizeof(RE::ScrapHeap));
            SKSE::stl::emplace_vtable(a_self);
            return a_self;
        }

        inline void Deallocate(RE::ScrapHeap*, void* a_mem)
        {
            if (!a_mem && REL::Module::IsVR())
                return;
            Allocator::GetAllocator()->DeallocateAligned(a_mem);
        }

        inline void WriteStubs()
        {
            using tuple_t = std::tuple<REL::RelocationID, std::size_t>;
            const std::array todo{
                tuple_t{ RELOCATION_ID(66891, 68152), VAR_NUM(0xC3, 0xBA) },   // Clean
                tuple_t{ RELOCATION_ID(66890, 68151), std::size_t(0x8) },      // ClearKeepPages
                tuple_t{ RELOCATION_ID(66894, 68155), std::size_t(0xF6) },     // InsertFreeBlock
                tuple_t{ RELOCATION_ID(66895, 68156), VAR_NUM(0x183, 0x185) }, // RemoveFreeBlock
                tuple_t{ RELOCATION_ID(66889, 68150), std::size_t(0x4) },      // SetKeepPages
                tuple_t{ RELOCATION_ID(66883, 68143), std::size_t(0x32) },     // dtor
            };

            for (const auto& [id, size] : todo) {
                REL::Relocation<std::uintptr_t> target{ id };
                target.write_fill(REL::INT3, size);
                target.write(REL::RET);
            }
        }

        inline void WriteHooks()
        {
            using tuple_t = std::tuple<REL::RelocationID, std::size_t, void*>;
            const std::array todo{
                tuple_t{ RELOCATION_ID(66884, 68144), VAR_NUM(0x607, 0x5E7), (void*)&Allocate },
                tuple_t{ RELOCATION_ID(66885, 68146), VAR_NUM(0x143, 0x13E), (void*)&Deallocate },
                tuple_t{ RELOCATION_ID(66882, 68142), VAR_NUM(0x12B, 0x13A), (void*)&Ctor }, // SE/VR is 299, not 296
            };

            for (const auto& [id, size, func] : todo) {
                REL::Relocation<std::uintptr_t> target{ id };
                target.replace_func(size, func);
            }
        }

        inline void Install()
        {
            WriteStubs();
            WriteHooks();
        }
    }

    inline void Install()
    {
        detail::Install();
        logger::info("installed scrapheap patch"sv);
    }
}