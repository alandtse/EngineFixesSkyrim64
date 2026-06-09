## [7.4.5](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.4.4...v7.4.5) (2026-06-09)

### Bug Fixes

* catch second site lockpick nullptr ctd ([#17](https://github.com/alandtse/EngineFixesSkyrim64/issues/17)) ([62d3dc7](https://github.com/alandtse/EngineFixesSkyrim64/commit/62d3dc70b75f76605d5e0a73bf71fbfcf7e1ee37))

## [7.4.4](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.4.3...v7.4.4) (2026-06-08)

### Bug Fixes

* **vr:** guard LockpickingMenu against unloaded models ([#16](https://github.com/alandtse/EngineFixesSkyrim64/issues/16)) ([3f369f3](https://github.com/alandtse/EngineFixesSkyrim64/commit/3f369f34c0be9890da8df0d6a312209928d97797))

## [7.4.3](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.4.2...v7.4.3) (2026-06-08)

### Bug Fixes

* **renderpass:** use pad44 as quarantine sentinel ([#15](https://github.com/alandtse/EngineFixesSkyrim64/issues/15)) ([90777d7](https://github.com/alandtse/EngineFixesSkyrim64/commit/90777d717cb1c5da477ed65657d23944305eaadd))
* **vr:** ability-condition throttle gate ([#14](https://github.com/alandtse/EngineFixesSkyrim64/issues/14)) ([4e5d770](https://github.com/alandtse/EngineFixesSkyrim64/commit/4e5d7709488f66580c782636b6399c85e4866b0e))

## [7.4.2](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.4.1...v7.4.2) (2026-06-07)

### Bug Fixes

* **renderpass:** defer BSRenderPass free to fix render-thread UAF ([#11](https://github.com/alandtse/EngineFixesSkyrim64/issues/11)) ([211d5f9](https://github.com/alandtse/EngineFixesSkyrim64/commit/211d5f92a5ce7dd2974affb4757c10f632dec3ab)), closes [#1601](https://github.com/alandtse/EngineFixesSkyrim64/issues/1601)

## [7.4.1](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.4.0...v7.4.1) (2026-06-04)

### Bug Fixes

* **vr:** correct cosave save path and enlarge trampoline pool ([#9](https://github.com/alandtse/EngineFixesSkyrim64/issues/9)) ([a0bf6b8](https://github.com/alandtse/EngineFixesSkyrim64/commit/a0bf6b8e7b66e9a669ad5e491036e6ec39962de5))

## [7.4.0](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.3.0...v7.4.0) (2026-06-04)

### Features

* guard Sky::UpdateClouds against null cloud object ([#6](https://github.com/alandtse/EngineFixesSkyrim64/issues/6)) ([ae56e78](https://github.com/alandtse/EngineFixesSkyrim64/commit/ae56e782de33e8e3ed959674ac6b985c6d9b987a))
* **VR:** guard CopyBoneTransform against null-ptr crash on missing bone ([#3](https://github.com/alandtse/EngineFixesSkyrim64/issues/3)) ([08b8d0e](https://github.com/alandtse/EngineFixesSkyrim64/commit/08b8d0ec854c8c3fd027d67674faa62ce482a5f6))

### Bug Fixes

* GetGameSetting NOT FOUND null-deref crash on invalid setting name ([#2](https://github.com/alandtse/EngineFixesSkyrim64/issues/2)) ([f2cadee](https://github.com/alandtse/EngineFixesSkyrim64/commit/f2cadee81a8ce2ebd7692eba54972ea45a723ca3))
* **VR:** faithfully port AbilityConditionBug throttle from EngineFixesVR ([#8](https://github.com/alandtse/EngineFixesSkyrim64/issues/8)) ([6911d51](https://github.com/alandtse/EngineFixesSkyrim64/commit/6911d51485d5e7216503ea475ccd1e2d09f0bbfb))

## [7.3.0](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.2.1...v7.3.0) (2026-05-02)

### Features

* add BSGraphics::TriShape::Release buffer guard fix ([ed4988b](https://github.com/alandtse/EngineFixesSkyrim64/commit/ed4988ba88d845f7677b0edce45aeb1356031f5c))

### Bug Fixes

* **VR:** pass correct register to AbilityConditionBug hook ([d783600](https://github.com/alandtse/EngineFixesSkyrim64/commit/d783600a65f132a42e9e7d88403b95feed7a9116))

## [7.2.1](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.2.0...v7.2.1) (2026-04-23)

### Bug Fixes

* **VR:** bWaterflowAnimation addresses ([f6e38fb](https://github.com/alandtse/EngineFixesSkyrim64/commit/f6e38fb7db45055f27586ad05386a91d4623c5d1))

## [7.2.0](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.1.3...v7.2.0) (2026-04-15)

### Features

* add BSTaskPool null vtable crash fix ([3d545ee](https://github.com/alandtse/EngineFixesSkyrim64/commit/3d545ee893e276714829c6edc979f777342f2b4b))

## [7.1.3](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.1.2...v7.1.3) (2026-04-14)

### Bug Fixes

* **VR:** skip/sleep crashes ([3acc7e7](https://github.com/alandtse/EngineFixesSkyrim64/commit/3acc7e7b15a9850749cf61b65a6ecfdb51011b42))

## [7.1.2](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.1.1...v7.1.2) (2026-04-13)

### Bug Fixes

* **VR:** map crash due to Ambient Specular fix ([40ad37a](https://github.com/alandtse/EngineFixesSkyrim64/commit/40ad37aa6108a7daa6c7dd2387598be05f76efe2))

## [7.1.1](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.1.0...v7.1.1) (2026-04-13)

### Bug Fixes

* **settings:** correct TOML key names for uRefrMainMenuLimit and uRefrLoadedGameLimit ([4adb1c5](https://github.com/alandtse/EngineFixesSkyrim64/commit/4adb1c5576b04f5a7adf1ba6f5d343c775082817))

## [7.1.0](https://github.com/alandtse/EngineFixesSkyrim64/compare/v7.0.20...v7.1.0) (2026-04-12)

### Features

* **fixes:** add VR-only IsPlayerInRegion parent cell null-check ([9adbe1c](https://github.com/alandtse/EngineFixesSkyrim64/commit/9adbe1cba403042a47517d7ecaf327fb14b83be5))
* ngify ([3cb54e0](https://github.com/alandtse/EngineFixesSkyrim64/commit/3cb54e05fe7fc3591533811c55f3a6765df8ebdf))

### Bug Fixes

* **ci:** restore version; fix tag fetch for semantic-release ([c25fdee](https://github.com/alandtse/EngineFixesSkyrim64/commit/c25fdee579f89a2aceadc5c869473edf0dd94bea))
* incorrect detection of a stuck button in the stack ([14af82a](https://github.com/alandtse/EngineFixesSkyrim64/commit/14af82af7e95c52eaff713d9f64d5d0159ed44a5))
* stuck mouse buttons ([3d07280](https://github.com/alandtse/EngineFixesSkyrim64/commit/3d072801d04e3f08406fc78b05b9b702ad5ebd5d))

### Refactors

* **fixes:** use CommonLib enum constants; update CommonLibSSE submodule ([2535e74](https://github.com/alandtse/EngineFixesSkyrim64/commit/2535e74ca21284973b94a47847202c01c5be6c08))
* **memory:** tie ScrapHeap install to MemoryManager setting ([373b455](https://github.com/alandtse/EngineFixesSkyrim64/commit/373b455bce13d3d87985dfbc3e35af817a791023))
