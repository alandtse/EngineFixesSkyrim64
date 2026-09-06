# AGENTS.md

Guidance for AI coding agents (and human contributors) working in this repository.

## Project snapshot

Engine Fixes is an SKSE plugin that patches specific, individually-toggleable bugs in the
Skyrim SE/AE/VR engine binary at runtime, via raw byte-level inline hooks (`REL::Relocation`
+ hand-written `Xbyak::CodeGenerator` trampolines) rather than source-level reimplementation.
Every fix lives under `src/fixes/`, is gated by its own boolean setting (see below), and is
independently toggleable so a regression in one fix never has to disable the rest.

Unlike CommonLibVR (which this plugin links against), there is no compile-time SE/AE/VR
split here — one binary branches on `REL::Module::IsVR()`/`IsAE()` at runtime, and there is
only a single CMake preset (no se/ae/vr/flatrim/all matrix to build). That means nothing in
the build catches a fix that's only correct for one runtime — verifying a change against
every runtime it touches is a manual discipline, not something CI/the build system enforces
for you. See the next section.

Base branch for PRs: **`main`**. Never push directly to `main` — release automation
(`chore(release): X.Y.Z [skip ci]`) runs from there.

## Adding or changing a setting

`src/settings_schema.h` is the single source of truth: one `X(type, key, default, comment)`
macro entry drives both the runtime binding (`settings.h`/`.cpp`) and the generated
`EngineFixes.toml`. Add a setting there, not by hand-editing the generated TOML or adding a
second declaration elsewhere — the schema existing specifically to prevent the two from
drifting apart (a real past bug: settings that were declared but never made it into the
shipped TOML). Update `SETTINGS.md`'s table to match, and keep its per-setting description in
sync with the schema's `comment` field — the doc table is not auto-generated from the schema.

## Build & verify before claiming done

```powershell
cmake --preset vs2026-windows-vcpkg
cmake --build --preset release   # or: --preset debug
```

This builds in the repo's own `build/` directory (the preset's default). A build's post-build
step copies the DLL into the installed game's `Data` folder — that copy step will fail with
"Permission denied" if the target game process is currently running (this is expected, not a
build failure; the compile itself already succeeded by that point).

Only reach for a separate, external build directory (`cmake -B <external-dir> -S .
--preset vs2026-windows-vcpkg`) if something else is genuinely using the repo's own `build/`
at the same time (e.g. another concurrent agent/session) — it's the exception, not the
default.

## Writing a raw byte-patch fix — verify against the real binary, every time

This codebase's fixes are exact byte-level patches. Every `Patch*` struct pairs with a
`SiteMatches*` byte-array check that gates whether the patch installs at all — never skip
writing that check, and never guess the expected bytes: **disassemble the real target binary
in Ghidra and read the actual bytes/registers**, per runtime, before writing the
`Xbyak::CodeGenerator` body. A wrong guess is supposed to fail safe (the site is skipped, a
warning is logged, nothing is corrupted) — but a *plausible-looking* wrong guess that
happens to byte-match the wrong thing is exactly the failure mode this pattern exists to
prevent, so the discipline matters even though the immediate failure mode is soft.

- **SE and VR are frequently, but not always, byte-identical** to each other at a given
  function — verify with a full byte-for-byte comparison of the whole function body, not
  just the leading instructions, before assuming one runtime's guard can be reused for
  another via `REL::ID()` alone.
- **AE is frequently NOT identical** — different register allocation, sometimes different
  instruction order, occasionally a genuinely different vtable/slot layout across AE's own
  point releases (1.6.1170 vs 1.7.99 vs 1.7.104). Check the AE range's own point releases
  independently rather than assuming the boundary you happened to test is representative —
  confirm at more than one AE build before treating "AE" as a single case.
- Prefer `REL::ID(<id>)` over a raw `REL::Offset{}` anchor once the id has a real mapping in
  [`skyrim_vr_address_library`](https://github.com/alandtse/skyrim_vr_address_library). A
  raw-offset anchor is a sign the address-library mapping is missing or wrong for that
  runtime — fix the address library (separate repo/PR) rather than leaving the raw offset as
  the permanent anchor. A `REL::ID()` call for an id missing from the currently-installed
  address library aborts the game at load (`REL/IDDB.cpp`'s `report_id_lookup_failure`) —
  don't land a fix that depends on an address-library id before that id's mapping has
  actually shipped.

## Code quality

- **Comments state a fact and its consequence, nothing more.** Default to none. Only write one
  when the *why* is genuinely non-obvious from the code alone. Target 1–2 lines; an
  extraordinary-invariant exception tops out at 3–4. Don't restate what the code already says,
  and don't narrate design rationale that belongs in the PR body instead.
- **Minimal churn** — touch only what the change requires.
- **No placeholders, complete solutions.** A fix ships complete — no stubbed guard "to be
  filled in later," and real resource management around every allocation this plugin makes
  outside the engine's own pools (see `Memory::RenderPassCache`'s deferred-free quarantine for
  what "getting this right" looks like for a lifetime-sensitive resource).
- **Descriptive naming**, one job per function/struct.

## Constructive proactivity

- Flag a plausible cross-thread race or lifetime issue proactively, even if you can't prove it
  without a live repro — this codebase already has one quarantine mechanism
  (`Memory::RenderPassCache`) built specifically because a "looks safe" immediate-free wasn't.
- Prefer surfacing a gap (a raw-offset anchor standing in for a missing address-library id, an
  AE point release you haven't independently checked) over silently shipping around it.
- **Verify identifying facts; don't confabulate.** A relocation id, a runtime's actual register
  allocation at a patch site, whether two runtimes are byte-identical — read it from the binary,
  don't assume it from a sibling fix's pattern.

## Security & input validation

- Validate anything this plugin reads from outside its own compiled code: the settings TOML,
  save-adjacent data, cosave files. Malformed input must not crash or corrupt state — see
  `src/settings_schema.h`'s self-healing generator for the settings side of this.
- Bounds-check any index or pointer arithmetic derived from a value the game or a save file
  controls before it reaches a raw byte-patch site.

## Error handling

- Log at a severity that matches reality: a skipped/no-op site (byte mismatch, unsupported
  runtime) is `warn`, not silent and not `error`; an actual install failure that leaves a fix
  inert is worth calling out clearly.
- Degrade gracefully by design — a `SiteMatches*` mismatch skips that one site rather than
  aborting the whole fix or the process; keep new fixes to that same fail-soft shape rather
  than a hard fault on an unexpected byte pattern.

## Testing & validation

- Build and, where feasible, run the changed fix against a live game session before calling it
  done — see the build/verify section above.
- **Verify every runtime the change actually touches, not just the one you happened to test.**
  Since there's no build-time SE/AE/VR split (see Project snapshot), "it built" proves nothing
  about runtime correctness on the other two. A change to a shared/per-runtime patch site
  needs its own check against each runtime it applies to — and if it applies across AE's own
  point releases (1.6.1170, 1.7.99, 1.7.104, ...), check more than one of those too rather than
  assuming they match.
- **Never bypass commit verification** (`--no-verify` or otherwise skipping pre-commit/
  commit-msg hooks) unless the user explicitly directs it for a specific commit.

## Commits & PRs

- Conventional Commits (`type(scope): description`), title ≤ 50 chars.
  `fix:`/`perf:` → patch, `feat:` → minor, `build:`/`chore:`/`ci:`/`docs:`/`refactor:`/`style:`/`test:`
  → no release. The squash-merged PR title is what semantic-release reads for the version
  bump — get its type right. **`ci` is its own type, not a scope** — a workflow/CI-config-only
  change is `ci: ...`, never `fix(ci): ...`/`feat(ci): ...`.
- PR/commit descriptions describe the change for a reviewer, not the session history that
  produced it.
- Treat `git commit`/`gh pr create` as a hard checkpoint: re-read this file's Commits & PRs and
  Collaboration sections immediately before either.

## Collaboration / git safety

- Never force-push or rewrite history on `main` without explicit instruction. Confirm before
  pushing to any remote.
- A review sweep must read each review's full body text, not just inline `reviewThreads` —
  "outside diff range" findings are often embedded in the review body with no inline thread.
- Don't manually create release tags or hand-edit version fields — semantic-release owns both
  on merge to `main`.
- If a fix depends on an unmerged upstream PR (another fix, or an address-library PR in a
  different repo), say so explicitly in the PR body and sequence the merges accordingly —
  don't let a fix ship in an order where its dependency isn't live yet.
