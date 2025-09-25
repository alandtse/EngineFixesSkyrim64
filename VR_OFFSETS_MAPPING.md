# VR Offset Mappings for EngineFixes

This file documents VR-specific offsets found in EngineFixesVR that need to be integrated.

## Memory Access Errors
- Snow Material VTBL: SE=0x12d38f3, VR=0x18fea78
- Snow Material FuncBase: SE=0x12f2020, VR=0x1338400
- Shader Particle Geometry VTBL: VR=0x15ebf50
- Shadow DirectionalLight: SE=0x13251c0, VR=0x13574f0

## Lip Sync
- FuncBase: SE=0x1f12a0, VR=0x201d80

## GHeap Leak Detection
- FuncBase: SE=0xfffeb0, VR=0x105cbb0

## Cell Init
- FuncBase: SE=0x262290, VR=0x273830

## Animation Load Signed Crash
- Target: SE=0xb66930, VR=0xba17a0

## BSLighting Ambient Specular
- AddAmbientSpecularToSetupGeometry: SE=0x12f2bb0, VR=0x1338f60
- AmbientSpecularAndFresnel: SE=0x1e0dfcc, VR=0x342317c
- DisableSetupMaterialAmbientSpecular: SE=0x12f2020, VR=0x1338400

## BSLighting Shader Force Alpha Test
- FuncBase: SE=0x5a6230, VR=0x5ad8f0

## BSLighting Shader Geometry Parallax
- VTBL: SE=0x15217e0, VR=0x1598b30

## Calendar Skipping
- FuncBase: SE=0x6323c0, VR=0x63b290

## Conjuration Enchant Absorbs
- FuncBase: SE=0x2db610, VR=0x2ecb20

## Double Perk Apply
- QueueApplyPerk: SE=0x5c67a0, VR=0x5ced40
- Handle_Add_Rf: SE=0x682020, VR=0x68b490
- Switch_Function_movzx: SE=0x5c6ee0, VR=0x5cf480
- Unknown_Add_Function_movzx: SE=0x5c67a0, VR=0x5ced40
- Next_Formid_Get_Hook: SE=0x681fd0, VR=0x68b440
- Do_Handle_Hook: SE=0x3386a0, VR=0x347f60
- Do_Add_Hook: SE=0x338a50, VR=0x348310

## Equip Shout Event Spam
- VTBL: SE=0x15592b8, VR=0x15c9e68

## Perk Fragment Is Running
- (Various offsets need to be checked against current implementation)

## Removed Spellbook
- (Various offsets need to be checked against current implementation)