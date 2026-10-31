#pragma once

#include "detail/projected_menu_order.h"
#include "installed_fixes.h"
#include "settings.h"

namespace Fixes::MessageBoxMainMenu
{
    namespace detail
    {
        inline constexpr std::ptrdiff_t kBodyOffset = 0x250;
        inline constexpr std::ptrdiff_t kNextOffset = 0x345;
        inline constexpr std::ptrdiff_t kLoopOffset = 0x214;
        inline constexpr auto           kProjectedTarget = static_cast<std::int32_t>(RE::VRMenuRenderTarget::kProjectedMenu);
        inline constexpr auto           kNoRenderTarget = static_cast<std::int32_t>(RE::VRMenuRenderTarget::kNone);
        inline constexpr std::size_t    kIteratorScratchOffset = 0x50;
        inline std::uintptr_t           mainVtable;
        inline std::uintptr_t           mistVtable;
        inline std::uintptr_t           messageVtable;
        inline std::uintptr_t           faderVtable;
        inline std::uintptr_t           baseDisplay;
        using Slot = RE::GPtr<RE::IMenu>;
        static_assert(sizeof(Slot) == sizeof(void*));

        inline MenuRole Classify(RE::IMenu* a_menu) noexcept
        {
            if (!a_menu)
                return MenuRole::kBlocked;
            const auto vtable = *reinterpret_cast<const std::uintptr_t*>(a_menu);
            if (vtable == mainVtable)
                return MenuRole::kMain;
            if (vtable == mistVtable)
                return MenuRole::kMist;
            if (vtable == faderVtable)
                return MenuRole::kBlocked;

            const auto target = a_menu->GetVRRuntimeData()->renderTarget.underlying();
            // Native draw begins at the last topmost menu; never rotate across that boundary.
            if ((target != kNoRenderTarget && target != kProjectedTarget) ||
                a_menu->menuFlags.any(
                    RE::IMenu::Flag::kTopmostRenderedMenu,
                    RE::IMenu::Flag::kRendersOffscreenTargets,
                    RE::IMenu::Flag::kCustomRendering) ||
                reinterpret_cast<const std::uintptr_t*>(vtable)[6] != baseDisplay)
                return MenuRole::kBlocked;
            return vtable == messageVtable ? MenuRole::kMessageBox : MenuRole::kProjected;
        }

        inline Slot* SelectSlot(RE::UI* a_ui, Slot* a_iterator) noexcept
        {
            const auto  count = a_ui->menuStack.size();
            auto* const slots = a_ui->menuStack.data();
            const auto  begin = reinterpret_cast<std::uintptr_t>(slots);
            const auto  current = reinterpret_cast<std::uintptr_t>(a_iterator);
            if (!slots || count > kMaxMenuCount || current < begin ||
                (current - begin) % sizeof(Slot) != 0 || (current - begin) / sizeof(Slot) >= count)
                return a_iterator;
            const auto index = (current - begin) / sizeof(Slot);
            return slots + SelectRenderSlot(count, index, [slots](std::size_t i) noexcept { return Classify(slots[i].get()); });
        }

        class TestPopup final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
        {
        public:
            static TestPopup& GetSingleton()
            {
                static TestPopup singleton;
                return singleton;
            }

            void Arm()
            {
                if (armed_ || queued_)
                    return;
                auto* ui = RE::UI::GetSingleton();
                if (!ui || !SKSE::GetTaskInterface()) {
                    logger::warn("MessageBox main-menu test unavailable: UI or task interface missing"sv);
                    return;
                }
                armed_ = true;
                ui->AddEventSink(this);
                Request();
                logger::info("MessageBox main-menu test armed"sv);
            }

        private:
            // Match RE::DebugMessageBox; CommonLib exposes integer parameters, not enums.
            static constexpr std::uint8_t kButtonPressOffset = 0;
            static constexpr std::int32_t kDebugMessageBoxWarningType = 4;
            static constexpr std::int32_t kDebugMessageBoxDepth = 10;

            TestPopup() = default;
            TestPopup(const TestPopup&) = delete;
            TestPopup& operator=(const TestPopup&) = delete;

            static void Answered(std::uint8_t a_index)
            {
                logger::info("MessageBox main-menu test answered: button {}"sv, a_index);
            }

            void Request()
            {
                if (pending_ || queued_)
                    return;
                pending_ = true;
                SKSE::GetTaskInterface()->AddTask([this]() {
                    pending_ = false;
                    auto* ui = RE::UI::GetSingleton();
                    if (queued_ || !ui || !ui->IsMenuOpen(RE::MainMenu::MENU_NAME) ||
                        !ui->IsMenuOpen(RE::MistMenu::MENU_NAME) ||
                        ui->IsMenuOpen(RE::LoadingMenu::MENU_NAME) ||
                        ui->IsMenuOpen(RE::MessageBoxMenu::MENU_NAME))
                        return;
                    queued_ = RE::MessageBoxMenu::Create(
                        "Engine Fixes: main-menu MessageBox test. Select OK to dismiss. "
                        "Disable bMessageBoxMainMenuTest in EngineFixes.toml to stop this startup test.",
                        Answered, kButtonPressOffset, kDebugMessageBoxWarningType, kDebugMessageBoxDepth, "OK");
                    if (queued_) {
                        ui->RemoveEventSink(this);
                        logger::info("MessageBox main-menu test queued (render fix setting: {})"sv,
                            Settings::Fixes::bMessageBoxMainMenu.GetValue());
                    } else {
                        logger::warn("MessageBox main-menu test rejected by native creation guards"sv);
                    }
                });
            }

            RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event,
                RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
            {
                if (a_event && (a_event->menuName == RE::MainMenu::MENU_NAME ||
                                   a_event->menuName == RE::MistMenu::MENU_NAME ||
                                   a_event->menuName == RE::LoadingMenu::MENU_NAME ||
                                   a_event->menuName == RE::MessageBoxMenu::MENU_NAME))
                    Request();
                return RE::BSEventNotifyControl::kContinue;
            }

            bool armed_ = false;
            bool pending_ = false;
            bool queued_ = false;
        };

        struct SelectPatch final : Xbyak::CodeGenerator
        {
            explicit SelectPatch(std::uintptr_t a_resume)
            {
                mov(ptr[rsp + kIteratorScratchOffset], rbx);
                push(rax);
                push(rcx);
                push(rdx);
                push(r8);
                push(r9);
                push(r10);
                push(r11);
                sub(rsp, 0x88);  // shadow space + volatile XMM registers + alignment
                for (int i = 0; i < 6; ++i)
                    movdqu(ptr[rsp + 0x20 + i * 16], Xbyak::Xmm(i));
                mov(rcx, r12);
                mov(rdx, rbx);
                mov(rax, reinterpret_cast<std::uintptr_t>(&SelectSlot));
                call(rax);
                mov(rbx, rax);
                for (int i = 0; i < 6; ++i)
                    movdqu(Xbyak::Xmm(i), ptr[rsp + 0x20 + i * 16]);
                add(rsp, 0x88);
                pop(r11);
                pop(r10);
                pop(r9);
                pop(r8);
                pop(rdx);
                pop(rcx);
                pop(rax);
                mov(rdx, ptr[rbx]);
                test(rdx, rdx);
                jmp(ptr[rip]);
                dq(a_resume);
            }
        };

        struct NextPatch final : Xbyak::CodeGenerator
        {
            explicit NextPatch(std::uintptr_t a_loop)
            {
                mov(rbx, ptr[rsp + kIteratorScratchOffset]);
                add(rbx, sizeof(Slot));
                jmp(ptr[rip]);
                dq(a_loop);
            }
        };

        template <std::size_t N>
        bool Matches(std::uintptr_t a_address, const std::array<std::uint8_t, N>& a_bytes)
        {
            return std::equal(a_bytes.begin(), a_bytes.end(), reinterpret_cast<const std::uint8_t*>(a_address));
        }
    }

    inline void OnDataLoaded()
    {
        if (REL::Module::IsVR() && Settings::Fixes::bMessageBoxMainMenuTest.GetValue())
            detail::TestPopup::GetSingleton().Arm();
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;
        if (REL::Module::get().version() != SKSE::RUNTIME_VR_1_4_15) {
            logger::warn("skipping MessageBox main-menu fix: unsupported VR runtime"sv);
            return;
        }

        REL::Relocation<std::uintptr_t> draw{ REL::ID(79947) };
        REL::Relocation<std::uintptr_t> display{ REL::ID(32867) };
        REL::Relocation<std::uintptr_t> main{ RE::VTABLE_MainMenu[0] };
        REL::Relocation<std::uintptr_t> mist{ RE::VTABLE_MistMenu[0] };
        REL::Relocation<std::uintptr_t> message{ RE::VTABLE_MessageBoxMenu[0] };
        REL::Relocation<std::uintptr_t> fader{ RE::VTABLE_FaderMenu[0] };
        // ID51449 is verified but not released yet; this is a target guard, not a hook anchor.
        REL::Relocation<std::uintptr_t> mistDisplay{ REL::Offset(0x8DA710) };
        REL::Relocation<std::uintptr_t> body{ draw.address() + detail::kBodyOffset };
        REL::Relocation<std::uintptr_t> next{ draw.address() + detail::kNextOffset };

        static constexpr std::array<std::uint8_t, 20> kFrame{
            0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
            0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x88, 0x00, 0x00, 0x00
        };
        static constexpr std::array<std::uint8_t, 6> kBody{ 0x48, 0x8B, 0x13, 0x48, 0x85, 0xD2 };
        static constexpr std::array<std::uint8_t, 9> kNext{ 0x48, 0x83, 0xC3, 0x08, 0xE9, 0xC6, 0xFE, 0xFF, 0xFF };
        static constexpr std::array<std::uint8_t, 3> kUIRegister{ 0x4C, 0x8B, 0xE1 };
        static constexpr std::array<std::uint8_t, 3> kDisplayCall{ 0xFF, 0x50, 0x30 };
        if (!detail::Matches(draw.address(), kFrame) || !detail::Matches(draw.address() + 0x28, kUIRegister) ||
            !detail::Matches(body.address(), kBody) || !detail::Matches(next.address(), kNext) ||
            !detail::Matches(draw.address() + 0x342, kDisplayCall) ||
            reinterpret_cast<const std::uintptr_t*>(main.address())[6] != display.address() ||
            reinterpret_cast<const std::uintptr_t*>(message.address())[6] != display.address() ||
            reinterpret_cast<const std::uintptr_t*>(mist.address())[6] != mistDisplay.address()) {
            logger::warn("skipping MessageBox main-menu fix: draw-loop signature or display target mismatch"sv);
            return;
        }

        detail::mainVtable = main.address();
        detail::mistVtable = mist.address();
        detail::messageVtable = message.address();
        detail::faderVtable = fader.address();
        detail::baseDisplay = display.address();
        detail::SelectPatch select(body.address() + kBody.size());
        detail::NextPatch   advance(draw.address() + detail::kLoopOffset);
        select.ready();
        advance.ready();
        auto&      trampoline = SKSE::GetTrampoline();
        const auto selectCode = trampoline.allocate(select);
        const auto advanceCode = trampoline.allocate(advance);
        body.write_branch<6>(selectCode);
        next.write_branch<6>(advanceCode);
        REL::safe_fill(next.address() + 6, REL::NOP, kNext.size() - 6);

        EF_INSTALLED("MessageBoxMainMenu"sv, "installed MessageBox main-menu projected render-order fix (VR)"sv);
    }
}
