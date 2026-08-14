#include "allocator.h"

#include <tbb/scalable_allocator.h>

#include <Windows.h>
#include <filesystem>

namespace Memory::Allocator
{
    class CRTAllocator final : public IAllocator
    {
    public:
        [[nodiscard]] void* Allocate(std::size_t a_size) override
        {
            return malloc(a_size);
        }
        [[nodiscard]] void* AllocateAligned(std::size_t a_size, std::size_t a_alignment) override
        {
            return _aligned_malloc(a_size, a_alignment);
        }
        [[nodiscard]] void* Reallocate(void* a_oldMem, std::size_t a_newSize) override
        {
            return realloc(a_oldMem, a_newSize);
        }
        [[nodiscard]] void* ReallocateAligned(void* a_oldMem, std::size_t a_newSize, std::size_t a_alignment) override
        {
            return _aligned_realloc(a_oldMem, a_newSize, a_alignment);
        }
        size_t Size(void* a_mem) override
        {
            return _msize(a_mem);
        }
        void Deallocate(void* a_mem) override
        {
            if (!a_mem && REL::Module::IsVR())
                return;
            free(a_mem);
        }
        void DeallocateAligned(void* a_mem) override
        {
            if (!a_mem && REL::Module::IsVR())
                return;
            _aligned_free(a_mem);
        }
        void ReplaceImports() override
        {
            logger::info("imports not replaced as they already use the CRT allocator"sv);
        }
    };

    namespace detail
    {
        // Official oneTBB redistributable (vendored under extern/tbb-redist, same version as the
        // vcpkg-built tbb we link for concurrency primitives). tbbmalloc_proxy.dll performs the
        // real process-wide CRT malloc/free/realloc replacement in its own DllMain(DLL_PROCESS_
        // ATTACH), including the foreign-pointer-safe fallback (__TBB_malloc_safer_free/_realloc)
        // a hand-rolled SKSE::PatchIAT redirect cannot provide.
        //
        // All DLLs are loaded by absolute path (not relying on Windows' DLL search order) so
        // EngineFixes' own scalable_* calls and tbbmalloc_proxy's CRT redirect resolve to the
        // SAME loaded tbbmalloc.dll instance -- one shared allocator, not two independent heaps.
        using scalable_malloc_t = decltype(&::scalable_malloc);
        using scalable_calloc_t = decltype(&::scalable_calloc);
        using scalable_free_t = decltype(&::scalable_free);
        using scalable_realloc_t = decltype(&::scalable_realloc);
        using scalable_aligned_malloc_t = decltype(&::scalable_aligned_malloc);
        using scalable_aligned_free_t = decltype(&::scalable_aligned_free);
        using scalable_aligned_realloc_t = decltype(&::scalable_aligned_realloc);
        using scalable_msize_t = decltype(&::scalable_msize);

        inline scalable_malloc_t          g_scalable_malloc = nullptr;
        inline scalable_calloc_t          g_scalable_calloc = nullptr;
        inline scalable_free_t            g_scalable_free = nullptr;
        inline scalable_realloc_t         g_scalable_realloc = nullptr;
        inline scalable_aligned_malloc_t  g_scalable_aligned_malloc = nullptr;
        inline scalable_aligned_free_t    g_scalable_aligned_free = nullptr;
        inline scalable_aligned_realloc_t g_scalable_aligned_realloc = nullptr;
        inline scalable_msize_t           g_scalable_msize = nullptr;

        inline std::filesystem::path GetOwnModuleDir()
        {
            HMODULE self = nullptr;
            ::GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(&GetOwnModuleDir), &self);

            wchar_t path[MAX_PATH]{};
            ::GetModuleFileNameW(self, path, MAX_PATH);
            return std::filesystem::path(path).parent_path();
        }

        inline HMODULE g_tbbmalloc = nullptr;

        // Resolves EngineFixes' own scalable_* calls. Independent of ReplaceImports()/
        // bReplaceImports -- callers like HavokMemorySystem call through TBBAllocator's API
        // regardless of whether the process-wide CRT replacement below is enabled, so this must
        // always run once TBB is selected, not only when the user opts into CRT replacement.
        inline void LoadScalableAllocator()
        {
            const auto dir = GetOwnModuleDir();

            ::LoadLibraryW((dir / L"tbb12.dll").c_str());
            g_tbbmalloc = ::LoadLibraryW((dir / L"tbbmalloc.dll").c_str());

            if (!g_tbbmalloc) {
                logger::critical(
                    "failed to load oneTBB redistributable tbbmalloc.dll from {} -- TBB allocator selected but scalable_* calls will crash"sv,
                    dir.string());
                return;
            }

            g_scalable_malloc = reinterpret_cast<scalable_malloc_t>(::GetProcAddress(g_tbbmalloc, "scalable_malloc"));
            g_scalable_calloc = reinterpret_cast<scalable_calloc_t>(::GetProcAddress(g_tbbmalloc, "scalable_calloc"));
            g_scalable_free = reinterpret_cast<scalable_free_t>(::GetProcAddress(g_tbbmalloc, "scalable_free"));
            g_scalable_realloc = reinterpret_cast<scalable_realloc_t>(::GetProcAddress(g_tbbmalloc, "scalable_realloc"));
            g_scalable_aligned_malloc = reinterpret_cast<scalable_aligned_malloc_t>(::GetProcAddress(g_tbbmalloc, "scalable_aligned_malloc"));
            g_scalable_aligned_free = reinterpret_cast<scalable_aligned_free_t>(::GetProcAddress(g_tbbmalloc, "scalable_aligned_free"));
            g_scalable_aligned_realloc = reinterpret_cast<scalable_aligned_realloc_t>(::GetProcAddress(g_tbbmalloc, "scalable_aligned_realloc"));
            g_scalable_msize = reinterpret_cast<scalable_msize_t>(::GetProcAddress(g_tbbmalloc, "scalable_msize"));

            logger::info("oneTBB redistributable loaded from {}"sv, dir.string());
        }

        // Loads tbbmalloc_proxy.dll, whose own DllMain(DLL_PROCESS_ATTACH) performs the real
        // process-wide CRT malloc/free/realloc replacement (with the foreign-pointer-safe
        // __TBB_malloc_safer_* fallback a hand-rolled SKSE::PatchIAT redirect can't provide).
        // Depends on LoadScalableAllocator() having already loaded tbbmalloc.dll -- the proxy
        // must bind to that SAME instance, not pull in a second one, or EngineFixes' own calls
        // and the CRT-wide redirect would be two independent allocator heaps.
        inline void LoadTbbMallocProxy()
        {
            if (!g_tbbmalloc) {
                logger::critical("tbbmalloc.dll was not loaded -- refusing to install tbbmalloc_proxy CRT replacement"sv);
                return;
            }

            const auto dir = GetOwnModuleDir();
            const auto tbbmallocProxy = ::LoadLibraryW((dir / L"tbbmalloc_proxy.dll").c_str());

            if (!tbbmallocProxy) {
                logger::critical("failed to load oneTBB redistributable tbbmalloc_proxy.dll from {} -- CRT replacement did NOT install"sv, dir.string());
                return;
            }

            logger::info("tbbmalloc_proxy installed the CRT allocator replacement"sv);
        }
    }

    class TBBAllocator final : public IAllocator
    {
    public:
        TBBAllocator()
        {
            detail::LoadScalableAllocator();
        }

        [[nodiscard]] void* Allocate(std::size_t a_size) override
        {
            return detail::g_scalable_malloc(a_size);
        }
        [[nodiscard]] void* AllocateAligned(std::size_t a_size, std::size_t a_alignment) override
        {
            return detail::g_scalable_aligned_malloc(a_size, a_alignment);
        }
        [[nodiscard]] void* Reallocate(void* a_oldMem, std::size_t a_newSize) override
        {
            return detail::g_scalable_realloc(a_oldMem, a_newSize);
        }
        [[nodiscard]] void* ReallocateAligned(void* a_oldMem, std::size_t a_newSize, std::size_t a_alignment) override
        {
            return detail::g_scalable_aligned_realloc(a_oldMem, a_newSize, a_alignment);
        }
        size_t Size(void* a_mem) override
        {
            return detail::g_scalable_msize(a_mem);
        }
        void Deallocate(void* a_mem) override
        {
            if (!a_mem && REL::Module::IsVR())
                return;
            return detail::g_scalable_free(a_mem);
        }
        void DeallocateAligned(void* a_mem) override
        {
            if (!a_mem && REL::Module::IsVR())
                return;
            return detail::g_scalable_aligned_free(a_mem);
        }
        void ReplaceImports() override
        {
            // No hand-rolled IAT patching here: loading tbbmalloc_proxy.dll makes IT perform the
            // real, foreign-pointer-safe CRT replacement in its own DllMain.
            detail::LoadTbbMallocProxy();
        }
    };

    static IAllocator* _allocator;

    void SetAllocator(const AllocatorKind a_kind)
    {
        switch (a_kind) {
        case CRT:
            _allocator = new CRTAllocator();
            break;
        case TBB:
            _allocator = new TBBAllocator();
            break;
        }
    }

    IAllocator* GetAllocator() { return _allocator; }
}
