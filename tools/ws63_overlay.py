#!/usr/bin/env python3
"""Stage a reviewable WS63 source overlay and BUILD.gn snippet, offline.

This deliberately NEVER rewrites an upstream OpenHarmony checkout.
The operator reviews the output and integrates it into the matching version of
vendor/atomgit/xiaohong/xiaohong/BUILD.gn after running ws63_preflight.py.
No SDK build, device interaction, or firmware flashing is performed here.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import sys


ROOT = Path(__file__).resolve().parents[1]

CORE_SOURCES = (
    "desk_core.c",
    "desk_wire.c",
    "desk_packet.c",
    "desk_dispatcher.c",
    "desk_radio_ingress.c",
    "desk_nv_store.c",
)
UI_SOURCES = ("desk_view_lvgl.c", "desk_ws63_glue.c")

CORE_HEADERS = tuple(name.replace(".c", ".h") for name in CORE_SOURCES)
UI_HEADERS = ("desk_view_lvgl.h", "desk_ws63_glue.h")


def files_to_stage(source_root: Path) -> list[tuple[Path, Path]]:
    entries: list[tuple[Path, Path]] = []
    for name in CORE_SOURCES:
        entries.append((
            source_root / "firmware/core/src" / name,
            Path("src/deskcard") / name,
        ))
    for name in CORE_HEADERS:
        entries.append((
            source_root / "firmware/core/include" / name,
            Path("src/deskcard/include") / name,
        ))
    for name in UI_SOURCES:
        folder = "display" if name.startswith("desk_view") else "platform/ws63"
        entries.append((
            source_root / "firmware" / folder / name,
            Path("src/deskcard") / name,
        ))
    for name in UI_HEADERS:
        folder = "display" if name.startswith("desk_view") else "platform/ws63"
        entries.append((
            source_root / "firmware" / folder / name,
            Path("src/deskcard/include") / name,
        ))
    return entries


def gn_snippet() -> str:
    core_list = "\n".join(
        f'    "src/deskcard/{name}",' for name in CORE_SOURCES
    )
    ui_list = "\n".join(
        f'    "src/deskcard/{name}",' for name in UI_SOURCES
    )
    return f"""# XiaohongDeskCard generated integration notes: REVIEW BEFORE APPLYING.
# Path base: vendor/atomgit/xiaohong/xiaohong/BUILD.gn
# These are GN FRAGMENTS, not a standalone BUILD.gn file.
#
# Add INSIDE the upstream static_library("xiaohong") block:
include_dirs += [
    "src/deskcard/include",
]
sources += [
{core_list}
]

# Add INSIDE THE EXISTING if (config_lvgl_enable) block:
# (do not nest in a second conflicting condition)
sources += [
{ui_list}
]

# Actual WS63 source binding remains to implement:
#   CMSIS display event queue / lock / on-result ACK queue
#   2 independent Flash erase sectors with journal read/erase/program HAL
#   BLE GATT Server + SLE SSAP Server service tables and ownership
#   Wi-Fi HTTP listener and GET status / PUT content handler
# There is NO complete runnable firmware here.
"""


def stage(source_root: Path, output: Path) -> dict:
    source_root = source_root.resolve()
    output = output.expanduser().resolve()
    entries = files_to_stage(source_root)
    missing = [str(src) for src, _ in entries if not src.is_file()]
    if missing:
        raise FileNotFoundError("Missing tracked DeskCard files:\n" +
                                "\n".join(missing))
    if output.exists():
        raise FileExistsError(f"Refusing to overwrite existing stage: {output}")

    # All files are validated BEFORE making a new output directory.
    output.mkdir(parents=True, exist_ok=False)
    manifest_files: list[dict[str, str]] = []
    try:
        for original, relative in entries:
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, target)
            data = target.read_bytes()
            manifest_files.append({
                "source": str(original.relative_to(source_root)),
                "staged": relative.as_posix(),
                "sha256": hashlib.sha256(data).hexdigest(),
            })
        (output / "BUILD.gn.fragment").write_text(
            gn_snippet(), encoding="utf-8"
        )
        manifest = {
            "format": "XiaohongDeskCard-ws63-overlay-v1",
            "sourceCount": len(CORE_SOURCES) + len(UI_SOURCES),
            "headerCount": len(CORE_HEADERS) + len(UI_HEADERS),
            "upstreamSdkCompiled": False,
            "firmwareFlashValidated": False,
            "files": manifest_files,
        }
        (output / "manifest.json").write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
        )
        return manifest
    except Exception:
        # No half-completed output that looks like a valid integrated SDK.
        shutil.rmtree(output)
        raise


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--source-root", type=Path, default=ROOT,
        help="XiaohongDeskCard clone (default: script repository root)"
    )
    parser.add_argument(
        "--output", type=Path, required=True,
        help="New, nonexisting staging directory; upstream checkout is never edited"
    )
    args = parser.parse_args()
    try:
        info = stage(args.source_root, args.output)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    print(
        f"Staged {info['sourceCount']} C sources and "
        f"{info['headerCount']} headers in {args.output}"
    )
    print("Review BUILD.gn.fragment; NO WS63 SDK build or flash performed.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
