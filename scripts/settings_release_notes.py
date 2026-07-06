#!/usr/bin/env python3
"""Generate user-facing settings documentation from settings_schema.h + git history.

Every EngineFixes setting is declared exactly once in src/settings_schema.h, and the
release that introduced it is derivable from git tags, so none of this is maintained
by hand.

Modes (all print to stdout):
  notes                 markdown "New Settings" section for settings not contained in
                        any release tag yet (i.e. new in the release being cut).
                        Prints nothing when there are none -- safe as a semantic-release
                        generateNotesCmd.
  full [--next VER]     complete settings reference (SETTINGS.md), grouped by TOML
                        section with an introduced-in column, plus a by-version index
                        for binary-searching a regression to the release that shipped
                        a setting. Untagged settings are labeled VER (default
                        "unreleased").
  filedesc [--versions N]
                        compact plain-text "New settings" summary of the N most recent
                        releases that added settings (default 3), for the Nexus file
                        description.
"""

import argparse
import re
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
SCHEMA = REPO_ROOT / "src" / "settings_schema.h"

SECTION_NAMES = {
    "GENERAL": "General",
    "FIXES": "Fixes",
    "PATCHES": "Patches",
    "MEMORYMANAGER": "MemoryManager",
    "WARNINGS": "Warnings",
    "DEBUG": "Debug",
}

ENTRY_RE = re.compile(
    r'X\(\s*(Bool|I32|U32|F32)\s*,\s*(\w+)\s*,\s*([^,]+?)\s*,\s*"((?:[^"\\]|\\.)*)"\s*\)'
)
SECTION_RE = re.compile(r"#define\s+EF_SETTINGS_(\w+)\(X\)")


def git(*args):
    return subprocess.run(
        ["git", *args], cwd=REPO_ROOT, capture_output=True, text=True, check=False
    ).stdout.strip()


def parse_schema():
    """Return [(section, key, default, description)] in schema order."""
    settings = []
    section = None
    in_block = False
    for line in SCHEMA.read_text(encoding="utf-8").splitlines():
        m = SECTION_RE.search(line)
        if m:
            section = SECTION_NAMES.get(m.group(1), m.group(1))
            in_block = True
        if in_block and section:
            for t, key, default, desc in ENTRY_RE.findall(line):
                settings.append((section, key, default.strip(), desc.replace('\\"', '"')))
            if not line.rstrip().endswith("\\") and not m:
                in_block = False
    return settings


def version_key(tag):
    return tuple(int(p) for p in re.findall(r"\d+", tag)[:3])


def introduced_version(key, oldest_tag):
    """First release tag containing the setting's first-appearance commit.

    Returns (version_string, is_floor) where is_floor means the setting predates
    the oldest tag in the repo, and None version for settings not in any tag yet.
    """
    first_sha = git("log", "--reverse", "--format=%H", "-S", key, "--", "src").splitlines()
    if not first_sha:
        first_sha = git("log", "--reverse", "--format=%H", "-S", key).splitlines()
    if not first_sha:
        return None, False
    tags = [t for t in git("tag", "--list", "v*", "--contains", first_sha[0]).splitlines() if t]
    if not tags:
        return None, False
    first = min(tags, key=version_key)
    return first.lstrip("v"), first == oldest_tag


def collect():
    all_tags = [t for t in git("tag", "--list", "v*").splitlines() if t]
    oldest_tag = min(all_tags, key=version_key) if all_tags else None
    rows = []
    for section, key, default, desc in parse_schema():
        version, is_floor = introduced_version(key, oldest_tag)
        rows.append({
            "section": section,
            "key": key,
            "default": default,
            "desc": desc,
            "version": version,   # None => not in any tag yet
            "floor": is_floor,    # predates the oldest tag
        })
    return rows


def since_label(row, next_version="unreleased"):
    if row["version"] is None:
        return next_version
    return f"≤ {row['version']}" if row["floor"] else row["version"]


def mode_notes(rows):
    new = [r for r in rows if r["version"] is None]
    if not new:
        return ""
    lines = ["### New Settings", ""]
    for r in new:
        lines.append(f"* `[{r['section']}] {r['key']}` (default `{r['default']}`) — {r['desc']}")
    lines.append("")
    lines.append("The full settings reference, including the release each setting first appeared in, is in [SETTINGS.md](SETTINGS.md).")
    return "\n".join(lines)


def mode_full(rows, next_version):
    lines = [
        "# EngineFixes Settings Reference",
        "",
        "Generated from `src/settings_schema.h` and git history by",
        "`scripts/settings_release_notes.py` — do not edit by hand.",
        "",
        "The **Since** column is the release that introduced the setting; to binary-search",
        "a regression, disable the settings introduced at or after the first broken release.",
        "",
    ]
    for section in dict.fromkeys(r["section"] for r in rows):
        lines.append(f"## [{section}]")
        lines.append("")
        lines.append("| Setting | Default | Since | Description |")
        lines.append("| --- | --- | --- | --- |")
        for r in rows:
            if r["section"] != section:
                continue
            desc = r["desc"].replace("|", "\\|")
            lines.append(f"| `{r['key']}` | `{r['default']}` | {since_label(r, next_version)} | {desc} |")
        lines.append("")
    lines.append("## Settings by release")
    lines.append("")
    by_version = {}
    for r in rows:
        by_version.setdefault(since_label(r, next_version), []).append(r)

    def sort_key(label):
        nums = re.findall(r"\d+", label)
        return tuple(int(n) for n in nums[:3]) if nums else (0,)

    for label in sorted(by_version, key=sort_key, reverse=True):
        keys = ", ".join(f"`{r['key']}`" for r in by_version[label])
        lines.append(f"- **{label}**: {keys}")
    lines.append("")
    return "\n".join(lines)


def mode_filedesc(rows, versions):
    by_version = {}
    for r in rows:
        if r["version"] is None or r["floor"]:
            continue
        by_version.setdefault(r["version"], []).append(r["key"])
    recent = sorted(by_version, key=version_key, reverse=True)[:versions]
    if not recent:
        return ""
    parts = [f"{v}: {', '.join(by_version[v])}" for v in recent]
    return "New settings — " + " | ".join(parts) + " (see SETTINGS.md / changelog for details)"


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=["notes", "full", "filedesc"])
    ap.add_argument("--next", default="unreleased", help="version label for not-yet-tagged settings (full mode)")
    ap.add_argument("--versions", type=int, default=3, help="release count for filedesc mode")
    args = ap.parse_args()

    rows = collect()
    if args.mode == "notes":
        out = mode_notes(rows)
    elif args.mode == "full":
        out = mode_full(rows, args.next)
    else:
        out = mode_filedesc(rows, args.versions)
    if out:
        sys.stdout.buffer.write((out + "\n").encode("utf-8"))


if __name__ == "__main__":
    main()
