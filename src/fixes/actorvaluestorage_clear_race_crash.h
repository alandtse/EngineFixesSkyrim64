#pragma once

namespace Fixes::ActorValueStorageClearRaceCrash
{
    // ClearBaseValues frees+nulls `entries`, releases the write lock, and only then resets
    // `actorValues` to "" -- another thread can enter SetBaseValue in that window, see the
    // still-nonempty key, take the no-realloc branch, and write through the null `entries`.
    // Fix moves the reset before UnlockWrite so both become visible together; the identical
    // bug (and fix) exists in the sibling wrapper that clears LocalMap<Modifiers> before
    // tail-calling into ClearBaseValues. SetBaseValue also gets a defensive guard, since a
    // torn `entries == nullptr && actorValues != ""` state is unsafe on every write branch,
    // not just the one observed. Structurally identical across SE/AE/VR but not
    // byte-identical (AE reloads registers via LEA instead of SE/VR's cached scheme), hence
    // the differing patch sites below.
    namespace detail
    {
        inline bool BytesMatch(std::uintptr_t a_addr, std::initializer_list<std::uint8_t> a_expected)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(a_expected.begin(), a_expected.end(), p);
        }

        // Re-runs the actorValues reset (originally located after UnlockWrite) while the
        // write lock is still held, then performs the original unlock and skips the now-
        // redundant reset call further down.
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
                call(rax);  // reset the key string to "" while the write lock is still held

                mov(rcx, a_lockAddr);
                mov(rax, a_unlockFunc);
                call(rax);  // original UnlockWrite call

                jmp(ptr[rip]);
                dq(a_resume);  // skip the original (now redundant) reset call further down
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
    }

    inline void Install()
    {
        auto&      trampoline = SKSE::GetTrampoline();
        const bool isAE = REL::Module::IsAE();

        // Shared engine internals (no address-library id; same layout in all 3 runtimes,
        // only the concrete addresses differ).
        const std::uintptr_t resetFunc = REL::VariantOffset(0xC28D60, 0xCEC760, 0xC6DC90).address();
        const std::uintptr_t resetEmptyString = REL::VariantOffset(0x151F2A0, 0x1ACBCC0, 0x15965F0).address();
        const std::uintptr_t lockAddr = REL::VariantOffset(0x2F3A2B8, 0x319ACD8, 0x2FFF0D8).address();
        const std::uintptr_t unlockFunc = REL::VariantOffset(0xC075A0, 0xCC9390, 0xC42420).address();

        // --- Fix 1: ClearBaseValues -- reset actorValues before UnlockWrite, not after ---
        {
            REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(38064, 39019), VAR_NUM(0x9D, 0x96, 0x9D) };
            REL::Relocation<std::uintptr_t> resume{ RELOCATION_ID(38064, 39019), VAR_NUM(0xB6, 0xB3, 0xB6) };

            const bool matches = isAE ? detail::BytesMatch(patch.address(), { 0x48, 0x8D, 0x0D }) :
                                        detail::BytesMatch(patch.address(), { 0x49, 0x8B, 0xCE });
            if (!matches) {
                logger::warn("actor value storage clear race crash fix: unexpected bytes at ClearBaseValues patch site, skipping"sv);
            } else {
                detail::ResetBeforeUnlockPatch p(resetFunc, resetEmptyString, lockAddr, unlockFunc, resume.address(), 0);
                p.ready();
                patch.write_branch<5>(trampoline.allocate(p));
            }
        }

        // --- Fix 2: sibling wrapper -- same bug clearing LocalMap<Modifiers> at +0x10/+0x18
        //     before tail-calling into ClearBaseValues. The signature check below fails
        //     closed (skip+warn) if id 38071/39026 is stale or unresolved. ---
        {
            REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(38071, 39026), VAR_NUM(0x9D, 0x96, 0x9D) };
            REL::Relocation<std::uintptr_t> resume{ RELOCATION_ID(38071, 39026), VAR_NUM(0xA6, 0xA3, 0xA6) };

            const bool matches = isAE ? detail::BytesMatch(patch.address(), { 0x48, 0x8D, 0x0D }) :
                                        detail::BytesMatch(patch.address(), { 0x49, 0x8B, 0xCE });
            if (!matches) {
                logger::warn("actor value storage clear race crash fix: unexpected bytes at Modifiers-clear wrapper patch site, skipping"sv);
            } else {
                detail::ResetBeforeUnlockPatch p(resetFunc, resetEmptyString, lockAddr, unlockFunc, resume.address(), 0x10);
                p.ready();
                patch.write_branch<5>(trampoline.allocate(p));
            }
        }

        // --- Fix 3 (defensive backstop): SetBaseValue -- bail out on the torn state
        //     Fix 1/2 close, in case any other path can still produce it ---
        {
            REL::Relocation<std::uintptr_t> patch{ RELOCATION_ID(38062, 39017), VAR_NUM(0x4B, 0x4C, 0x4B) };
            REL::Relocation<std::uintptr_t> resume{ RELOCATION_ID(38062, 39017), VAR_NUM(0x51, 0x52, 0x51) };

            const bool matches = isAE ? detail::BytesMatch(patch.address(), { 0x48, 0x8B, 0x0F }) :
                                        detail::BytesMatch(patch.address(), { 0x48, 0x8B, 0x0E });
            if (!matches) {
                logger::warn("actor value storage clear race crash fix: unexpected bytes at SetBaseValue patch site, skipping"sv);
            } else {
                detail::NullEntriesGuardPatch p(lockAddr, unlockFunc, resume.address(), isAE);
                p.ready();
                patch.write_branch<5>(trampoline.allocate(p));
            }
        }

        logger::info("installed actor value storage clear race crash fix"sv);
    }
}
