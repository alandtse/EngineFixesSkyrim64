#pragma once

namespace Fixes::GCArrayCleanupBug
{
    namespace detail
    {
        struct Patch16
        {
            std::uint16_t offset;
            std::uint16_t orig;
            std::uint16_t patch;
        };

        // VR is byte-identical to SE at these offsets; AE's wider inline array-size
        // field shifts the same edits further in (kPatchArrAE/kPatchObjAE below).
        constexpr Patch16 kPatchSE[] = {
            { 0x12C, 0x0C75, 0x0A75 },
            { 0x130, 0x0872, 0x0675 },
            { 0x136, 0x02EB, 0x9066 },
            { 0x138, 0xC3FF, 0x0000 },
        };
        constexpr Patch16 kPatchArrAE[] = {
            { 0x148, 0x1674, 0x1474 },
            { 0x151, 0x0D72, 0x0B72 },
            { 0x15C, 0x02EB, 0x9066 },
            { 0x15E, 0xC5FF, 0x0000 },
        };
        constexpr Patch16 kPatchObjAE[] = {
            { 0x152, 0x1674, 0x1474 },
            { 0x15B, 0x0D72, 0x0B72 },
            { 0x166, 0x02EB, 0x9066 },
            { 0x168, 0xC5FF, 0x0000 },
        };

        inline bool Apply(std::uintptr_t a_base, std::span<const Patch16> a_patch, std::string_view a_name)
        {
            for (const auto& p : a_patch) {
                std::uint16_t cur;
                std::memcpy(&cur, reinterpret_cast<const void*>(a_base + p.offset), sizeof(cur));
                if (cur != p.orig) {
                    logger::error("GCArrayCleanupBug: {} AOB mismatch at +{:#x}: expected {:#x}, found {:#x}"sv, a_name, p.offset, p.orig, cur);
                    return false;
                }
            }

            for (const auto& p : a_patch) {
                const std::array<std::byte, 2> bytes{
                    static_cast<std::byte>(p.patch & 0xFF),
                    static_cast<std::byte>((p.patch >> 8) & 0xFF),
                };
                REL::Relocation<std::uintptr_t>{ a_base + p.offset }.write(std::span<const std::byte>{ bytes });
            }
            return true;
        }
    }

    // Fixes a bug in the Papyrus VM's incremental garbage collector: ProcessArrayCleanup/
    // ProcessObjectCleanup can exit their cleanup loop after a single collected entry,
    // forgoing the rest of the frame's time budget and causing growing script lag and
    // save/load hitches. Ported from InTheBottle/SkyrimSE-gc-bug-fix (GPL-3.0 with
    // modding exception), itself a port of Nukem9/fallout4-gc-bug-fix.
    //
    // VR resolves via a raw module offset rather than RELOCATION_ID: ids 98217/98218
    // aren't in the currently-vendored VR address library release yet.
    inline void Install()
    {
        const auto base = REL::Module::get().base();

        const std::uintptr_t arrAddr = REL::Module::IsVR() ? base + 0x28D1F0 : REL::RelocationID(98217, 104859).address();
        const std::uintptr_t objAddr = REL::Module::IsVR() ? base + 0x28D380 : REL::RelocationID(98218, 104860).address();

        const bool isAE = REL::Module::IsAE();
        const bool arrOk = detail::Apply(arrAddr, isAE ? std::span{ detail::kPatchArrAE } : std::span{ detail::kPatchSE }, "GC_Arr"sv);
        const bool objOk = detail::Apply(objAddr, isAE ? std::span{ detail::kPatchObjAE } : std::span{ detail::kPatchSE }, "GC_Obj"sv);

        if (arrOk && objOk)
            logger::info("installed GC array/object cleanup bug fix"sv);
    }
}
