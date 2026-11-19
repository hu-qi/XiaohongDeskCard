#!/usr/bin/env python3
"""Version-pinned, review-first installation of DeskCard C sources into WS63 GN.

Default: no writes. --apply requires exact upstream vendor commit, an entirely
clean vendor BUILD.gn, complete SHA-256 verified staging, and matching GN scopes.
The tool does NOT implement SDK HALs or run hb build/flash any hardware.
"""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

from ws63_overlay import CORE_SOURCES, CORE_HEADERS, UI_SOURCES, UI_HEADERS

VENDOR = Path("vendor/atomgit/xiaohong")
BUILD = VENDOR / "xiaohong/BUILD.gn"
DEST = VENDOR / "xiaohong/src/deskcard"
BEGIN_CORE = "# DESKCARD-GN-BEGIN CORE"
END_CORE = "# DESKCARD-GN-END CORE"
BEGIN_LVGL = "# DESKCARD-GN-BEGIN LVGL"
END_LVGL = "# DESKCARD-GN-END LVGL"


def _close_brace(text: str, opened: int) -> int:
    """Find matching GN brace, ignoring quoted strings and # comments."""
    depth = 0
    quoted = False
    comment = False
    escaped = False
    for pos in range(opened, len(text)):
        c = text[pos]
        if comment:
            if c == "\n":
                comment = False
            continue
        if quoted:
            if escaped:
                escaped = False
            elif c == "\\":
                escaped = True
            elif c == '"':
                quoted = False
            continue
        if c == "#":
            comment = True
        elif c == '"':
            quoted = True
        elif c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return pos
    raise ValueError("Unbalanced GN block; refusing modification")


def _find_scope(text: str, pattern: str, start: int, end: int):
    found = list(re.finditer(pattern, text[start:end], re.MULTILINE))
    if len(found) != 1:
        raise ValueError(f"Expected exactly one GN scope matching {pattern!r}")
    match = found[0]
    opened = start + match.end() - 1
    closed = _close_brace(text, opened)
    if closed >= end:
        raise ValueError("GN block extends beyond expected parent scope")
    return opened, closed


def patch_build_gn(original: str) -> str:
    marks = (BEGIN_CORE, END_CORE, BEGIN_LVGL, END_LVGL)
    count = sum(original.count(mark) for mark in marks)
    if count:
        if all(original.count(mark) == 1 for mark in marks):
            expected = [f'"src/deskcard/{name}"'
                        for name in (*CORE_SOURCES, *UI_SOURCES)]
            if all(item in original for item in expected):
                return original  # Idempotent, no duplicated source additions.
        raise ValueError("Partial/ambiguous DeskCard GN injection; review manually")

    target_pattern = r'^\s*static_library\(\s*"xiaohong"\s*\)\s*\{'
    _, target_end = _find_scope(original, target_pattern, 0, len(original))
    target_start = original.index('{', original.index('static_library("xiaohong")'))
    target_text = original[target_start:target_end]
    if not re.search(r'(?m)^\s*sources\s*(?:\+?=)', target_text):
        raise ValueError("Target has no sources list to extend")
    if not re.search(r'(?m)^\s*include_dirs\s*(?:\+?=)', target_text):
        raise ValueError("Target has no include_dirs list to extend")

    _, lvgl_end = _find_scope(
        original, r'^\s*if\s*\(\s*config_lvgl_enable\s*\)\s*\{',
        target_start + 1, target_end,
    )
    core_lines = "\n".join(
        '    "src/deskcard/' + name + '",' for name in CORE_SOURCES
    )
    ui_lines = "\n".join(
        '      "src/deskcard/' + name + '",' for name in UI_SOURCES
    )
    core = (
        "\n  " + BEGIN_CORE + "\n"
        '  include_dirs += [ "src/deskcard/include" ]\n'
        "  sources += [\n" + core_lines + "\n  ]\n"
        "  " + END_CORE + "\n"
    )
    ui = (
        "\n    " + BEGIN_LVGL + "\n"
        "    sources += [\n" + ui_lines + "\n    ]\n"
        "    " + END_LVGL + "\n"
    )
    # Descending offsets: injecting inside the outer target first cannot
    # invalidate the earlier LVGL close position.
    patched = original[:target_end] + core + original[target_end:]
    patched = patched[:lvgl_end] + ui + patched[lvgl_end:]
    return patched


def validate_overlay(overlay: Path) -> list[tuple[Path, Path]]:
    overlay = overlay.resolve()
    manifest_path = overlay / "manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("format") != "XiaohongDeskCard-ws63-overlay-v1":
        raise ValueError("Unknown overlay manifest format")
    names = (*CORE_SOURCES, *CORE_HEADERS, *UI_SOURCES, *UI_HEADERS)
    expected = {f"src/deskcard/{name}" if name.endswith(".c")
                else f"src/deskcard/include/{name}" for name in names}
    files = manifest.get("files")
    if not isinstance(files, list) or len(files) != len(expected):
        raise ValueError("Overlay manifest file count mismatch")
    seen: set[str] = set()
    validated = []
    for entry in files:
        rel = entry.get("staged")
        if not isinstance(rel, str) or rel not in expected or rel in seen:
            raise ValueError("Unexpected or duplicated overlay file")
        seen.add(rel)
        source = (overlay / rel).resolve()
        if not source.is_relative_to(overlay) or not source.is_file():
            raise ValueError("Overlay path escapes stage or is missing")
        checksum = hashlib.sha256(source.read_bytes()).hexdigest()
        if checksum != entry.get("sha256"):
            raise ValueError("Overlay SHA256 mismatch: " + rel)
        dst = Path(rel).relative_to("src/deskcard")
        validated.append((source, dst))
    if seen != expected:
        raise ValueError("Overlay does not contain all required sources")
    return validated


def _vendor_commit(root: Path) -> str:
    vendor = root / VENDOR
    cmd = subprocess.run(
        ["git", "-C", str(vendor), "rev-parse", "HEAD"],
        capture_output=True, text=True, check=False,
    )
    if cmd.returncode or not re.fullmatch(r"[a-f0-9]{40}", cmd.stdout.strip()):
        raise ValueError("No readable pinned upstream vendor Git commit")
    return cmd.stdout.strip()


def prepare(root: Path, overlay: Path, expected_sha: str):
    root = root.resolve()
    if not re.fullmatch(r"[a-f0-9]{40}", expected_sha):
        raise ValueError("Full 40-character upstream commit SHA required")
    commit = _vendor_commit(root)
    if commit != expected_sha:
        raise ValueError("Upstream commit mismatch; refusing to patch")
    files = validate_overlay(overlay)
    build = root / BUILD
    if not build.is_file() or build.is_symlink():
        raise ValueError("No regular upstream vendor BUILD.gn found")
    original = build.read_text(encoding="utf-8")
    revised = patch_build_gn(original)
    dest = root / DEST
    if dest.is_symlink():
        raise ValueError("DeskCard destination cannot be a symbolic link")
    if dest.exists():
        if revised != original:
            raise ValueError("DeskCard destination exists but GN not integrated")
        for source, relative in files:
            present = dest / relative
            if not present.is_file() or hashlib.sha256(
                present.read_bytes()
            ).digest() != hashlib.sha256(source.read_bytes()).digest():
                raise ValueError("Existing DeskCard destination content differs")
    elif revised == original:
        raise ValueError("GN patched but source destination is missing")
    return root, build, dest, original, revised, files


def apply(root: Path, overlay: Path, expected_sha: str) -> bool:
    root, build, dest, original, revised, files = prepare(
        root, overlay, expected_sha
    )
    if revised == original:
        return False
    vendor = root / VENDOR
    status = subprocess.run(
        ["git", "-C", str(vendor), "status", "--porcelain", "--",
         "xiaohong/BUILD.gn", "xiaohong/src/deskcard"],
        capture_output=True, text=True, check=False,
    )
    if status.returncode or status.stdout.strip():
        raise ValueError("Upstream GN/source paths dirty; commit or review first")

    # Build a complete temporary source directory first.
    dest.parent.mkdir(parents=True, exist_ok=True)
    tempdir = Path(tempfile.mkdtemp(prefix=".deskcard-stage-", dir=dest.parent))
    try:
        for source, relative in files:
            copied = tempdir / relative
            copied.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, copied)
        os.replace(tempdir, dest)
        tempdir = None  # Destination now owns sources.
        # Only after all C files are installed does BUILD.gn change atomically.
        fd, name = tempfile.mkstemp(prefix=".deskcard-gn-", dir=build.parent)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as out:
                out.write(revised)
            os.chmod(name, build.stat().st_mode & 0o777)
            os.replace(name, build)
        except Exception:
            try:
                os.unlink(name)
            except OSError:
                pass
            raise
    except Exception:
        if tempdir is not None:
            shutil.rmtree(tempdir)
        # If GN remained unchanged, roll back newly installed overlay.
        if build.read_text(encoding="utf-8") == original and dest.exists():
            shutil.rmtree(dest)
        raise
    return True


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="OpenHarmony SDK source tree root")
    parser.add_argument("--overlay", type=Path, required=True,
                        help="Output of tools/ws63_overlay.py")
    parser.add_argument("--expected-upstream-commit", required=True,
                        help="Complete upstream vendor repo SHA (40 hex)")
    parser.add_argument("--apply", action="store_true",
                        help="Actually copy files and patch GN after all checks")
    args = parser.parse_args()
    try:
        root, build, dest, original, revised, files = prepare(
            args.root, args.overlay, args.expected_upstream_commit
        )
        if not args.apply:
            print("READ-ONLY PREVIEW; no SDK changes made.")
            diff = difflib.unified_diff(
                original.splitlines(keepends=True),
                revised.splitlines(keepends=True),
                fromfile=str(BUILD), tofile=str(BUILD) + " (proposed)",
            )
            sys.stdout.writelines(diff)
            print(f"Verified {len(files)} staged C/header files for {dest}")
            return 0
        changed = apply(root, args.overlay, args.expected_upstream_commit)
        print("GN + sources integrated." if changed else "Already integrated.")
        print("No SDK compilation or firmware flash performed.")
        return 0
    except (OSError, ValueError, KeyError, TypeError) as exc:
        print("BLOCKED:", str(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
