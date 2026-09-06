# AGENTS.md

Guidance for AI coding agents (and human contributors) working in this repository.

## Project snapshot

Engine Fixes is an SKSE plugin that patches specific, individually-toggleable bugs in the
Skyrim SE/AE/VR engine binary at runtime, via raw byte-level inline hooks (`REL::Relocation`
+ hand-written `Xbyak::CodeGenerator` trampolines) rather than source-level reimplementation.
Every fix lives under `src/fixes/`, is gated by its own boolean setting (see below), and is
independently toggleable so a regression in one fix never has to disable the rest.

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
cmake -B <external-build-dir> -S . --preset vs2026-windows-vcpkg
cmake --build <external-build-dir> --config Release --parallel
```

Use an **external** build directory (outside the repo) when verifying a change without
disturbing another in-progress build in the repo's own `build/`. A build's post-build step
copies the DLL into the installed game's `Data` folder — that copy step will fail with
"Permission denied" if the target game process is currently running (this is expected, not a
build failure; the compile itself already succeeded by that point).

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
  `skyrim_vr_address_library`. A raw-offset anchor is a sign the address-library mapping is
  missing or wrong for that runtime — fix the address library (separate repo/PR) rather than
  leaving the raw offset as the permanent anchor. A `REL::ID()` call for an id missing from
  the currently-installed address library aborts the game at load
  (`REL/IDDB.cpp`'s `report_id_lookup_failure`) — don't land a fix that depends on an
  address-library id before that id's mapping has actually shipped.

## Code quality

- **Comments state a fact and its consequence, nothing more.** Default to none. Only write one
  when the *why* is genuinely non-obvious from the code alone. Target 1–2 lines; an
  extraordinary-invariant exception tops out at 3–4. Don't restate what the code already says,
  and don't narrate design rationale that belongs in the PR body instead.
- **Minimal churn** — touch only what the change requires.
- **Descriptive naming**, one job per function/struct.

## Commits & PRs

- Conventional Commits (`type(scope): description`), title ≤ 50 chars.
  `fix:`/`perf:` → patch, `feat:` → minor, `build:`/`chore:`/`ci:`/`docs:`/`refactor:`/`style:`/`test:`
  → no release. The squash-merged PR title is what semantic-release reads for the version
  bump — get its type right.
- PR/commit descriptions describe the change for a reviewer, not the session history that
  produced it.
- Never force-push or rewrite history on `main` without explicit instruction. Confirm before
  pushing to any remote.
- If a fix depends on an unmerged upstream PR (another fix, or an address-library PR in a
  different repo), say so explicitly in the PR body and sequence the merges accordingly —
  don't let a fix ship in an order where its dependency isn't live yet.
