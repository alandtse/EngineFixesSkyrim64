#pragma once

// VR-only crash: LockpickingMenu::Init3DElements (VR 0x1408C4770) called from
// LockpickingMenu::ProcessMessage before model assets finish loading.
//
// Root cause: ProcessMessage calls Init3DElements whenever init3DElements==false.
// Init3DElements issues async RequestModel2 calls for pickModel and lockpickShiv,
// then immediately dereferences their _entry pointers.  In VR the async loads
// can still be in flight at this point, leaving _entry null → null deref crash.
//
//   VR crash site: LockpickingMenu::Init3DElements+0x231 = 0x1408C49A1
//   Instruction:   mov rcx, [rax+0x28]  (rax = lockpickShiv._entry = 0x0)
//
// Fix: guard the call site in ProcessMessage (VR RVA 0x8C3EF6) with a check
// that both model handles have a non-null _entry with a valid data._ptr.
// When the models are not yet ready we return without calling Init3DElements,
// leaving init3DElements==false so ProcessMessage retries on the next frame.
//
// Safety of early return: the three calls that follow Init3DElements in
// ProcessMessage (UpdatePickUI / ActivateLockSequence / BeginLockpicking)
// all null-check their lockController/pickController parameters and return
// immediately when those are null, so skipping Init3DElements is cascade-safe.

namespace Fixes::LockpickingMenuInitCrash
{
    inline REL::Relocation<void(RE::LockpickingMenu*)> origInit3DElements;

    inline void guardedInit3DElements(RE::LockpickingMenu* a_this)
    {
        auto& rtd = a_this->GetRuntimeData();
        if (!rtd.pickModel || !rtd.lockpickShiv)
            return;
        if (!rtd.pickModel->data || !rtd.lockpickShiv->data)
            return;
        origInit3DElements(a_this);
    }

    inline void Install()
    {
        if (!REL::Module::IsVR())
            return;

        // Patch the CALL to Init3DElements inside LockpickingMenu::ProcessMessage.
        // VR: ProcessMessage (0x1408C3E10) + 0xEE6 = 0x1408C3EF6 → VR RVA 0x8C3EF6
        REL::Relocation<std::uintptr_t> target{ REL::Offset{ 0x8C3EF6 } };
        origInit3DElements = target.write_call<5>(guardedInit3DElements);

        logger::info("installed lockpicking menu init crash fix (VR)"sv);
    }
}
