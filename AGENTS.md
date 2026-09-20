# AGENTS.md

Guidance for AI coding agents (and human contributors) working in this repository.

## Project snapshot

Engine Fixes is an SKSE plugin that patches individually-toggleable bugs in the Skyrim SE/AE/VR
engine binary at runtime, via raw byte-level inline hooks (`REL::Relocation` + hand-written
`Xbyak::CodeGenerator` trampolines) and vtable hooks rather than source-level reimplementation.
Every fix lives under `src/fixes/` and is gated by its own boolean setting.

One binary branches on `REL::Module::IsVR()`/`IsAE()` at runtime, and there is a single CMake
preset (no se/ae/vr matrix). Nothing in the build catches a fix that is only correct for one
runtime, so verifying each runtime a change touches is a manual discipline (see Testing).

Base branch for PRs: **`main`**. Never push directly to `main`; release automation
(`chore(release): X.Y.Z [skip ci]`) runs from there.

## Adding or changing a setting

`src/settings_schema.h` is the single source of truth: one `X(type, key, default, comment)` entry
drives both the runtime binding and the generated `EngineFixes.toml`. Add settings there, never
by hand-editing generated output or declaring them twice.

- Keep each entry within the macro's existing line-continuation column; a longer line makes
  clang-format re-align the whole block and bloats the diff.
- Update `SETTINGS.md`'s table by hand and keep its description identical to the schema `comment`.
- The shipped `Skyrim/Data/SKSE/Plugins/EngineFixes.toml` is generated on first run and the repo
  copy lags. For a new setting, copy just its line from a freshly generated file; don't
  regenerate the whole file.

## Build & verify

```powershell
cmake --preset vs2026-windows-vcpkg
cmake --build --preset release   # or: --preset debug
```

This builds in `build/`. A post-build step copies the DLL into the installed game's `Data`
folder; that copy fails with "Permission denied" while the game runs (expected, the compile
already succeeded). Use a separate build directory (`cmake -B <dir> -S . --preset
vs2026-windows-vcpkg`) only when something else is using `build/`, or when the link fails with
`LNK1201` on a PDB that can't be overwritten (rename the stale `.pdb` aside, or use a new dir).
Pre-commit hooks (clang-format) may rewrite files: re-add and commit again, never `--no-verify`.

## Writing a byte-patch or hook fix

Disassemble the real target in Ghidra and read the actual bytes, registers and slots **per
runtime** before writing any patch; never guess or copy a sibling fix's pattern. Every patch or
hook must verify what it expects (a `SiteMatches*`-style byte check, or the vtable slot and
call-site target against the expected address) before installing, and skip with a `warn` on a
mismatch. A plausible-looking wrong guess that byte-matches the wrong thing is the failure mode
this discipline exists to prevent.

- **SE and VR are often, not always, byte-identical.** Compare the whole function body, not just
  the leading instructions, before reusing a guard across them.
- **AE is often not identical** to SE/VR, and its own point releases (1.6.1170, 1.7.99, 1.7.104)
  can differ in registers, instruction order and vtable layout. Check them independently.
- Prefer `REL::ID(<id>)` / `RELOCATION_ID(se, ae)` over raw `REL::Offset{}` once the id exists in
  [`skyrim_vr_address_library`](https://github.com/alandtse/skyrim_vr_address_library); for VR
  the id must be in a released library. A missing id aborts the game at load
  (`report_id_lookup_failure`), so don't depend on an id before its mapping has shipped. Vtable
  hooks use CommonLibVR's `VTABLE_*` and need no id. A raw-offset anchor is acceptable only as a
  documented stopgap; fix the address library instead of keeping it.
- Validate anything read from outside compiled code (settings TOML, cosaves, save data), and
  bounds-check any index or pointer derived from game-controlled values before it reaches a
  patch site.

## Code quality

- **Comments: default to none.** State a fact and its consequence in 1–2 lines only when the why
  is non-obvious from the code; design rationale belongs in the PR body.
- **Minimal churn.** No placeholders; complete solutions with real resource management around
  anything allocated outside the engine's pools (see `Memory::RenderPassCache`'s deferred-free
  quarantine). Descriptive names; magic numbers become named constants.
- **Flag cross-thread races and lifetime hazards proactively,** even without a live repro, and
  surface gaps (a raw-offset anchor, an unchecked AE point release) instead of shipping around
  them. Verify facts (ids, registers, byte-identity) from the binary, not from memory.
- **Log severity matches reality:** a skipped site is `warn`; an install failure that leaves a fix
  inert is called out clearly. Fail soft: one mismatched site skips that site, never the process.

## Testing

- Build, then run the change live before calling it done.
- **Static verification covers every runtime the change touches.** Check each with Ghidra
  (addresses, bytes, vtable slots, ids): SE 1.5.97, AE 1.6.1170, AE 1.7.104, VR 1.4.15.
- **Live testing needs VR 1.4.15 plus one flat variant** (SE 1.5.97, AE 1.6.1170 or AE 1.7.104).
  The PR body says which runtimes ran in game and which were only verified statically.

## Commits & PRs

- Conventional Commits, title ≤ 50 chars. `fix:`/`perf:` → patch, `feat:` → minor,
  `build:`/`chore:`/`ci:`/`docs:`/`refactor:`/`style:`/`test:` → no release. The squash-merged PR
  title drives the version bump, so get its type right. A brand-new guard plus setting is `feat:`;
  `fix:` is for repairing a shipped fix's own bug. `ci` is a type, never a scope.
- Describe the change for a reviewer, not the session history. Re-read this section and
  Collaboration immediately before `git commit` / `gh pr create`.
- CodeRabbit skips this repo, so review is manual; a review sweep reads each review's full body,
  not just inline `reviewThreads` ("outside diff range" findings hide in the body).

## Collaboration / git safety

- Confirm before pushing to any remote. Never force-push or rewrite history on `main`.
- Semantic-release owns tags and version fields; don't create or edit them.
- If a fix depends on an unmerged PR (another fix, or an address-library PR in another repo), say
  so in the PR body and sequence the merges so the dependency ships first.
