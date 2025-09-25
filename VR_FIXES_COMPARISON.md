# EngineFixes vs EngineFixesVR - Comprehensive Settings-Based Comparison

## Fixes Present in BOTH Repositories:
✅ **Animation Load Signed Crash** - `bAnimationLoadSignedCrash` / `fixAnimationLoadSignedCrash`
✅ **BSLighting Ambient Specular** - `bBSLightingAmbientSpecular` / `fixBSLightingAmbientSpecular`
✅ **BSLighting Shader Force Alpha Test** - `bBSLightingShaderForceAlphaTest` / `fixBSLightingShaderForceAlphaTest`
✅ **BSLighting Shader Parallax Bug** - `bBSLightingShaderParallaxBug` / `fixBSLightingShaderGeometryParallaxBug`
✅ **Calendar Skipping** - `bCalendarSkipping` / `fixCalendarSkipping`
✅ **Cell Init** - `bCellInit` / `fixCellInit`
✅ **Conjuration Enchant Absorbs** - `bConjurationEnchantAbsorbs` / `fixConjurationEnchantAbsorbs`
✅ **Double Perk Apply** - `bDoublePerkApply` / `fixDoublePerkApply`
✅ **Equip Shout Event Spam** - `bEquipShoutEventSpam` / `fixEquipShoutEventSpam`
✅ **GHeap Leak Detection Crash** - `bGHeapLeakDetectionCrash` / `fixGHeapLeakDetectionCrash`
✅ **Lip Sync** - `bLipSync` / `fixLipSync`
✅ **Memory Access Errors** - `bMemoryAccessErrors` / `fixMemoryAccessErrors`
✅ **MO5S Typo** - `bMO5STypo` / `fixMO5STypo`
✅ **Perk Fragment Is Running** - `bPerkFragmentIsRunning` / `fixPerkFragmentIsRunning`
✅ **Removed Spell Book** - `bRemovedSpellBook` / `fixRemovedSpellBook`
✅ **Save Screenshots** - `bSaveScreenshots` / `fixSaveScreenshots`
✅ **Tree Reflections** - `bTreeReflections` / `fixTreeReflections`
✅ **Vertical Look Sensitivity** - `bVerticalLookSensitivity` / `fixVerticalLookSensitivity`
✅ **Weapon Block Scaling** - `bWeaponBlockScaling` / `fixWeaponBlockScaling`

## Fixes in UPSTREAM but NOT in VR (19 missing):
❌ **Archery Downward Aiming** - `bArcheryDownwardAiming`
❌ **Bethesda Net Crash** - `bBethesdaNetCrash`
❌ **BGSKeywordForm Load Crash** - `bBGSKeywordFormLoadCrash`
❌ **BSLightingShaderProperty ShadowMap** - `bBSLightingShaderPropertyShadowMap`
❌ **BSTempEffect NiRTTI** - `bBSTempEffectNiRTTI`
❌ **Climate Load** - `bClimateLoad`
❌ **Create Armor Node Nullptr Crash** - `bCreateArmorNodeNullPtrCrash`
❌ **ESL Cell Loading Bug** - `bESLCELLLoadBug`
❌ **Get Keyword Item Count** - `bGetKeywordItemCount`
❌ **Global Time** - `bGlobalTime`
❌ **Initialize Hit Data Nullptr Crash** - `bInitializeHitDataNullPtrCrash`
❌ **Music Overlap** - `bMusicOverlap`
❌ **NiController No Target** - `bNiControllerNoTarget`
❌ **Null Process Crash** - `bNullProcessCrash`
❌ **Precomputed Paths** - `bPrecomputedPaths`
❌ **Saved Havok Data Load Init** - `bSavedHavokDataLoadInit`
❌ **Shadow Scene Node Nullptr Crash** - `bShadowSceneNodeNullPtrCrash` (EXISTS in upstream!)
❌ **Texture Load Crash** - `bTextureLoadCrash`
❌ **Torch Landscape** - `bTorchLandscape`

## Fixes in VR but NOT in UPSTREAM (4 truly VR-specific):
❌ **Ability Condition Bug** - `fixAbilityConditionBug` (Added to VR May 2020, from Bug Fixes SSE mod)
❌ **Buy Sell Stack Speech Gain** - `fixBuySellStackSpeechGain` (Added to VR May 2020, from Bug Fixes SSE mod)
❌ **Slow Time Camera Movement** - `fixSlowTimeCameraMovement` (VR-specific, no upstream equivalent)
❌ **Unequip All Crash** - `fixUnequipAllCrash` (VR-specific, no upstream equivalent)

## Fixes in BOTH (but different naming):
✅ **FaceGen MorphDataHead Nullptr Crash** - VR:`fixFaceGenMorphDataHeadNullptrCrash` / Upstream:`bFaceGenMorphDataHeadNullPtrCrash` (Both have this!)
✅ **Shadow Scene Node Nullptr Crash** - VR:`fixShadowSceneNodeNullptrCrash` / Upstream:`bShadowSceneNodeNullPtrCrash` (Both have this!)

## VR has "Shadow Scene Crash" (different from Shadow Scene Node):
❓ **Shadow Scene Crash** - `fixShadowSceneCrash` (VR has this separate from ShadowSceneNode - may be VR-specific variant)

## PATCHES - Present in Both:
✅ **Enable Achievements With Mods** - `bEnableAchievementsWithMods`
✅ **Form Caching** - `bFormCaching`
✅ **Max StdIO** - `bMaxStdIO`
✅ **Save Added Sound Categories** - `bSaveAddedSoundCategories`
✅ **Tree LOD Reference Caching** - `bTreeLodReferenceCaching`

## PATCHES - In UPSTREAM but NOT in VR:
❌ **Disable Chargen Precache** - `bDisableChargenPrecache`
❌ **Disable Snow Flag** - `bDisableSnowFlag`
❌ **INI Setting Collection** - `bINISettingCollection`
❌ **Regular Quicksaves** - `bRegularQuicksaves`
❌ **Safe Exit** - `bSafeExit`
❌ **Scrolling Doesn't Switch POV** - `bScrollingDoesntSwitchPOV`
❌ **Waterflow Animation** - `bWaterflowAnimation`

## Summary:
- **Upstream has 32 fixes total** (17 missing in VR after corrections)
- **VR has 25 fixes total** (4 truly VR-specific, 21 shared)
- **Upstream has 10 patches** (5 missing in VR)
- **VR has 5 patches** (all shared with upstream)
- **Coverage**: ~66% of upstream fixes are in VR (better than initially thought!)
- **VR adds ~16% unique fixes** (4 out of 25)

## Key Findings:
1. **FaceGen and ShadowSceneNode** crashes exist in BOTH repositories (added to upstream April 2021)
2. **Ability Condition Bug** and **Buy Sell Stack Speech Gain** were added to VR in May 2020 from the "Bug Fixes SSE" mod - these could potentially be backported to upstream
3. **Only 4 fixes are truly VR-specific** (not 6-7 as initially thought)
4. **VR's unique "Shadow Scene Crash"** appears to be separate from the ShadowSceneNode fix that both have