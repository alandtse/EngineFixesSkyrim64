#pragma once

namespace Fixes::MapMenuCloseHandlersNullGuard
{
    // MapMenu::CloseHandlers guards each of its 5 handler-teardown slots with
    // `(counter & 0x1F) != 0`, but the counter only ever takes values 1,2,4,8,0x10 --
    // all nonzero against that mask, so the guard never actually skips a slot.
    namespace detail
    {
        inline void GuardedUnregisterAndClear(RE::MenuEventHandler** a_slot) noexcept
        {
            RE::MenuEventHandler* handler = *a_slot;
            if (!handler)
                return;

            // A torn/not-yet-constructed handler can be non-null yet still fault on
            // dereference, with no known-valid range to bounds-check against; SEH is the
            // fallback for pointers that can't be validated algorithmically.
            __try {
                RE::MenuControls::GetSingleton()->UnregisterHandler(handler);
                handler->registered = false;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                logger::warn("recovered an invalid MapMenu handler pointer in CloseHandlers"sv);
            }
        }

        struct Patch final : Xbyak::CodeGenerator
        {
            explicit Patch(std::uintptr_t a_resume)
            {
                mov(rcx, r14);  // r14 = current slot address, live at the patch site
                mov(rax, reinterpret_cast<std::uint64_t>(&GuardedUnregisterAndClear));
                call(rax);
                jmp(ptr[rip]);
                dq(a_resume);  // resume at the loop's increment/counter-rotate step
            }
        };
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;  // VR-only function, no SE/AE equivalent

        if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
            logger::warn("skipping MapMenu::CloseHandlers null guard: unsupported VR runtime"sv);
            return;
        }

        REL::Relocation<std::uintptr_t> target{ REL::Offset(0x9156a6) };  // MapMenu::CloseHandlers loop body: mov rbx,[r14]; mov rdx,rbx
        REL::Relocation<std::uintptr_t> resume{ REL::Offset(0x9156bc) };  // loop increment: rol edi,1

        static constexpr std::array<std::uint8_t, 6> kExpected{ 0x49, 0x8B, 0x1E, 0x48, 0x8B, 0xD3 };
        if (!std::equal(kExpected.begin(), kExpected.end(), reinterpret_cast<const std::uint8_t*>(target.address()))) {
            logger::warn("skipping MapMenu::CloseHandlers null guard: patch-site signature mismatch"sv);
            return;
        }

        detail::Patch p(resume.address());
        p.ready();

        auto& trampoline = SKSE::GetTrampoline();
        target.write_branch<6>(trampoline.allocate(p));

        logger::info("installed MapMenu::CloseHandlers null-handler guard (VR)"sv);
    }
}
