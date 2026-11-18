#!/usr/bin/env python3
"""Offline *source-layout* preflight for Xiaohong OpenHarmony 7 WS63 tree.

This does NOT build firmware, edit upstream files, download SDKs or touch a
device. Checks that expected upstream source anchor files exist and prints
evidence for manually reviewing the proposed integration.
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys


REQUIRED = {
    "vendor_build": "vendor/atomgit/xiaohong/xiaohong/BUILD.gn",
    "lvgl_task": "vendor/atomgit/xiaohong/xiaohong/src/display/lvgl_task.c",
    "disp_driver": "vendor/atomgit/xiaohong/xiaohong/src/display/disp_driver.h",
    "wifi_task": "vendor/atomgit/xiaohong/xiaohong/src/network/net_wifi_task.c",
}
OPTIONAL = {
    "sdk_config": "device/soc/hisilicon/ws63v100/sdkv106/build/config/target_config/ws63/config.py",
    "vendor_config": "vendor/atomgit/xiaohong/xiaohong/config.gni",
    "sle_sample": "vendor/atomgit/xiaohong/samples/13_sle_uart",
    "upstream_docs": "vendor/atomgit/xiaohong/README.md",
}
ANCHORS = {
    "lvgl_task": ("lv_timer_handler", "g_disp_event_qid"),
    "disp_driver": ("eDisp_Max",),
    "vendor_build": ("static_library",),
    "wifi_task": ("wifi",),
}


def inspect(root: Path) -> dict:
    root = root.expanduser().resolve()
    files: dict[str, dict] = {}
    missing: list[str] = []
    for group, items in (("required", REQUIRED), ("optional", OPTIONAL)):
        for label, relative in items.items():
            candidate = root / relative
            exists = candidate.is_file() if candidate.suffix else candidate.exists()
            record: dict = {
                "path": relative, "required": group == "required", "exists": exists
            }
            if exists and candidate.is_file():
                text = candidate.read_text(encoding="utf-8", errors="replace")
                record["sizeBytes"] = candidate.stat().st_size
                record["anchors"] = {
                    a: (a in text) for a in ANCHORS.get(label, ())
                }
            files[label] = record
            if group == "required" and not exists:
                missing.append(relative)
    vendor = root / "vendor/atomgit/xiaohong"
    commit = None
    if vendor.is_dir():
        try:
            process = subprocess.run(
                ["git", "-C", str(vendor), "rev-parse", "HEAD"],
                capture_output=True, text=True, timeout=5, check=False
            )
            if process.returncode == 0:
                commit = process.stdout.strip()
        except (OSError, subprocess.TimeoutExpired):
            pass
    return {
        "tree": str(root),
        "allRequiredPresent": not missing,
        "missingRequired": missing,
        "upstreamVendorCommit": commit,
        "checks": files,
        "sdkCompiled": False,
        "hardwareValidated": False,
        "note": "Source preflight only; SDK APIs, GN integration and firmware need separate validation."
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, required=True,
                        help="Root of a checked-out Xiaohong OpenHarmony source tree")
    parser.add_argument("--json", action="store_true", help="Machine-readable report")
    args = parser.parse_args()
    result = inspect(args.root)
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print("XiaohongDeskCard WS63 source preflight:", result["tree"])
        for key, info in result["checks"].items():
            status = "OK" if info["exists"] else ("MISSING" if info["required"] else "OPTIONAL")
            print(f" [{status}] {key}: {info['path']}")
            if "anchors" in info:
                for token, exists in info["anchors"].items():
                    print(f"    {'+' if exists else '-'} {token}")
        print("Upstream vendor commit:", result["upstreamVendorCommit"] or "(not found)")
        print("Firmware compile: NOT RUN; hardware: NOT USED")
    return 0 if result["allRequiredPresent"] else 2


if __name__ == "__main__":
    sys.exit(main())
