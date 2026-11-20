#!/usr/bin/env python3
"""Wire the DeskCard BLE/SLE transports into the upstream Xiaohong tree.

The Desk Card service tables live in firmware/platform/ws63/desk_ws63_adapters.c,
but nothing in the vendor firmware calls them. This tool adds the minimum set of
call sites, each guarded by BEGIN/END markers so it stays reviewable and
idempotent:

  net_ble_prov.c
    + forward GATT writes on handles the provisioning service does not own
      into desk_ble_on_write(), so both services share the vendor's single
      global gatts callback
    + register the Desk Card GATT service once BLE is enabled
    + register the Desk Card GATT service (ACKs are pumped by the UI task)

SLE and the station-mode HTTP listener are started from desk_board_init(), so
they do not depend on the provisioning task ever running.

Wi-Fi is NOT hooked here: the vendor's net_http_server.c is the AP provisioning
page and is not even compiled into this target. The Desk Card runs its own
station-mode listener (firmware/platform/ws63/desk_ws63_http.c), started from
desk_board_init(), so it needs no vendor edit at all.

Design constraints, matching tools/ws63_board_hook.py:
  * review-first: prints a unified diff and changes nothing without --apply
  * idempotent: a second --apply reports "Already hooked."
  * refuses to guess: if an anchor is missing or ambiguous the tool exits
    non-zero instead of patching a file it does not recognise
"""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

BLE_RELATIVE = Path("vendor/atomgit/xiaohong/xiaohong/src/network/net_ble_prov.c")

BEGIN = "/* DESKCARD-TRANSPORT-HOOK-BEGIN */"
END = "/* DESKCARD-TRANSPORT-HOOK-END */"

BLE_INCLUDE_ANCHOR = '#include "net_ble_prov.h"'
# The vendor's write callback ignores every handle that is not its own RX char.
BLE_WRITE_ANCHOR = (
    "    if ((write_cb_para != NULL) && (write_cb_para->handle == g_rx_handle)) {\n"
    "        ble_prov_on_rx_frame(write_cb_para->value, write_cb_para->length);\n"
    "    }\n"
)
# Called after the provisioning service is built, inside the BLE enable cbk.
BLE_SERVICE_ANCHOR = (
    "    if (ble_prov_add_service() == ERRCODE_BT_SUCCESS) {\n"
    "        (void)ble_prov_start_adv();\n"
    "    }\n"
)

def _ble_include_block() -> str:
    return (
        f"\n{BEGIN}\n"
        '#include "desk_ws63_adapters.h"\n'
        f"{END}\n"
    )


def _ble_write_block() -> str:
    return (
        f"\n    {BEGIN}\n"
        "    if ((write_cb_para != NULL) &&\n"
        "        !((write_cb_para->handle == g_rx_handle))) {\n"
        "        (void)desk_ble_on_write(write_cb_para->handle,\n"
        "                                write_cb_para->value,\n"
        "                                write_cb_para->length);\n"
        "    }\n"
        f"    {END}\n"
    )


def _ble_service_block() -> str:
    return (
        f"\n    {BEGIN}\n"
        "    /* Desk Card service rides on the same BLE stack the vendor just\n"
        "     * enabled. Its ACKs are pumped from the UI task, not this one, so\n"
        "     * the desk link keeps working after provisioning exits. */\n"
        "    (void)desk_ble_register_service();\n"
        f"    {END}\n"
    )


def _insert_after(text: str, anchor: str, block: str, label: str) -> str:
    if text.count(anchor) != 1:
        raise ValueError(
            f"anchor for {label} appears {text.count(anchor)} times, expected 1"
        )
    index = text.index(anchor) + len(anchor)
    return text[:index] + block + text[index:]


def _hook_ble(text: str) -> str:
    if BEGIN in text:
        if END not in text:
            raise ValueError("BLE: found BEGIN marker without END; review manually")
        return text
    patched = _insert_after(text, BLE_INCLUDE_ANCHOR, _ble_include_block(),
                            "BLE include")
    patched = _insert_after(patched, BLE_WRITE_ANCHOR, _ble_write_block(),
                            "BLE write forwarding")
    patched = _insert_after(patched, BLE_SERVICE_ANCHOR, _ble_service_block(),
                            "BLE service registration")
    return patched


TARGETS = (
    (BLE_RELATIVE, _hook_ble),
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True,
                        help="OpenHarmony source root (contains vendor/)")
    parser.add_argument("--apply", action="store_true",
                        help="write the change; default is a read-only preview")
    args = parser.parse_args(argv)

    root = Path(args.root).resolve()
    results: list[tuple[Path, str, str]] = []
    for relative, hook in TARGETS:
        target = root / relative
        if not target.is_file():
            print(f"missing {target}", file=sys.stderr)
            return 2
        original = target.read_text(encoding="utf-8")
        try:
            updated = hook(original)
        except ValueError as exc:
            print(f"refusing to patch {relative}: {exc}", file=sys.stderr)
            return 3
        results.append((relative, original, updated))

    wrote = False
    for relative, original, updated in results:
        if updated == original:
            print(f"{relative}: already hooked.")
            continue
        diff = difflib.unified_diff(
            original.splitlines(keepends=True),
            updated.splitlines(keepends=True),
            fromfile=str(relative),
            tofile=str(relative) + " (proposed)",
        )
        sys.stdout.write("".join(diff))
        wrote = True
        if args.apply:
            (root / relative).write_text(updated, encoding="utf-8")

    if not wrote:
        return 0
    if not args.apply:
        print("\nPreview only. Re-run with --apply to write.")
        return 0
    print("\nTransport hook applied.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
