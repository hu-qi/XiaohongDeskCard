#!/usr/bin/env python3
"""Read-only, privacy-aware evidence probe for the REAL Xiaohong WS63 SDK.

No build, signing, board operation, secret scraping, source modification, or
network access. Output carries file hashes, path names, recognized identifiers,
and bounded matched-line metadata, never source file contents or credentials.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from typing import Any


VENDOR = Path("vendor/atomgit/xiaohong")
TARGET = VENDOR / "xiaohong"
FILES: dict[str, str] = {
    "gn": "BUILD.gn",
    "lvgl_task": "src/display/lvgl_task.c",
    "display_event": "src/display/disp_driver.h",
    "wifi_task": "src/network/net_wifi_task.c",
    "http_server": "src/network/net_http_server.c",
    "board_config": "src/boards/xiaohong_ws63_v1/board_config.c",
    "flash_driver": "src/drivers/w25q128.c",
    "board_options": "config.gni",
}

# Safe fixed identifier presence only, not arbitrary line extraction.
TOKENS: dict[str, tuple[str, ...]] = {
    "gn": (
        'static_library("xiaohong")',
        "config_lvgl_enable",
        "include_dirs",
        "sources",
        "deskcard",
    ),
    "lvgl_task": (
        "g_disp_event_qid",
        "osMessageQueueNew",
        "osMessageQueueGet",
        "lv_timer_handler",
        "lv_task_handler",
        "lv_init",
    ),
    "display_event": (
        "eDisp_Max", "eDisp_Desk_Update", "typedef enum",
        "osMessageQueueId_t",
    ),
    "wifi_task": (
        "wifi", "osThreadNew", "socket", "http",
    ),
    "http_server": ("http", "socket", "recv", "send"),
    "board_options": ("config_lvgl_enable", "config_lcd_panel"),
}

SAFE_DISCOVERY: dict[str, str] = {
    "driver": "driver",
    "drivers": "drivers",
    "display": "display",
    "network": "network",
    "boards": "boards",
}

SENSITIVE_NAMES = frozenset((
    "password", "passwd", "secret", "token", "credential",
    "private", "key", "wpa", "ssid",
))


def _commit(directory: Path) -> str | None:
    try:
        result = subprocess.run(
            ["git", "-C", str(directory), "rev-parse", "HEAD"],
            timeout=6, check=False, capture_output=True, text=True,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    sha = result.stdout.strip().lower()
    return sha if result.returncode == 0 and re.fullmatch(r"[a-f0-9]{40}", sha) else None


def _inspect_file(path: Path, keys: tuple[str, ...]) -> dict[str, Any]:
    if not path.is_file() or path.is_symlink():
        return {"exists": False}
    # Do not process huge generated binaries, unbounded text or unrelated files.
    if path.stat().st_size > 1024 * 1024:
        return {"exists": True, "sizeBytes": path.stat().st_size,
                "skipped": "file_exceeds_1MiB"}
    raw = path.read_bytes()
    source = raw.decode("utf-8", errors="replace")
    return {
        "exists": True,
        "sizeBytes": len(raw),
        "sha256": hashlib.sha256(raw).hexdigest(),
        "identifiers": {token: token in source for token in keys},
    }


def _lvgl_version(root: Path) -> dict[str, Any]:
    candidates = (
        root / "third_party/lvgl/src/lv_version.h",
        root / "third_party/lvgl/lvgl.h",
        root / "third_party/lvgl/src/lv_conf_internal.h",
    )
    for candidate in candidates:
        if candidate.is_symlink() or not candidate.is_file():
            continue
        if candidate.stat().st_size > 1024 * 1024:
            continue
        data = candidate.read_text(encoding="utf-8", errors="replace")
        result = {"path": candidate.relative_to(root).as_posix(),
                  "sha256": hashlib.sha256(candidate.read_bytes()).hexdigest()}
        for variable in ("LVGL_VERSION_MAJOR", "LVGL_VERSION_MINOR",
                         "LVGL_VERSION_PATCH"):
            match = re.search(
                rf"#\s*define\s+{variable}\s+(\d+)", data
            )
            if match:
                result[variable] = int(match.group(1))
        return result
    return {"present": False, "note": "LVGL source path/version unresolved"}


def _scan_names(base: Path, limit: int = 80) -> list[str]:
    """Walk only known board source areas. Return safe *filenames* only."""
    if not base.is_dir() or base.is_symlink():
        return []
    seen = []
    patterns = ("flash", "w25q", "gatt", "ssap", "nearlink",
                "ble", "sle", "http", "wifi", "st7789")
    for file in sorted(base.rglob("*")):
        if len(seen) >= limit:
            break
        if file.is_symlink() or not file.is_file() or file.suffix.lower() not in (".c", ".h"):
            continue
        if any(word in file.name.lower() for word in patterns):
            rel = file.relative_to(base).as_posix()
            if any(x in rel.lower() for x in SENSITIVE_NAMES):
                continue
            seen.append(rel)
    return seen


def inspect(root: Path) -> dict[str, Any]:
    root = root.expanduser().resolve()
    vendor = root / VENDOR
    base = root / TARGET
    files = {
        label: {"path": (TARGET / relative).as_posix(),
                **_inspect_file(base / relative, TOKENS.get(label, ()))}
        for label, relative in FILES.items()
    }
    discovered = {
        area: _scan_names(base / "src" / path)
        for area, path in SAFE_DISCOVERY.items()
    }
    return {
        "format": "XiaohongDeskCard-sdk-probe-v1",
        "vendorCommit": _commit(vendor),
        "vendorTreePresent": vendor.is_dir(),
        "sourceFiles": files,
        "lvglVersion": _lvgl_version(root),
        "candidateSourceNames": discovered,
        "boardSdkCompiled": False,
        "boardFirmwareFlashed": False,
        "phoneDeviceTested": False,
        "note": ("Read-only identifier/sha/filename probe; never a firmware "
                 "build, SDK API compatibility proof or credential scan."),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="Existing OpenHarmony Xiaohong SDK source root")
    parser.add_argument("--output", type=Path,
                        help="Optional report path; refuses to overwrite")
    args = parser.parse_args()
    result = inspect(args.root)
    data = json.dumps(result, indent=2, ensure_ascii=False) + "\n"
    if args.output:
        output = args.output.expanduser()
        try:
            with output.open("x", encoding="utf-8") as file:
                file.write(data)
        except FileExistsError:
            print("BLOCKED: output file already exists", file=sys.stderr)
            return 2
    else:
        sys.stdout.write(data)
    return 0 if result["vendorTreePresent"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
