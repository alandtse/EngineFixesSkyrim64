#pragma once

#include <array>
#include <cstdint>

// Two independent sources of a null BSSubIndexTriShape* reaching unguarded code:
//
// 1. CreateFromShapeData allocates the shape object, then uses the result completely
//    unguarded: a segment-population loop, an unconditional finalize call, a virtual
//    call through its vtable, and a shaderProperty field write. The allocator can
//    return null under memory pressure (e.g. heavy LOD streaming). CreateFromShapeData's
//    own caller already tolerates a null return (it has a fallback-allocate path), so
//    returning null cleanly here is a safe, non-crashing outcome.
// 2. sub_1404B5090's per-item loop pulls `this` from an unguarded vtable cast and feeds
//    it to FinalizeSegments, GetNumSegments, or a third accessor depending on a flag
//    bit. Several more BSSubIndexTriShape accessors share this same unguarded-`this`
//    shape, so the guard sits at the loop's `this` assignment, covering all of them.
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
                // RAX is already 0 here (that's why we branched); jumping straight into
                // the epilogue's ADD RSP is correct without touching any other register --
                // the callee-saved pops that follow restore them from the stack regardless
                // of what CreateFromShapeData did to them before this point.
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

        // FinalizeSegments has 5 call sites; only one is CreateFromShapeData's (guarded
        // above). The other 4 pull `this` from an unguarded vtable/array lookup that can
        // also yield null. It only writes into its own object's members and returns
        // void, so a null `this` can safely no-op at its own entry, covering all 5 sites.
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
                // FinalizeSegments is a leaf function (no prologue, no locals) --
                // returning here is exactly equivalent to letting it run to completion
                // on a no-op object.
                ret();

                L(resumeAddr);
                dq(a_resume);
            }
        };

        inline void InstallFinalizeSegmentsGuardForRuntime(const FinalizeSegmentsInfo& a_info)
        {
            REL::Relocation<std::uintptr_t> hook{ REL::Offset{ a_info.entryRva } };
            const auto*                     bytes = reinterpret_cast<const std::uint8_t*>(hook.address());
            const auto                      expected = FinalizeSegmentsExpectedBytes(a_info.runtimeDataOffset);
            if (!std::equal(expected.begin(), expected.end(), bytes)) {
                logger::warn("skipping SubIndexTriShapeCreateNullCrash FinalizeSegments guard: unexpected bytes at {:X}"sv,
                    a_info.entryRva);
                return;
            }

            FinalizeSegmentsPatch p{ hook.address() + expected.size(), a_info.runtimeDataOffset };
            p.ready();
            hook.write_branch<5>(SKSE::GetTrampoline().allocate(p));
            logger::info("installed SubIndexTriShapeCreateNullCrash FinalizeSegments guard"sv);
        }

        // GetNumSegments: `return this->nonSegmented ? 1 : this->numSegments;`. A null
        // `this` here returns 0, matching how a real object with numSegments == 0 would
        // behave -- callers already treat "0 segments" as a normal, no-op result.
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

        // sub_1404B5090's per-item loop pulls `this` from an unguarded vtable cast
        // (`CALL [reg+0x58]`) and, when null, feeds it to GetNumSegments,
        // FinalizeSegments, or a third accessor depending on a flag bit -- all 3 real
        // crashes observed so far trace back to this one call site. On null, this jumps
        // straight to the loop's per-item continue point, the same landing spot an
        // out-of-range or already-exhausted item already uses elsewhere in this loop.
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
                detail::InstallFinalizeSegmentsGuardForRuntime(info);
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

        if (!matched) {
            logger::warn("skipping SubIndexTriShapeCreateNullCrash guard: unsupported runtime"sv);
        }
    }
}
