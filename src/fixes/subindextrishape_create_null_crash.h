#pragma once

#include <array>
#include <cstdint>

// Two independent sources of a null BSSubIndexTriShape*: CreateFromShapeData's own
// allocation can return null under memory pressure, and separate unguarded vtable casts
// elsewhere can each produce a null `this` before reaching one of this file's accessors.
// Guarding each accessor at its own entry covers every caller.
namespace Fixes::SubIndexTriShapeCreateNullCrash
{
    namespace detail
    {
        struct RuntimeInfo
        {
            REL::Version   version;
            std::uintptr_t hookRva;  // start of the displaced `MOV reg,RAX; MOV EBX,R12D` pair
            std::uintptr_t resumeRva;
            std::uintptr_t epilogueRva;  // shared epilogue's `ADD RSP,frameSize`
            bool           isRDI;        // true = RDI holds the shape pointer here, false = RSI
        };

        inline constexpr RuntimeInfo kRuntimes[] = {
            { SKSE::RUNTIME_SSE_1_5_97, 0xD7AA64, 0xD7AA6A, 0xD7AC78, true },
            { SKSE::RUNTIME_SSE_1_6_1170, 0xE55B24, 0xE55B2A, 0xE55D38, false },
            { SKSE::RUNTIME_SSE_1_7_99, 0x101B1A4, 0x101B1AA, 0x101B3B8, false },
            { SKSE::RUNTIME_VR_1_4_15, 0xDCE417, 0xDCE41D, 0xDCE6F6, true },
        };

        // MOV reg,RAX (48 8B /r) + MOV EBX,R12D (41 8B DC -- REX.B, R12D is the source).
        inline std::array<std::uint8_t, 6> ExpectedBytes(bool a_isRDI)
        {
            return a_isRDI ? std::array<std::uint8_t, 6>{ 0x48, 0x8B, 0xF8, 0x41, 0x8B, 0xDC } :
                             std::array<std::uint8_t, 6>{ 0x48, 0x8B, 0xF0, 0x41, 0x8B, 0xDC };
        }

        struct Patch final : Xbyak::CodeGenerator
        {
            Patch(std::uintptr_t a_resume, std::uintptr_t a_epilogue, const Xbyak::Reg64& a_reg)
            {
                Xbyak::Label nullLbl, resumeAddr, epilogueAddr;

                // RAX holds Allocate()'s return here -- `a_reg` hasn't been assigned yet,
                // that's the displaced instruction this patch replaces.
                test(rax, rax);
                jz(nullLbl);

                mov(a_reg, rax);  // replicate displaced MOV reg,RAX
                mov(ebx, r12d);   // replicate displaced MOV EBX,R12D
                jmp(ptr[rip + resumeAddr]);

                L(nullLbl);
                // RAX is already 0 here; the callee-saved pops after the epilogue's ADD RSP
                // restore other registers from the stack regardless, so jumping straight in is safe.
                jmp(ptr[rip + epilogueAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(epilogueAddr);
                dq(a_epilogue);
            }
        };

        inline void InstallForRuntime(const RuntimeInfo& a_info)
        {
            REL::Relocation<std::uintptr_t> hook{ REL::Offset{ a_info.hookRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(hook.address());
            const auto                      expected = ExpectedBytes(a_info.isRDI);
            if (!std::equal(expected.begin(), expected.end(), bytes)) {
                logger::warn("skipping SubIndexTriShapeCreateNullCrash guard: unexpected bytes at {:X}"sv, a_info.hookRva);
                return;
            }

            const Xbyak::Reg64 reg = a_info.isRDI ? Xbyak::util::rdi : Xbyak::util::rsi;
            Patch              p{
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_info.resumeRva } }.address(),
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_info.epilogueRva } }.address(),
                reg
            };
            p.ready();
            hook.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed SubIndexTriShapeCreateNullCrash guard"sv);
        }

        // The non-CreateFromShapeData call sites pull `this` from an unguarded lookup that
        // can yield null; it's void and only touches its own members, so a null `this`
        // can safely no-op at entry.
        struct FinalizeSegmentsInfo
        {
            REL::Version   version;
            std::uintptr_t entryRva;
            std::uintptr_t runtimeDataOffset;  // +0x160 SE/AE, +0x1A0 VR
        };

        inline constexpr FinalizeSegmentsInfo kFinalizeSegmentsRuntimes[] = {
            { SKSE::RUNTIME_SSE_1_5_97, 0xD59360, 0x160 },
            { SKSE::RUNTIME_SSE_1_6_1170, 0xE310B0, 0x160 },
            { SKSE::RUNTIME_SSE_1_7_99, 0xFF6520, 0x160 },
            { SKSE::RUNTIME_VR_1_4_15, 0xDA2470, 0x1A0 },
        };

        // MOV RAX,[RCX+disp32]: 48 8B 81 <disp32 LE>.
        inline std::array<std::uint8_t, 7> FinalizeSegmentsExpectedBytes(std::uintptr_t a_runtimeDataOffset)
        {
            const auto disp = static_cast<std::uint32_t>(a_runtimeDataOffset);
            return { 0x48, 0x8B, 0x81, static_cast<std::uint8_t>(disp), static_cast<std::uint8_t>(disp >> 8),
                static_cast<std::uint8_t>(disp >> 16), static_cast<std::uint8_t>(disp >> 24) };
        }

        struct FinalizeSegmentsPatch final : Xbyak::CodeGenerator
        {
            FinalizeSegmentsPatch(std::uintptr_t a_resume, std::uintptr_t a_runtimeDataOffset)
            {
                Xbyak::Label nullLbl, resumeAddr;

                test(rcx, rcx);
                jz(nullLbl);

                mov(rax, qword[rcx + a_runtimeDataOffset]);  // replicate displaced MOV RAX,[RCX+offset]
                jmp(ptr[rip + resumeAddr]);

                L(nullLbl);
                // Leaf function, no prologue/locals -- returning here is equivalent to
                // letting it finish on a no-op object.
                ret();

                L(resumeAddr);
                dq(a_resume);
            }
        };

        inline void InstallNullThisAtOffsetGuardForRuntime(const FinalizeSegmentsInfo& a_info, std::string_view a_name)
        {
            REL::Relocation<std::uintptr_t> hook{ REL::Offset{ a_info.entryRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(hook.address());
            const auto                      expected = FinalizeSegmentsExpectedBytes(a_info.runtimeDataOffset);
            if (!std::equal(expected.begin(), expected.end(), bytes)) {
                logger::warn("skipping SubIndexTriShapeCreateNullCrash {} guard: unexpected bytes at {:X}"sv, a_name,
                    a_info.entryRva);
                return;
            }

            FinalizeSegmentsPatch p{ hook.address() + expected.size(), a_info.runtimeDataOffset };
            p.ready();
            hook.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed SubIndexTriShapeCreateNullCrash {} guard"sv, a_name);
        }

        // Same unguarded-`this` sources as FinalizeSegments above, reaching the same
        // `MOV RAX,[RCX+offset]` load, so this guard reuses its shape.
        inline constexpr FinalizeSegmentsInfo kRefreshSegmentActiveFlagRuntimes[] = {
            { SKSE::RUNTIME_SSE_1_5_97, 0xD593E6, 0x160 },
            { SKSE::RUNTIME_SSE_1_6_1170, 0xE31136, 0x160 },
            { SKSE::RUNTIME_SSE_1_7_99, 0xFF65A6, 0x160 },
            { SKSE::RUNTIME_VR_1_4_15, 0xDA24F6, 0x1A0 },
        };

        inline constexpr FinalizeSegmentsInfo kClearSegmentActiveFlagRuntimes[] = {
            { SKSE::RUNTIME_SSE_1_5_97, 0xD59416, 0x160 },
            { SKSE::RUNTIME_SSE_1_6_1170, 0xE31166, 0x160 },
            { SKSE::RUNTIME_SSE_1_7_99, 0xFF65D6, 0x160 },
            { SKSE::RUNTIME_VR_1_4_15, 0xDA2526, 0x1A0 },
        };

        // GetNumSegments returns `nonSegmented ? 1 : numSegments`; a null `this` returning
        // 0 matches how a real zero-segment object already behaves.
        struct GetNumSegmentsInfo
        {
            REL::Version   version;
            std::uintptr_t entryRva;
            std::uintptr_t nonSegmentedOffset;  // +0x171 SE/AE, +0x1B1 VR
            std::uintptr_t numSegmentsOffset;   // +0x168 SE/AE, +0x1A8 VR
        };

        inline constexpr GetNumSegmentsInfo kGetNumSegmentsRuntimes[] = {
            { SKSE::RUNTIME_SSE_1_5_97, 0x4B66A0, 0x171, 0x168 },
            { SKSE::RUNTIME_SSE_1_6_1170, 0x5129A0, 0x171, 0x168 },
            { SKSE::RUNTIME_SSE_1_7_99, 0x51A6C0, 0x171, 0x168 },
            { SKSE::RUNTIME_VR_1_4_15, 0x4C6780, 0x1B1, 0x1A8 },
        };

        // CMP byte ptr [RCX+disp32],0 (80 B9 <disp32 LE> 00) + MOV EAX,1 (B8 01 00 00 00).
        inline std::array<std::uint8_t, 12> GetNumSegmentsExpectedBytes(std::uintptr_t a_nonSegmentedOffset)
        {
            const auto disp = static_cast<std::uint32_t>(a_nonSegmentedOffset);
            return { 0x80, 0xB9, static_cast<std::uint8_t>(disp), static_cast<std::uint8_t>(disp >> 8),
                static_cast<std::uint8_t>(disp >> 16), static_cast<std::uint8_t>(disp >> 24), 0x00, 0xB8, 0x01, 0x00,
                0x00, 0x00 };
        }

        struct GetNumSegmentsPatch final : Xbyak::CodeGenerator
        {
            GetNumSegmentsPatch(std::uintptr_t a_resume, std::uintptr_t a_nonSegmentedOffset)
            {
                Xbyak::Label nullLbl, resumeAddr;

                test(rcx, rcx);
                jz(nullLbl);

                cmp(byte[rcx + a_nonSegmentedOffset], 0);  // replicate displaced CMP
                mov(eax, 1);                               // replicate displaced MOV EAX,1
                jmp(ptr[rip + resumeAddr]);

                L(nullLbl);
                xor_(eax, eax);  // return 0 segments
                ret();

                L(resumeAddr);
                dq(a_resume);
            }
        };

        inline void InstallGetNumSegmentsGuardForRuntime(const GetNumSegmentsInfo& a_info)
        {
            REL::Relocation<std::uintptr_t> hook{ REL::Offset{ a_info.entryRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(hook.address());
            const auto                      expected = GetNumSegmentsExpectedBytes(a_info.nonSegmentedOffset);
            if (!std::equal(expected.begin(), expected.end(), bytes)) {
                logger::warn("skipping SubIndexTriShapeCreateNullCrash GetNumSegments guard: unexpected bytes at {:X}"sv,
                    a_info.entryRva);
                return;
            }

            GetNumSegmentsPatch p{ hook.address() + expected.size(), a_info.nonSegmentedOffset };
            p.ready();
            hook.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed SubIndexTriShapeCreateNullCrash GetNumSegments guard"sv);
        }

        // Void, safe to no-op on null like FinalizeSegments. SE/VR have an extra
        // `SUB RSP,0x8` prologue AE1170/AE1799 lack; hooking the true entry lets the null
        // path `ret` immediately on every runtime.
        struct RecomputeSegmentDataInfo
        {
            REL::Version                 version;
            std::uintptr_t               entryRva;
            std::array<std::uint8_t, 11> displacedBytes;
            std::uint8_t                 displacedLen;  // 7 (AE1170/AE1799) or 11 (SE/VR)
        };

        inline constexpr RecomputeSegmentDataInfo kRecomputeSegmentDataRuntimes[] = {
            // SE: SUB RSP,0x8; CMP byte ptr[RCX+0x170],0
            { SKSE::RUNTIME_SSE_1_5_97, 0xD59430,
                { 0x48, 0x83, 0xEC, 0x08, 0x80, 0xB9, 0x70, 0x01, 0x00, 0x00, 0x00 }, 11 },
            // AE 1.6.1170: CMP byte ptr[RCX+0x170],0 (no SUB RSP)
            { SKSE::RUNTIME_SSE_1_6_1170, 0xE31180, { 0x80, 0xB9, 0x70, 0x01, 0x00, 0x00, 0x00 }, 7 },
            // AE 1.7.99: CMP byte ptr[RCX+0x170],0 (no SUB RSP)
            { SKSE::RUNTIME_SSE_1_7_99, 0xFF65F0, { 0x80, 0xB9, 0x70, 0x01, 0x00, 0x00, 0x00 }, 7 },
            // VR: SUB RSP,0x8; CMP byte ptr[RCX+0x1B0],0
            { SKSE::RUNTIME_VR_1_4_15, 0xDA2540,
                { 0x48, 0x83, 0xEC, 0x08, 0x80, 0xB9, 0xB0, 0x01, 0x00, 0x00, 0x00 }, 11 },
        };

        struct RecomputeSegmentDataPatch final : Xbyak::CodeGenerator
        {
            RecomputeSegmentDataPatch(std::uintptr_t a_resume, const std::uint8_t* a_bytes, std::uint8_t a_len)
            {
                Xbyak::Label nullLbl, resumeAddr;

                test(rcx, rcx);
                jz(nullLbl);

                for (std::uint8_t i = 0; i < a_len; ++i) {
                    db(a_bytes[i]);
                }
                jmp(ptr[rip + resumeAddr]);

                L(nullLbl);
                ret();

                L(resumeAddr);
                dq(a_resume);
            }
        };

        inline void InstallRecomputeSegmentDataGuardForRuntime(const RecomputeSegmentDataInfo& a_info)
        {
            REL::Relocation<std::uintptr_t> hook{ REL::Offset{ a_info.entryRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(hook.address());
            if (!std::equal(a_info.displacedBytes.begin(), a_info.displacedBytes.begin() + a_info.displacedLen,
                    bytes)) {
                logger::warn(
                    "skipping SubIndexTriShapeCreateNullCrash RecomputeSegmentData guard: unexpected bytes at {:X}"sv,
                    a_info.entryRva);
                return;
            }

            RecomputeSegmentDataPatch p{ hook.address() + a_info.displacedLen, a_info.displacedBytes.data(),
                a_info.displacedLen };
            p.ready();
            hook.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed SubIndexTriShapeCreateNullCrash RecomputeSegmentData guard"sv);
        }

        // sub_1404B5090's per-item loop pulls `this` from an unguarded vtable cast; on
        // null, jump to the loop's own per-item continue point instead of calling in.
        struct CallerGuardInfo
        {
            REL::Version                version;
            std::uintptr_t              hookRva;  // start of the displaced `MOV this,RAX; TEST flag,flag` pair
            std::uintptr_t              skipRva;  // loop's per-item continue point
            std::array<std::uint8_t, 6> displacedBytes;
            std::uint8_t                displacedLen;  // 5 or 6; trailing bytes in displacedBytes are unused padding
        };

        inline constexpr CallerGuardInfo kSub1404B5090Runtimes[] = {
            // SE: MOV RDI,RAX; TEST SIL,SIL
            { SKSE::RUNTIME_SSE_1_5_97, 0x4B5111, 0x4B51E9, { 0x48, 0x8B, 0xF8, 0x40, 0x84, 0xF6 }, 6 },
            // AE 1.6.1170: MOV R15,RAX; TEST SIL,SIL
            { SKSE::RUNTIME_SSE_1_6_1170, 0x511151, 0x5112B9, { 0x4C, 0x8B, 0xF8, 0x40, 0x84, 0xF6 }, 6 },
            // AE 1.7.99: MOV R15,RAX; TEST BL,BL (no REX needed on BL)
            { SKSE::RUNTIME_SSE_1_7_99, 0x518DE1, 0x518F67, { 0x4C, 0x8B, 0xF8, 0x84, 0xDB, 0x00 }, 5 },
            // VR: MOV RDI,RAX; TEST SIL,SIL
            { SKSE::RUNTIME_VR_1_4_15, 0x4C51F1, 0x4C52C9, { 0x48, 0x8B, 0xF8, 0x40, 0x84, 0xF6 }, 6 },
        };

        struct CallerGuardPatch final : Xbyak::CodeGenerator
        {
            CallerGuardPatch(std::uintptr_t a_resume, std::uintptr_t a_skip, const std::uint8_t* a_bytes,
                std::uint8_t a_len)
            {
                Xbyak::Label nullLbl, resumeAddr, skipAddr;

                // RAX holds the vtable-cast result here. The displaced bytes below are
                // replicated verbatim since the register they use differs per runtime.
                test(rax, rax);
                jz(nullLbl);

                for (std::uint8_t i = 0; i < a_len; ++i) {
                    db(a_bytes[i]);
                }
                jmp(ptr[rip + resumeAddr]);

                L(nullLbl);
                jmp(ptr[rip + skipAddr]);

                L(resumeAddr);
                dq(a_resume);
                L(skipAddr);
                dq(a_skip);
            }
        };

        inline void InstallCallerGuardForRuntime(const CallerGuardInfo& a_info)
        {
            REL::Relocation<std::uintptr_t> hook{ REL::Offset{ a_info.hookRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(hook.address());
            if (!std::equal(a_info.displacedBytes.begin(), a_info.displacedBytes.begin() + a_info.displacedLen,
                    bytes)) {
                logger::warn("skipping SubIndexTriShapeCreateNullCrash caller guard: unexpected bytes at {:X}"sv,
                    a_info.hookRva);
                return;
            }

            CallerGuardPatch p{ hook.address() + a_info.displacedLen,
                REL::Relocation<std::uintptr_t>{ REL::Offset{ a_info.skipRva } }.address(), a_info.displacedBytes.data(),
                a_info.displacedLen };
            p.ready();
            hook.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed SubIndexTriShapeCreateNullCrash caller guard"sv);
        }
    }

    inline void Install()
    {
        const auto currentVersion = REL::Module::get().version();
        bool       matched = false;

        for (const auto& info : detail::kRuntimes) {
            if (currentVersion == info.version) {
                detail::InstallForRuntime(info);
                matched = true;
                break;
            }
        }

        for (const auto& info : detail::kFinalizeSegmentsRuntimes) {
            if (currentVersion == info.version) {
                detail::InstallNullThisAtOffsetGuardForRuntime(info, "FinalizeSegments"sv);
                matched = true;
                break;
            }
        }

        for (const auto& info : detail::kRefreshSegmentActiveFlagRuntimes) {
            if (currentVersion == info.version) {
                detail::InstallNullThisAtOffsetGuardForRuntime(info, "RefreshSegmentActiveFlag"sv);
                matched = true;
                break;
            }
        }

        for (const auto& info : detail::kClearSegmentActiveFlagRuntimes) {
            if (currentVersion == info.version) {
                detail::InstallNullThisAtOffsetGuardForRuntime(info, "ClearSegmentActiveFlag"sv);
                matched = true;
                break;
            }
        }

        for (const auto& info : detail::kGetNumSegmentsRuntimes) {
            if (currentVersion == info.version) {
                detail::InstallGetNumSegmentsGuardForRuntime(info);
                matched = true;
                break;
            }
        }

        for (const auto& info : detail::kSub1404B5090Runtimes) {
            if (currentVersion == info.version) {
                detail::InstallCallerGuardForRuntime(info);
                matched = true;
                break;
            }
        }

        for (const auto& info : detail::kRecomputeSegmentDataRuntimes) {
            if (currentVersion == info.version) {
                detail::InstallRecomputeSegmentDataGuardForRuntime(info);
                matched = true;
                break;
            }
        }

        if (!matched) {
            logger::warn("skipping SubIndexTriShapeCreateNullCrash guard: unsupported runtime"sv);
        }
    }
}
