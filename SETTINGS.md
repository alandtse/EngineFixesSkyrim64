# EngineFixes Settings Reference

Generated from `src/settings_schema.h` and git history by
`scripts/settings_release_notes.py` — do not edit by hand.

The **Since** column is the release that introduced the setting; to binary-search
a regression, disable the settings introduced at or after the first broken release.

## [General]

| Setting | Default | Since | Description |
| --- | --- | --- | --- |
| `bVerboseLogging` | `false` | ≤ 7.0.20 | enable extra log levels |
| `bCleanSKSECoSaves` | `true` | ≤ 7.0.20 | delete SKSE cosaves with no matching saves |

## [Fixes]

| Setting | Default | Since | Description |
| --- | --- | --- | --- |
| `bArcheryDownwardAiming` | `true` | ≤ 7.0.20 | fixes a bug where arrows don't fire properly if you're aiming downward while crouching on a ridge |
| `bAnimationLoadSignedCrash` | `true` | ≤ 7.0.20 | fixes a misplaced used of a signed value in animation loading |
| `bBethesdaNetCrash` | `true` | ≤ 7.0.20 | fixes the game crashing on startup if you live somewhere with special characters in the name |
| `bBGSKeywordFormLoadCrash` | `true` | ≤ 7.0.20 | fixes a crash when malformed BGSKeywordForms are loaded by the game |
| `bBSLightingAmbientSpecular` | `true` | ≤ 7.0.20 | fixes bug where light template Directional Ambient Specular & Fresnel Power are sent to BSLightingShader incorrectly |
| `bBSLightingShaderForceAlphaTest` | `true` | ≤ 7.0.20 | fixes object LOD reflections by forcing alpha test flag on when NiAlphaProperty/AlphaTest is true |
| `bBSLightingShaderParallaxBug` | `true` | ≤ 7.0.20 | fixes a bug causing the parallax technique to break if specular is not also set |
| `bBSLightingShaderPropertyShadowMap` | `true` | ≤ 7.0.20 | fixes re-use of render passes when a light has multiple shadow map passes |
| `bBSTaskPoolNullVtableCrash` | `true` | 7.2.0 | fixes a crash in BSTaskPool when an actor is freed while a pathfinding task is still pending |
| `bBSTempEffectNiRTTI` | `true` | ≤ 7.0.20 | fixes a bug where the NiRTTI for this object is not set properly |
| `bCalendarSkipping` | `true` | ≤ 7.0.20 | fixes a bug where the game calendar effectively skips a year if you fast travel too far between 20:00 and 23:99 in-game |
| `bCellInit` | `true` | ≤ 7.0.20 | fixes a rare crash where a form field does not get converted from an id to a pointer |
| `bClimateLoad` | `true` | ≤ 7.0.20 | fixes a bug where the game fails to properly apply sunrise and sunset data from Climate records if you load a saved game in an interior |
| `bConjurationEnchantAbsorbs` | `true` | ≤ 7.0.20 | fixes a bug where spell absorption triggers on enchanted items using conjuration summons |
| `bConsoleSaveDeadlock` | `true` | 7.4.7 | fixes a deadlock (permanent hang, no crash) when the console 'save' command executes off the main thread, e.g. issued programmatically via Console::ExecuteCommand |
| `bCreateArmorNodeNullPtrCrash` | `true` | ≤ 7.0.20 | fixes typo that may cause a crash somewhere in CreateArmorNode |
| `bDoublePerkApply` | `true` | ≤ 7.0.20 | fixes NPC perks applying twice when you load a game |
| `bEquipShoutEventSpam` | `true` | ≤ 7.0.20 | fixes a bug where the "equip shout" procedure will send a "shout equipped" event even if the shout fails to equip |
| `bESLCELLLoadBug` | `true` | ≤ 7.0.20 | fixes issues with interior cells created in ESL files |
| `bFaceGenMorphDataHeadNullPtrCrash` | `true` | ≤ 7.0.20 | fixes a crash in face morphing, possibly related to decapitations |
| `bGetGameSettingNotFoundCrash` | `true` | 7.4.0 | fixes a crash when the "GetGameSetting" (getgs) console command is given a setting name that doesn't exist |
| `bGetKeywordItemCount` | `false` | ≤ 7.0.20 | fixes the condition function "GetKeywordItemCount", which returns broken results sometimes |
| `bGHeapLeakDetectionCrash` | `true` | ≤ 7.0.20 | fixes a crash where scaleform attempts to report a memory leak but the code doesn't exist in Skyrim's build |
| `bGlobalTime` | `true` | ≤ 7.0.20 | fixes game systems that are affected by game time instead of real time, including old slow time camera movement fix |
| `bInitializeHitDataNullPtrCrash` | `true` | ≤ 7.0.20 | fixes a crash on melee hit that unequipped the weapon at the same time |
| `bIsPlayerInRegionParentCellCheck` | `true` | 7.1.0 | (VR-only) fixes a crash in IsPlayerInRegion caused by missing parent-cell null checks that SE added but VR lacks |
| `bLipSync` | `true` | ≤ 7.0.20 | fixes a bug causing lip sync to desync |
| `bMemoryAccessErrors` | `true` | ≤ 7.0.20 | fixes miscellaneous errors that are obscured by Skyrim's default allocator |
| `bMO5STypo` | `true` | ≤ 7.0.20 | fixes a typo preventing the game from loading MO5S entries in ARMA forms |
| `bMusicOverlap` | `true` | ≤ 7.0.20 | fixes a bug where multiple music tracks are playing at the same time |
| `bNiControllerNoTarget` | `true` | ≤ 7.0.20 | fixes a crash if a malformed nif with a time controller that has no target is loaded, and logs a warning for the malformed nif |
| `bNullProcessCrash` | `true` | ≤ 7.0.20 | fixes a couple cases where the game can crash when checking the equipped weapons of an actor without an AI process |
| `bPerkFragmentIsRunning` | `true` | ≤ 7.0.20 | fixes a crash if the IsRunning function of a perk fragment is called on a non-actor form |
| `bPrecomputedPaths` | `true` | ≤ 7.0.20 | fixes a crash when NAVI precomputed paths aren't accurate for your load order and logs a warning |
| `bRemovedSpellBook` | `true` | ≤ 7.0.20 | fixes a crash where learning a spell from a book that is later removed in another plugin causes a crash in the inventory |
| `bSaveScreenshots` | `true` | ≤ 7.0.20 | fixes save screenshots being blank under certain configurations |
| `bSavedHavokDataLoadInit` | `true` | ≤ 7.0.20 | fixes motion vectors for objects with saved havok data that differs significantly from their base state |
| `bShadowSceneNodeNullPtrCrash` | `true` | ≤ 7.0.20 | fixes a crash in shadowscenenode |
| `bSkyUpdateCloudsNullPtrCrash` | `true` | 7.4.0 | fixes a crash in Sky::UpdateClouds when the current cloud object is null (e.g. during weather transitions; surfaced by sky/weather shader mods) |
| `bStuckMouseButtons` | `true` | 7.1.0 | fixes stuck mouse buttons when a new menu opened and the old menu didn't receive MouseUp |
| `bTextureLoadCrash` | `true` | ≤ 7.0.20 | fixes a crash in 1.5.97 when a texture load fails (D6DDDA), this behavior is built-in to 1.6.1170; also logs texture load errors |
| `bTriShapeReleaseBufferGuard` | `true` | 7.3.0 | guards BSGraphics::TriShape::Release against an access violation when the geometry's vertex/index buffer was already freed (seen on some PBR/parallax/SMP meshes) |
| `bTorchLandscape` | `true` | ≤ 7.0.20 | fixes a bug where torches sometimes don't light the landscape |
| `bTreeReflections` | `true` | ≤ 7.0.20 | fixes tree LOD reflection alpha |
| `bVerticalLookSensitivity` | `true` | ≤ 7.0.20 | fixes vertical look sensitivity being tied to framerate |
| `bWeaponBlockScaling` | `true` | ≤ 7.0.20 | fixes weapon blocking so it correctly scales off of the blocking actor's weapon |
| `bAbilityConditionBug` | `true` | 7.1.0 | (VR-only) fixes ability conditions being re-evaluated every frame; throttles evaluation per ActiveEffect to the engine's fActiveEffectConditionUpdateInterval |
| `bBuySellStackSpeechGain` | `true` | 7.1.0 | (VR-only) fixes buying/selling a stack of items only granting speech XP for a single item |
| `bCopyBoneTransformNullCrash` | `true` | 7.4.0 | (VR-only) fixes a null-pointer crash when an actor's animation references a bone name absent from its skeleton (modded/mismatched skeleton) |
| `bShadowSceneCrash` | `true` | 7.1.0 | (VR-only) fixes a null-pointer crash in shadow scene light processing distinct from the SE/AE shadowscenenode fix |
| `bLockpickingMenuInitCrash` | `true` | 7.4.4 | (VR-only) guards the LockpickingMenu against a null-pointer crash when its lock/pick 3D models aren't loaded yet on the first frame |
| `bBSOpenVRHandIndexNullCrash` | `true` | 7.4.7 | (VR-only) guards BSOpenVR::GetTrackedDeviceIndexForHand against a null-pointer crash when the VR hand-device API is queried while the HMD is asleep / OpenVR is not initialized (no SteamVR null driver loaded) |

## [Patches]

| Setting | Default | Since | Description |
| --- | --- | --- | --- |
| `bDisableChargenPrecache` | `false` | ≤ 7.0.20 | disables pre-caching of chargen, unnecessary with RaceMenu installed |
| `bDisableSnowFlag` | `false` | ≤ 7.0.20 | forcably removes snow flags from loaded LTEX, MATO, and STAT forms |
| `bEnableAchievementsWithMods` | `true` | ≤ 7.0.20 | enables achievements with mods active |
| `bFormCaching` | `true` | ≤ 7.0.20 | attempts to speedup form lookups |
| `bINISettingCollection` | `true` | ≤ 7.0.20 | slightly speeds up startup time for lists with a large number of plugins |
| `bMaxStdIO` | `true` | ≤ 7.0.20 | sets the maximum number of open file handles to the maximum available on your system (8192 in most cases, 2048 for older versions of windows) |
| `bRegularQuicksaves` | `false` | ≤ 7.0.20 | makes quicksaves into regular saves |
| `bSafeExit` | `true` | ≤ 7.0.20 | prevent the game from hanging when shutting down |
| `bSaveAddedSoundCategories` | `true` | ≤ 7.0.20 | save the volume of sound categories added by mods |
| `iSaveGameMaxSize` | `128` | ≤ 7.0.20 | expands the maximum uncompressed size of a save game from 64 MB to a configurable size (MB), game default = 64 MB, only go as high as you need! |
| `bScrollingDoesntSwitchPOV` | `false` | ≤ 7.0.20 | disables swapping between 1st/3rd person when using mousewheel scroll to zoom |
| `fSleepWaitTimeModifier` | `1.0f` | ≤ 7.0.20 | modifies your sleep/wait time, 1.0 = default, smaller = faster, larger = slower |
| `bTreeLodReferenceCaching` | `true` | ≤ 7.0.20 | requires form caching to be enabled. speeds up a tree LOD function that slows down proportionate to the number of plugins loaded |
| `bWaterflowAnimation` | `true` | ≤ 7.0.20 | decouple waterflow speed from in-game timescale |
| `fWaterflowSpeed` | `20.0f` | ≤ 7.0.20 | 20.0 = default, smaller = slower, larger = faster |

## [MemoryManager]

| Setting | Default | Since | Description |
| --- | --- | --- | --- |
| `bOverrideMemoryManager` | `true` | ≤ 7.0.20 | overrides Skyrim's memory manager and scrap heap with direct malloc/free calls (both always enabled together) |
| `bOverrideScaleformAllocator` | `true` | ≤ 7.0.20 | overrides Skyrim's scaleform allocator with calls to the global memory manager |
| `bOverrideRenderPassCache` | `true` | ≤ 7.0.20 | overrides Skyrim's render pass cache with direct malloc/free calls |
| `bOverrideHavokMemorySystem` | `true` | ≤ 7.0.20 | overrides Havok's memory manager with direct malloc/free calls |
| `bReplaceImports` | `true` | ≤ 7.0.20 | replace imported CRT memory functions with selected allocator |

## [Warnings]

| Setting | Default | Since | Description |
| --- | --- | --- | --- |
| `bTextureLoadFailed` | `true` | ≤ 7.0.20 | On exit, pops up a message box telling you one or more textures failed to load and have been logged |
| `bPrecomputedPathHasErrors` | `false` | ≤ 7.0.20 | On exit, pops up a message box telling you a precomputed path had an error |
| `bRefHandleLimit` | `true` | ≤ 7.0.20 | Warns when you are close to the reference handle limit at main menu and after loading a save |
| `uRefrMainMenuLimit` | `800000` | ≤ 7.0.20 | Handle count to warn at on main menu |
| `uRefrLoadedGameLimit` | `1000000` | ≤ 7.0.20 | Handle count to warn at after loading a save game |
| `bDupeAddonNodes` | `true` | 7.1.0 | Warns when duplicate ADDN (Addon Node) form indices are found in your load order; pops up a message box and logs to the Engine Fixes log |

## [Debug]

| Setting | Default | Since | Description |
| --- | --- | --- | --- |
| `bPrintDetailedPrecomputedPathInfo` | `false` | ≤ 7.0.20 | disables the precomputed path crash fix and prints detailed information about broken paths |
| `bDisableTBB` | `false` | ≤ 7.0.20 | use CRT allocator instead of tbb - this can and will cause crashes with broken plugins |

## Settings by release

- **7.4.7**: `bConsoleSaveDeadlock`, `bBSOpenVRHandIndexNullCrash`
- **7.4.4**: `bLockpickingMenuInitCrash`
- **7.4.0**: `bGetGameSettingNotFoundCrash`, `bSkyUpdateCloudsNullPtrCrash`, `bCopyBoneTransformNullCrash`
- **7.3.0**: `bTriShapeReleaseBufferGuard`
- **7.2.0**: `bBSTaskPoolNullVtableCrash`
- **7.1.0**: `bIsPlayerInRegionParentCellCheck`, `bStuckMouseButtons`, `bAbilityConditionBug`, `bBuySellStackSpeechGain`, `bShadowSceneCrash`, `bDupeAddonNodes`
- **≤ 7.0.20**: `bVerboseLogging`, `bCleanSKSECoSaves`, `bArcheryDownwardAiming`, `bAnimationLoadSignedCrash`, `bBethesdaNetCrash`, `bBGSKeywordFormLoadCrash`, `bBSLightingAmbientSpecular`, `bBSLightingShaderForceAlphaTest`, `bBSLightingShaderParallaxBug`, `bBSLightingShaderPropertyShadowMap`, `bBSTempEffectNiRTTI`, `bCalendarSkipping`, `bCellInit`, `bClimateLoad`, `bConjurationEnchantAbsorbs`, `bCreateArmorNodeNullPtrCrash`, `bDoublePerkApply`, `bEquipShoutEventSpam`, `bESLCELLLoadBug`, `bFaceGenMorphDataHeadNullPtrCrash`, `bGetKeywordItemCount`, `bGHeapLeakDetectionCrash`, `bGlobalTime`, `bInitializeHitDataNullPtrCrash`, `bLipSync`, `bMemoryAccessErrors`, `bMO5STypo`, `bMusicOverlap`, `bNiControllerNoTarget`, `bNullProcessCrash`, `bPerkFragmentIsRunning`, `bPrecomputedPaths`, `bRemovedSpellBook`, `bSaveScreenshots`, `bSavedHavokDataLoadInit`, `bShadowSceneNodeNullPtrCrash`, `bTextureLoadCrash`, `bTorchLandscape`, `bTreeReflections`, `bVerticalLookSensitivity`, `bWeaponBlockScaling`, `bDisableChargenPrecache`, `bDisableSnowFlag`, `bEnableAchievementsWithMods`, `bFormCaching`, `bINISettingCollection`, `bMaxStdIO`, `bRegularQuicksaves`, `bSafeExit`, `bSaveAddedSoundCategories`, `iSaveGameMaxSize`, `bScrollingDoesntSwitchPOV`, `fSleepWaitTimeModifier`, `bTreeLodReferenceCaching`, `bWaterflowAnimation`, `fWaterflowSpeed`, `bOverrideMemoryManager`, `bOverrideScaleformAllocator`, `bOverrideRenderPassCache`, `bOverrideHavokMemorySystem`, `bReplaceImports`, `bTextureLoadFailed`, `bPrecomputedPathHasErrors`, `bRefHandleLimit`, `uRefrMainMenuLimit`, `uRefrLoadedGameLimit`, `bPrintDetailedPrecomputedPathInfo`, `bDisableTBB`
