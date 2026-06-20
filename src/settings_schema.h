#pragma once

// Single source of truth for every EngineFixes setting.
//
// Each entry is  X(type, key, default, comment)  where:
//   type    - REX::TOML scalar type: Bool / I32 / U32 / F32
//   key     - the TOML key (also the C++ variable name; section is fixed per list)
//   default - the default value (a C++ literal of the matching type)
//   comment - the description emitted into EngineFixes.toml
//
// These lists drive BOTH the runtime bindings (settings.h) AND the self-healing
// EngineFixes.toml generator (settings.cpp). Because both derive from here, the
// declared settings and the generated TOML can never drift apart -- the bug that
// previously left new keys (e.g. bLockpickingMenuInitCrash) out of the shipped
// TOML. To add a setting, add ONE line to the appropriate list below.

#define EF_SETTINGS_GENERAL(X)                                 \
    X(Bool, bVerboseLogging, false, "enable extra log levels") \
    X(Bool, bCleanSKSECoSaves, true, "delete SKSE cosaves with no matching saves")

#define EF_SETTINGS_FIXES(X)                                                                                                                                                                                        \
    X(Bool, bArcheryDownwardAiming, true, "fixes a bug where arrows don't fire properly if you're aiming downward while crouching on a ridge")                                                                      \
    X(Bool, bAnimationLoadSignedCrash, true, "fixes a misplaced used of a signed value in animation loading")                                                                                                       \
    X(Bool, bBethesdaNetCrash, true, "fixes the game crashing on startup if you live somewhere with special characters in the name")                                                                                \
    X(Bool, bBGSKeywordFormLoadCrash, true, "fixes a crash when malformed BGSKeywordForms are loaded by the game")                                                                                                  \
    X(Bool, bBSLightingAmbientSpecular, true, "fixes bug where light template Directional Ambient Specular & Fresnel Power are sent to BSLightingShader incorrectly")                                               \
    X(Bool, bBSLightingShaderForceAlphaTest, true, "fixes object LOD reflections by forcing alpha test flag on when NiAlphaProperty/AlphaTest is true")                                                             \
    X(Bool, bBSLightingShaderParallaxBug, true, "fixes a bug causing the parallax technique to break if specular is not also set")                                                                                  \
    X(Bool, bBSLightingShaderPropertyShadowMap, true, "fixes re-use of render passes when a light has multiple shadow map passes")                                                                                  \
    X(Bool, bBSTaskPoolNullVtableCrash, true, "fixes a crash in BSTaskPool when an actor is freed while a pathfinding task is still pending")                                                                       \
    X(Bool, bBSTempEffectNiRTTI, true, "fixes a bug where the NiRTTI for this object is not set properly")                                                                                                          \
    X(Bool, bCalendarSkipping, true, "fixes a bug where the game calendar effectively skips a year if you fast travel too far between 20:00 and 23:99 in-game")                                                     \
    X(Bool, bCellInit, true, "fixes a rare crash where a form field does not get converted from an id to a pointer")                                                                                                \
    X(Bool, bClimateLoad, true, "fixes a bug where the game fails to properly apply sunrise and sunset data from Climate records if you load a saved game in an interior")                                          \
    X(Bool, bConjurationEnchantAbsorbs, true, "fixes a bug where spell absorption triggers on enchanted items using conjuration summons")                                                                           \
    X(Bool, bCreateArmorNodeNullPtrCrash, true, "fixes typo that may cause a crash somewhere in CreateArmorNode")                                                                                                   \
    X(Bool, bDoublePerkApply, true, "fixes NPC perks applying twice when you load a game")                                                                                                                          \
    X(Bool, bEquipShoutEventSpam, true, "fixes a bug where the \"equip shout\" procedure will send a \"shout equipped\" event even if the shout fails to equip")                                                    \
    X(Bool, bESLCELLLoadBug, true, "fixes issues with interior cells created in ESL files")                                                                                                                         \
    X(Bool, bFaceGenMorphDataHeadNullPtrCrash, true, "fixes a crash in face morphing, possibly related to decapitations")                                                                                           \
    X(Bool, bGetGameSettingNotFoundCrash, true, "fixes a crash when the \"GetGameSetting\" (getgs) console command is given a setting name that doesn't exist")                                                     \
    X(Bool, bGetKeywordItemCount, false, "fixes the condition function \"GetKeywordItemCount\", which returns broken results sometimes")                                                                            \
    X(Bool, bGHeapLeakDetectionCrash, true, "fixes a crash where scaleform attempts to report a memory leak but the code doesn't exist in Skyrim's build")                                                          \
    X(Bool, bGlobalTime, true, "fixes game systems that are affected by game time instead of real time, including old slow time camera movement fix")                                                               \
    X(Bool, bInitializeHitDataNullPtrCrash, true, "fixes a crash on melee hit that unequipped the weapon at the same time")                                                                                         \
    X(Bool, bIsPlayerInRegionParentCellCheck, true, "(VR-only) fixes a crash in IsPlayerInRegion caused by missing parent-cell null checks that SE added but VR lacks")                                             \
    X(Bool, bLipSync, true, "fixes a bug causing lip sync to desync")                                                                                                                                               \
    X(Bool, bMemoryAccessErrors, true, "fixes miscellaneous errors that are obscured by Skyrim's default allocator")                                                                                                \
    X(Bool, bMO5STypo, true, "fixes a typo preventing the game from loading MO5S entries in ARMA forms")                                                                                                            \
    X(Bool, bMusicOverlap, true, "fixes a bug where multiple music tracks are playing at the same time")                                                                                                            \
    X(Bool, bNiControllerNoTarget, true, "fixes a crash if a malformed nif with a time controller that has no target is loaded, and logs a warning for the malformed nif")                                          \
    X(Bool, bNullProcessCrash, true, "fixes a couple cases where the game can crash when checking the equipped weapons of an actor without an AI process")                                                          \
    X(Bool, bPerkFragmentIsRunning, true, "fixes a crash if the IsRunning function of a perk fragment is called on a non-actor form")                                                                               \
    X(Bool, bPrecomputedPaths, true, "fixes a crash when NAVI precomputed paths aren't accurate for your load order and logs a warning")                                                                            \
    X(Bool, bRemovedSpellBook, true, "fixes a crash where learning a spell from a book that is later removed in another plugin causes a crash in the inventory")                                                    \
    X(Bool, bSaveScreenshots, true, "fixes save screenshots being blank under certain configurations")                                                                                                              \
    X(Bool, bSavedHavokDataLoadInit, true, "fixes motion vectors for objects with saved havok data that differs significantly from their base state")                                                               \
    X(Bool, bShadowSceneNodeNullPtrCrash, true, "fixes a crash in shadowscenenode")                                                                                                                                 \
    X(Bool, bSkyUpdateCloudsNullPtrCrash, true, "fixes a crash in Sky::UpdateClouds when the current cloud object is null (e.g. during weather transitions; surfaced by sky/weather shader mods)")                  \
    X(Bool, bStuckMouseButtons, true, "fixes stuck mouse buttons when a new menu opened and the old menu didn't receive MouseUp")                                                                                   \
    X(Bool, bTextureLoadCrash, true, "fixes a crash in 1.5.97 when a texture load fails (D6DDDA), this behavior is built-in to 1.6.1170; also logs texture load errors")                                            \
    X(Bool, bTriShapeReleaseBufferGuard, true, "guards BSGraphics::TriShape::Release against an access violation when the geometry's vertex/index buffer was already freed (seen on some PBR/parallax/SMP meshes)") \
    X(Bool, bTorchLandscape, true, "fixes a bug where torches sometimes don't light the landscape")                                                                                                                 \
    X(Bool, bTreeReflections, true, "fixes tree LOD reflection alpha")                                                                                                                                              \
    X(Bool, bVerticalLookSensitivity, true, "fixes vertical look sensitivity being tied to framerate")                                                                                                              \
    X(Bool, bWeaponBlockScaling, true, "fixes weapon blocking so it correctly scales off of the blocking actor's weapon")                                                                                           \
    X(Bool, bAbilityConditionBug, true, "(VR-only) fixes ability conditions being re-evaluated every frame; throttles evaluation per ActiveEffect to the engine's fActiveEffectConditionUpdateInterval")            \
    X(Bool, bBuySellStackSpeechGain, true, "(VR-only) fixes buying/selling a stack of items only granting speech XP for a single item")                                                                             \
    X(Bool, bCopyBoneTransformNullCrash, true, "(VR-only) fixes a null-pointer crash when an actor's animation references a bone name absent from its skeleton (modded/mismatched skeleton)")                       \
    X(Bool, bShadowSceneCrash, true, "(VR-only) fixes a null-pointer crash in shadow scene light processing distinct from the SE/AE shadowscenenode fix")                                                           \
    X(Bool, bLockpickingMenuInitCrash, true, "(VR-only) guards the LockpickingMenu against a null-pointer crash when its lock/pick 3D models aren't loaded yet on the first frame")                                 \
    X(Bool, bBSOpenVRHandIndexNullCrash, true, "(VR-only) guards BSOpenVR::GetTrackedDeviceIndexForHand against a null-pointer crash when the VR hand-device API is queried while the HMD is asleep / OpenVR is not initialized (no SteamVR null driver loaded)")

#define EF_SETTINGS_PATCHES(X)                                                                                                                                                       \
    X(Bool, bDisableChargenPrecache, false, "disables pre-caching of chargen, unnecessary with RaceMenu installed")                                                                  \
    X(Bool, bDisableSnowFlag, false, "forcably removes snow flags from loaded LTEX, MATO, and STAT forms")                                                                           \
    X(Bool, bEnableAchievementsWithMods, true, "enables achievements with mods active")                                                                                              \
    X(Bool, bFormCaching, true, "attempts to speedup form lookups")                                                                                                                  \
    X(Bool, bINISettingCollection, true, "slightly speeds up startup time for lists with a large number of plugins")                                                                 \
    X(Bool, bMaxStdIO, true, "sets the maximum number of open file handles to the maximum available on your system (8192 in most cases, 2048 for older versions of windows)")        \
    X(Bool, bRegularQuicksaves, false, "makes quicksaves into regular saves")                                                                                                        \
    X(Bool, bSafeExit, true, "prevent the game from hanging when shutting down")                                                                                                     \
    X(Bool, bSaveAddedSoundCategories, true, "save the volume of sound categories added by mods")                                                                                    \
    X(I32, iSaveGameMaxSize, 128, "expands the maximum uncompressed size of a save game from 64 MB to a configurable size (MB), game default = 64 MB, only go as high as you need!") \
    X(Bool, bScrollingDoesntSwitchPOV, false, "disables swapping between 1st/3rd person when using mousewheel scroll to zoom")                                                       \
    X(F32, fSleepWaitTimeModifier, 1.0f, "modifies your sleep/wait time, 1.0 = default, smaller = faster, larger = slower")                                                          \
    X(Bool, bTreeLodReferenceCaching, true, "requires form caching to be enabled. speeds up a tree LOD function that slows down proportionate to the number of plugins loaded")      \
    X(Bool, bWaterflowAnimation, true, "decouple waterflow speed from in-game timescale")                                                                                            \
    X(F32, fWaterflowSpeed, 20.0f, "20.0 = default, smaller = slower, larger = faster")

#define EF_SETTINGS_MEMORYMANAGER(X)                                                                                                                       \
    X(Bool, bOverrideMemoryManager, true, "overrides Skyrim's memory manager and scrap heap with direct malloc/free calls (both always enabled together)") \
    X(Bool, bOverrideScaleformAllocator, true, "overrides Skyrim's scaleform allocator with calls to the global memory manager")                           \
    X(Bool, bOverrideRenderPassCache, true, "overrides Skyrim's render pass cache with direct malloc/free calls")                                          \
    X(Bool, bOverrideHavokMemorySystem, true, "overrides Havok's memory manager with direct malloc/free calls")                                            \
    X(Bool, bReplaceImports, true, "replace imported CRT memory functions with selected allocator")

#define EF_SETTINGS_WARNINGS(X)                                                                                                              \
    X(Bool, bTextureLoadFailed, true, "On exit, pops up a message box telling you one or more textures failed to load and have been logged") \
    X(Bool, bPrecomputedPathHasErrors, false, "On exit, pops up a message box telling you a precomputed path had an error")                  \
    X(Bool, bRefHandleLimit, true, "Warns when you are close to the reference handle limit at main menu and after loading a save")           \
    X(U32, uRefrMainMenuLimit, 800000, "Handle count to warn at on main menu")                                                               \
    X(U32, uRefrLoadedGameLimit, 1000000, "Handle count to warn at after loading a save game")                                               \
    X(Bool, bDupeAddonNodes, true, "Warns when duplicate ADDN (Addon Node) form indices are found in your load order; pops up a message box and logs to the Engine Fixes log")

#define EF_SETTINGS_DEBUG(X)                                                                                                                        \
    X(Bool, bPrintDetailedPrecomputedPathInfo, false, "disables the precomputed path crash fix and prints detailed information about broken paths") \
    X(Bool, bDisableTBB, false, "use CRT allocator instead of tbb - this can and will cause crashes with broken plugins")
