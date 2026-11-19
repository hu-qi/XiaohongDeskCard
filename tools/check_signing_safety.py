#!/usr/bin/env python3
"""Reject machine-local HarmonyOS debug signing credentials in shared Git.

Does not print secrets, private paths, or the protected config contents.
Run with --staged before commit; CI runs --tracked on checked-out sources.
This is a narrow signing-config guard, NOT a replacement for secret scanning.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys

RELATIVE = "apps/harmony/build-profile.json5"
REPO_ROOT = Path(__file__).resolve().parents[1]

# DevEco generated debug-signing fields must remain local. Even a .cer path
# should not enter the project config; credentials and user home are private.
SIGNING_KEYS = (
    "certpath", "keyAlias", "keyPassword", "profile",
    "signAlg", "storeFile", "storePassword",
)
SENSITIVE_FIELD = re.compile(
    r'(?i)["\'](?:' + "|".join(SIGNING_KEYS) + r')["\']\s*:'
)
NONEMPTY_CONFIGS = re.compile(
    r'["\']signingConfigs["\']\s*:\s*\[\s*(?=\S)(?!\])',
    re.IGNORECASE,
)
LOCAL_PATH = re.compile(
    r'(?i)(/Users/|/home/[^/"\s]+/\.ohos/|[A-Z]:/Users/)'
)


def is_safe_shared_config(source: str) -> bool:
    return not (
        SENSITIVE_FIELD.search(source)
        or NONEMPTY_CONFIGS.search(source)
        or LOCAL_PATH.search(re.sub(r"[\\/]+", "/", source))
    )


def staged_config() -> str:
    process = subprocess.run(
        ["git", "show", f":{RELATIVE}"],
        cwd=REPO_ROOT,
        capture_output=True,
        check=False,
    )
    if process.returncode != 0:
        raise RuntimeError("Unable to read the staged HarmonyOS build profile")
    return process.stdout.decode("utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    options = parser.add_mutually_exclusive_group(required=True)
    options.add_argument("--tracked", action="store_true",
                         help="Check the current checkout / GitHub CI")
    options.add_argument("--staged", action="store_true",
                         help="Check the staged version (pre-commit)")
    args = parser.parse_args()
    try:
        text = (REPO_ROOT / RELATIVE).read_text(encoding="utf-8") if args.tracked \
            else staged_config()
    except (OSError, UnicodeError, RuntimeError):
        print("FAIL: unable to inspect HarmonyOS build profile", file=sys.stderr)
        return 1
    if not is_safe_shared_config(text):
        print(
            "BLOCKED: apps/harmony/build-profile.json5 contains local signing "
            "material. Keep signingConfigs/signingConfig local to DevEco, "
            "and stage only non-secret files. No credential values are shown.",
            file=sys.stderr,
        )
        return 1
    print("PASS: no private signing material in shared HarmonyOS build profile")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
