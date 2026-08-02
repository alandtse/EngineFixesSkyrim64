#pragma once

namespace Fixes::ActorValueStorageClearRaceCrash
{
    // Vanilla lock-scope bug in Actor::ActorValueStorage::LocalMap<float>::ClearBaseValues
    // (id 38064) leading to a null-pointer write in the sibling SetBaseValue (id 38062).
    //
    // ClearBaseValues frees `entries`, nulls it, then calls UnlockWrite -- and only *after*
    // the lock is released does it reset the `actorValues` key string to "". Between the
    // unlock and that reset, another thread can enter SetBaseValue's write lock and see the
    // still-nonempty `actorValues` (so it takes the "key already present"/no-realloc branch)
    // with `entries` already null, and writes through the null pointer.
    // Observed: AE 1.6.1170 EXCEPTION_ACCESS_VIOLATION writing 0x4 (`movss [rbp+rbx*4],xmm6`,
    // rbp == 0) inside SetBaseValue.
    //
    // Fix: move the `actorValues` reset to *before* UnlockWrite so `entries == nullptr` and
    // `actorValues == ""` become visible together, closing the torn window. The identical
    // bug exists in the neighboring wrapper that clears the sibling LocalMap<Modifiers> (at
    // this+0x10/this+0x18) before tail-calling into ClearBaseValues (id 38071/39026); same
    // fix applied there.
    // A defensive guard is also added to SetBaseValue itself as a backstop, since a torn
    // `entries == nullptr && actorValues != ""` state -- however it arises -- is unsafe on
    // every branch of SetBaseValue, not just the one hit in the crash log:
    //   - the "key already present" branch writes `entries[index]` directly (no null check)
    //   - the "insert, no realloc needed" branch (odd HasValue() parity) shifts and writes
    //     into `entries` directly
    //   - the "insert, realloc needed" branch (even parity) allocates a *new* buffer but
    //     then copies from the old (null) `entries` at an offset near address 0, in the
    //     copy-in loop -- a different faulting address, same class of bug
    //
    // Verified identical structure (ClearBaseValues, the Modifiers wrapper, and SetBaseValue)
    // in SE 1.5.97, AE 1.6.1170 and VR 1.4.15, though not byte-identical: AE caches neither
    // the write-lock global's address nor `this` in the same registers SE/VR do (SE/VR use a
    // cached R14/RSI-RDI register scheme; AE reloads via LEA/RDI each time), so patch sites
    // and byte lengths differ per runtime (see VAR_NUM / signatures below).
    namespace detail
    {
        inline bool BytesMatch(std::uintptr_t a_addr, std::initializer_list<std::uint8_t> a_expected)
        {
            const auto* p = reinterpret_cast<const std::uint8_t*>(a_addr);
            return std::equal(a_expected.begin(), a_expected.end(), p);
        }

        // Displaces the original "restore lock ptr into rcx; call UnlockWrite" sequence
        // (8 bytes on SE/VR via cached R14, 12 bytes on AE via a fresh LEA) found at the end
        // of ClearBaseValues' and the Modifiers-wrapper's free-guarded block. Re-runs the
        // reset call (previously located further down, after the original unlock) while the
        // write lock is still held, performs the original unlock, then jumps past the
        // now-redundant reset call in the original code.
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

        // Defensive backstop in SetBaseValue: bail out (still under the write lock) if
        // `entries` is null but the key string is non-empty -- a torn state where every
        // write branch below is unsafe. `entries == nullptr && length == 0` is the
        // legitimate first-insert case and must fall through unchanged.
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
                // Hash verified 2026-08-01: 0 code xrefs, 0 stored/vtable pointers into the
                // clobbered+orphan range on SE/AE or VR.
                patch.write_branch<5>(trampoline.allocate(p), false, 0xA0EC9A68EA31A37BULL);
            }
        }

        // --- Fix 2: sibling wrapper -- same bug clearing LocalMap<Modifiers> at +0x10/+0x18
        //     before tail-calling into ClearBaseValues. id 38071/39026 registered in
        //     skyrim_vr_address_library#176; requires that PR to merge and a new release
        //     to ship before this id resolves (same dependency as #176's other ids). The
        //     signature check below still fails closed (skip+warn) if the id is stale or
        //     unresolved.
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
                // Hash verified 2026-08-01: 0 code xrefs, 0 stored/vtable pointers into the
                // clobbered+orphan range on SE/AE or VR.
                patch.write_branch<5>(trampoline.allocate(p), false, 0xB0ADFBE81818D7D1ULL);
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
                // Hash verified 2026-08-01: 0 code xrefs, 0 stored/vtable pointers into the
                // clobbered+orphan range on SE/AE or VR.
                patch.write_branch<5>(trampoline.allocate(p), false, 0x4ABBE119CECF34F7ULL);
            }
        }

        logger::info("installed actor value storage clear race crash fix"sv);
    }
}
