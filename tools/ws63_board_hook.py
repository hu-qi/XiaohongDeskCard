#!/usr/bin/env python3
"""Insert the DeskCard board hook into the upstream Xiaohong LvglTask.

`static_library("xiaohong")` is a `ram_component`, so the SDK links it with
normal archive semantics. Adding desk sources to BUILD.gn compiles them into
libxiaohong.a, but if nothing in the vendor tree calls desk_* the linker drops
every deskcard object from the final image. This tool adds the two calls that
create that reference:

    desk_board_init();   once, after LVGL and the vendor UI exist
    desk_board_poll();   every loop iteration of the same UI task

Design constraints, matching tools/ws63_gn_integrate.py:
  * review-first: prints a unified diff and changes nothing without --apply
  * idempotent: a second --apply reports "Already hooked."
  * minimal: only lvgl_task.c is touched, guarded by BEGIN/END markers
  * refuses to guess: if the anchor functions are missing or ambiguous the tool
    exits non-zero instead of patching a file it does not recognise
"""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

RELATIVE = Path("vendor/atomgit/xiaohong/xiaohong/src/display/lvgl_task.c")

BEGIN = "/* DESKCARD-BOARD-HOOK-BEGIN */"
END = "/* DESKCARD-BOARD-HOOK-END */"

INCLUDE_ANCHOR = '#include "disp_driver.h"'
INIT_ANCHOR = "    disp_create_ui();\n"
# The loop body `continue`s whenever the display queue is empty, which is the
# common case. Anchoring the poll at the top of the loop guarantees it runs
# every iteration instead of only when a vendor display event arrives.
POLL_ANCHOR = "    while (1) {\n"


def _include_block() -> str:
    return (
        f"{BEGIN}\n"
        '#include "desk_ws63_board.h"\n'
        f"{END}\n"
    )


def _init_block() -> str:
    return (
        f"\n    {BEGIN}\n"
        "    if (!desk_board_init()) {\n"
        '        log_error("[Desk] board init failed; desk UI disabled\\r\\n");\n'
        "    }\n"
        f"    {END}\n"
    )


def _poll_block() -> str:
    return (
        f"        {BEGIN}\n"
        "        desk_board_poll();\n"
        f"        {END}\n"
    )


def _insert_after(text: str, anchor: str, block: str, label: str) -> str:
    if text.count(anchor) != 1:
        raise ValueError(
            f"anchor for {label} appears {text.count(anchor)} times, expected 1"
        )
    index = text.index(anchor) + len(anchor)
    return text[:index] + block + text[index:]


def hook(text: str) -> str:
    if BEGIN in text:
        if END not in text:
            raise ValueError("found BEGIN marker without END; review manually")
        return text

    patched = _insert_after(text, INCLUDE_ANCHOR, _include_block(), "include")
    patched = _insert_after(patched, INIT_ANCHOR, _init_block(), "init")
    patched = _insert_after(patched, POLL_ANCHOR, _poll_block(), "poll")
    return patched


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True,
                        help="OpenHarmony source root (contains vendor/)")
    parser.add_argument("--apply", action="store_true",
                        help="write the change; default is a read-only preview")
    args = parser.parse_args(argv)

    target = Path(args.root).resolve() / RELATIVE
    if not target.is_file():
        print(f"missing {target}", file=sys.stderr)
        return 2

    original = target.read_text(encoding="utf-8")
    try:
        updated = hook(original)
    except ValueError as exc:
        print(f"refusing to patch: {exc}", file=sys.stderr)
        return 3

    if updated == original:
        print("Already hooked.")
        return 0

    diff = difflib.unified_diff(
        original.splitlines(keepends=True),
        updated.splitlines(keepends=True),
        fromfile=str(RELATIVE),
        tofile=str(RELATIVE) + " (proposed)",
    )
    sys.stdout.write("".join(diff))

    if not args.apply:
        print("\nPreview only. Re-run with --apply to write.")
        return 0

    target.write_text(updated, encoding="utf-8")
    print("\nBoard hook applied.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
