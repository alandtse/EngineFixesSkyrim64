#pragma once

namespace Fixes::ActorValueStorageClearRaceCrash
{
    // ClearBaseValues unlocks before resetting `actorValues`; a racing SetBaseValue can see
    // the torn state and write through the freed `entries`. Moves the reset before unlock,
    // in both ClearBaseValues and its sibling wrapper, plus a defensive SetBaseValue guard.
    namespace detail
    {
        inline bool BytesMatch(std::uintptr_t a_addr, std::initializer_list<std::uint8_t> a_expected)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(a_expected.begin(), a_expected.end(), p);
        }

        // Runs the key-string reset while the write lock is still held, then unlocks.
        struct ResetBeforeUnlockPatch final : Xbyak::CodeGenerator
        {
            // a_fieldOffset: 0 for ClearBaseValues' own actorValues (LocalMap<float>), 0x10
            // for the wrapper's inlined LocalMap<Modifiers> key string.
            ResetBeforeUnlockPatch(
                std::uintptr_t a_resetFunc,
                std::uintptr_t a_resetEmptyString,
                std::uintptr_t a_lockAddr,
                std::uintptr_t a_unlockFunc,
                std::uintptr_t a_resume,
                std::int32_t   a_fieldOffset)
            {
                if (a_fieldOffset == 0) {
                    mov(rcx, rdi);
                } else {
                    lea(rcx, ptr[rdi + a_fieldOffset]);
                }
                mov(rdx, a_resetEmptyString);
                mov(rax, a_resetFunc);
                call(rax);

                mov(rcx, a_lockAddr);
                mov(rax, a_unlockFunc);
                call(rax);

                jmp(ptr[rip]);
                dq(a_resume);
            }
        };

        // Bails out under lock on a torn `entries == nullptr && length != 0` state;
        // `length == 0` is the legitimate first-insert case and falls through unchanged.
        struct NullEntriesGuardPatch final : Xbyak::CodeGenerator
        {
            NullEntriesGuardPatch(
                std::uintptr_t a_lockAddr,
                std::uintptr_t a_unlockFunc,
                std::uintptr_t a_resume,
                bool           a_isAE)
            {
                Xbyak::Label continueLbl;
                const auto&  thisReg = a_isAE ? rdi : rsi;

                mov(rax, qword[thisReg + 0x8]);  // entries
                test(rax, rax);
                jnz(continueLbl);
                mov(rax, qword[thisReg]);  // actorValues._data
                movzx(eax, byte[rax]);
                test(al, al);
                jz(continueLbl);  // length == 0: legitimate first-insert, proceed normally

                // entries == nullptr && length != 0: torn state, bail out under lock
                mov(rcx, a_lockAddr);
                mov(rax, a_unlockFunc);
                call(rax);
                ret();

                L(continueLbl);
                mov(rcx, qword[thisReg]);  // re-run displaced: MOV RCX,[this]
                mov(rbx, rcx);             // re-run displaced: MOV RBX,RCX
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };
        template <typename PatchFactory>
        inline void InstallGuardedSite(REL::Relocation<std::uintptr_t> a_patch,
            std::initializer_list<std::uint8_t> a_expectedSEVR, std::initializer_list<std::uint8_t> a_expectedAE,
            bool a_isAE, const char* a_siteName, SKSE::Trampoline& a_trampoline, PatchFactory&& a_makePatch)
        {
            const bool matches =
                a_isAE ? BytesMatch(a_patch.address(), a_expectedAE) : BytesMatch(a_patch.address(), a_expectedSEVR);
            if (!matches) {
                logger::warn("actor value storage clear race crash fix: unexpected bytes at {} patch site, skipping"sv,
                    a_siteName);
                return;
            }
            auto p = a_makePatch();
            p.ready();
            a_patch.write_branch<5>(a_trampoline.allocate(p));
        }
    }

    inline void Install()
    {
        auto&      trampoline = SKSE::GetTrampoline();
        const bool isAE = REL::Module::IsAE();

        const std::uintptr_t resetFunc = REL::VariantOffset(0xC28D60, 0xCEC760, 0xC6DC90).address();
        const std::uintptr_t resetEmptyString = REL::VariantOffset(0x151F2A0, 0x1ACBCC0, 0x15965F0).address();
        const std::uintptr_t lockAddr = REL::VariantOffset(0x2F3A2B8, 0x319ACD8, 0x2FFF0D8).address();
        const std::uintptr_t unlockFunc = REL::VariantOffset(0xC075A0, 0xCC9390, 0xC42420).address();

        // Fix 1: ClearBaseValues -- reset actorValues before UnlockWrite, not after.
        {
            REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(38064, 39019), VAR_NUM(0x9D, 0x96, 0x9D) };
            REL::Relocation<std::uintptr_t> resume{ RELOCATION_ID(38064, 39019), VAR_NUM(0xB6, 0xB3, 0xB6) };
            detail::InstallGuardedSite(
                patch, { 0x49, 0x8B, 0xCE }, { 0x48, 0x8D, 0x0D }, isAE, "ClearBaseValues", trampoline, [&] {
                    return detail::ResetBeforeUnlockPatch(
                        resetFunc, resetEmptyString, lockAddr, unlockFunc, resume.address(), 0);
                });
        }

        // Fix 2: sibling wrapper -- same bug clearing LocalMap<Modifiers> before
        // tail-calling into ClearBaseValues.
        {
            REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(38071, 39026), VAR_NUM(0x9D, 0x96, 0x9D) };
            REL::Relocation<std::uintptr_t> resume{ RELOCATION_ID(38071, 39026), VAR_NUM(0xA6, 0xA3, 0xA6) };
            detail::InstallGuardedSite(patch, { 0x49, 0x8B, 0xCE }, { 0x48, 0x8D, 0x0D }, isAE, "Modifiers-clear wrapper",
                trampoline, [&] {
                    return detail::ResetBeforeUnlockPatch(
                        resetFunc, resetEmptyString, lockAddr, unlockFunc, resume.address(), 0x10);
                });
        }

        // Fix 3 (defensive backstop): SetBaseValue -- bail out on the torn state Fix 1/2
        // close, in case any other path can still produce it.
        {
            REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(38062, 39017), VAR_NUM(0x4B, 0x4C, 0x4B) };
            REL::Relocation<std::uintptr_t> resume{ RELOCATION_ID(38062, 39017), VAR_NUM(0x51, 0x52, 0x51) };
            detail::InstallGuardedSite(
                patch, { 0x48, 0x8B, 0x0E }, { 0x48, 0x8B, 0x0F }, isAE, "SetBaseValue", trampoline, [&] {
                    return detail::NullEntriesGuardPatch(lockAddr, unlockFunc, resume.address(), isAE);
                });
        }

        logger::info("installed actor value storage clear race crash fix"sv);
    }
}
