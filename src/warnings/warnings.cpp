#include "warnings.h"

namespace Warnings
{
    // Warn when two different BGSAddonNode forms share the same node index.
    // ADDN node indices must be unique across the entire load order.
    // Originally in src/warnings/miscwarnings.cpp, dropped in the "start modernizing" rewrite (2c5c0d7).
    //
    // Map clearing on data reload (Data Files change without restart):
    //   Hooks two callsites that call Main::ReloadFormData (the data reload function).
    //     Call1: RELOCATION_ID(35551, 36550)           + 0x163       (MainLoop)
    //     Call2: RELOCATION_ID(35589, 416418)          + VAR_NUM(0x0D, 0x12)  (Main::FUN_ModsChangedCallback)
    //     DataReload target: RELOCATION_ID(35593, 36601)
    namespace WarnDupeAddonNodes
    {
        namespace detail
        {
            using Load_t    = bool(RE::BGSAddonNode*, RE::TESFile*);
            using Reload_t  = void(void*);

            // Map from node index → the first form that claimed it
            inline std::unordered_map<std::uint32_t, RE::BGSAddonNode*> g_nodeMap;
            inline Load_t*   _original   = nullptr;
            inline Reload_t* _origReload = nullptr;

            inline bool Load_Hook(RE::BGSAddonNode* a_this, RE::TESFile* a_mod)
            {
                const auto result = _original(a_this, a_mod);

                if (result) {
                    const auto [it, inserted] = g_nodeMap.emplace(a_this->index, a_this);
                    if (!inserted) {
                        const auto* existing = it->second;
                        // Only warn when it's genuinely two different forms
                        if (existing != a_this && existing->GetFormID() != a_this->GetFormID()) {
                            const auto* srcFile = existing->GetDescriptionOwnerFile();
                            logger::warn(
                                "duplicate addon node index {} found: formID {:08X} in '{}' and formID {:08X} in '{}'"sv,
                                a_this->index,
                                existing->GetFormID(), srcFile ? srcFile->fileName : "<unknown>",
                                a_this->GetFormID(), a_mod->fileName);
                            logger::warn("for info on resolving this problem, please check the Engine Fixes mod page https://www.nexusmods.com/skyrimspecialedition/mods/17230"sv);
                            logger::warn("you can disable this warning in the ini file"sv);
                            REX::W32::MessageBoxW(
                                nullptr,
                                L"WARNING: You have a duplicate Addon Node index. Please check the Engine Fixes log for more details.",
                                L"Engine Fixes for Skyrim Special Edition",
                                MB_OK);
                        }
                    }
                }

                return result;
            }

            // Called at the two data-reload callsites — clears the map so a fresh load
            // doesn't produce false positives from the previous session's entries.
            inline void Reload_Hook(void* a_this)
            {
                _origReload(a_this);
                logger::trace("data reload detected, clearing addon node map"sv);
                g_nodeMap.clear();
            }
        }

        void Install()
        {
            // Hook virtual slot 6 (BGSAddonNode::Load, overrides TESBoundObject::Load).
            REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_BGSAddonNode[0].address() };
            detail::_original = reinterpret_cast<detail::Load_t*>(
                vtable.write_vfunc(6, detail::Load_Hook));

            // Hook the two callsites that call the data-reload function so we can clear
            // the node map when the game reloads form data (e.g. Data Files without restart).
            {
                REL::Relocation<std::uintptr_t> dataReload{ RELOCATION_ID(35593, 36601) };
                detail::_origReload = reinterpret_cast<detail::Reload_t*>(dataReload.address());

                REL::Relocation<std::uintptr_t> call1{ RELOCATION_ID(35551, 36550), 0x163 };
                REL::Relocation<std::uintptr_t> call2{ RELOCATION_ID(35589, 416418), VAR_NUM(0x0D, 0x12) };
                auto& trampoline = SKSE::GetTrampoline();
                trampoline.write_call<5>(call1.address(), detail::Reload_Hook);
                trampoline.write_call<5>(call2.address(), detail::Reload_Hook);
            }

            logger::info("installed duplicate addon node warning"sv);
        }
    }

    void WarnActiveRefrHandleCount(std::uint32_t warnCount)
    {
        const auto refrArray = RE::BSPointerHandleManager<RE::TESObjectREFR*>::GetHandleEntries();

        std::uint32_t activeHandleCount = 0;

        for (auto& val : refrArray) {
            if (val.IsInUse())
                activeHandleCount++;
        }

        if (activeHandleCount > warnCount) {
            logger::warn(FMT_STRING("your active refr handle count is currently {} which is higher than the warning level of {}"), activeHandleCount, warnCount);
            if (warnCount == Settings::Warnings::uRefrMainMenuLimit.GetValue())
                logger::warn("this is your main menu limit"sv);
            if (warnCount == Settings::Warnings::uRefrLoadedGameLimit.GetValue())
                logger::warn("this is your loaded game limit"sv);
            logger::warn("for info about this warning, please check the Engine Fixes mod page https://www.nexusmods.com/skyrimspecialedition/mods/17230"sv);
            logger::warn("you can disable this warning in the ini file"sv);

            std::wostringstream warningString;
            warningString << L"WARNING: Your active refr handle count is currently "sv << activeHandleCount << L" which is dangerously close to the limit. Please check the Engine Fixes log for more details."sv;
            REX::W32::MessageBoxW(nullptr, warningString.str().c_str(), L"Engine Fixes for Skyrim Special Edition", MB_OK);
        }
    }
}
