# oneTBB redistributable (vendored)

Official prebuilt Windows binaries from oneTBB v2023.0.0
(https://github.com/uxlfoundation/oneTBB/releases/tag/v2023.0.0), `intel64/vc14` release
build, extracted from `oneapi-tbb-2023.0.0-win.zip`'s `redist/intel64/vc14/` folder.

- `tbb12.dll` — dependency of `tbbmalloc_proxy.dll`.
- `tbbmalloc.dll` — the real scalable allocator; EngineFixes resolves `scalable_*` from this
  module via `GetProcAddress` instead of statically linking it (see `src/memory/allocator.cpp`).
- `tbbmalloc_proxy.dll` — Intel's official CRT malloc/free/realloc replacement mechanism.
  Loading it triggers its own `DllMain(DLL_PROCESS_ATTACH)`, which performs the real,
  foreign-pointer-safe redirection (`__TBB_malloc_safer_free`/`_realloc`) that a hand-rolled
  `SKSE::PatchIAT` approach can't provide — see git history of `allocator.cpp` for the
  abandoned hand-rolled attempt.

These three files are deployed alongside `EngineFixes.dll` (same `SKSE/Plugins/` folder) by
the CMake post-build step. `tbbmalloc_proxy.dll` can only be built as a shared library
(`src/tbbmalloc_proxy/CMakeLists.txt` returns early if `BUILD_SHARED_LIBS` is off), which
vcpkg's `tbb` port — built against this project's static `x64-windows-static-md` triplet —
never produces, so these are vendored as binaries rather than built from the submodule.

Licensed under Apache-2.0 (see `LICENSE.txt` in this folder).
