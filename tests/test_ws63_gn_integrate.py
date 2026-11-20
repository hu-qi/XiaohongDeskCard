"""Gate B host tests: zero-impact preview and version-pinned GN installation.

A temporary vendor Git repo stands in for the unavailable Xiaohong SDK.
No board, OpenHarmony toolchain or real upstream checkout is modified.
"""
from __future__ import annotations

from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest

from tools.ws63_overlay import ROOT, CORE_SOURCES, UI_SOURCES, CORE_HEADERS, UI_HEADERS, stage
from tools.ws63_gn_integrate import (
    BUILD, DEST, apply, patch_build_gn, prepare
)


SAMPLE_GN = """import("//build/lite/config/component/lite_component.gni")
static_library("xiaohong") {
  sources = [
    "src/main.c",
  ]
  include_dirs += [
    "src",
  ]
  if (config_lvgl_enable) {
    defines += [ "SUPPORT_LVGL" ]
    sources += [
      "src/display/lvgl_task.c",
    ]
  }
}
"""


def git(where: Path, *args: str) -> str:
    result = subprocess.run(
        ["git", "-C", str(where), *args],
        capture_output=True, text=True, check=True
    )
    return result.stdout.strip()


class GNIntegrationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve() / "ohos"
        self.vendor = self.root / "vendor/atomgit/xiaohong"
        self.vendor.mkdir(parents=True)
        git(self.vendor, "init", "-q")
        git(self.vendor, "config", "user.email", "test@example.invalid")
        git(self.vendor, "config", "user.name", "XiaohongDeskCard Test")
        self.build = self.root / BUILD
        self.build.parent.mkdir(parents=True)
        self.build.write_text(SAMPLE_GN, encoding="utf-8")
        git(self.vendor, "add", "xiaohong/BUILD.gn")
        git(self.vendor, "commit", "-qm", "fixture upstream GN")
        self.sha = git(self.vendor, "rev-parse", "HEAD")
        self.overlay = Path(self.temp.name) / "overlay"
        stage(ROOT, self.overlay)
        self.dest = self.root / DEST

    def tearDown(self):
        self.temp.cleanup()

    def test_preview_is_read_only_and_anchors_lvgl_inside_target(self):
        args = (self.root, self.overlay, self.sha)
        _, build, dest, original, updated, files = prepare(*args)
        self.assertEqual(original, SAMPLE_GN)
        self.assertEqual(build, self.build)
        self.assertEqual(dest, self.dest)
        self.assertFalse(self.dest.exists())
        self.assertEqual(len(files), (len(CORE_SOURCES) + len(UI_SOURCES)) +
                         (len(CORE_HEADERS) + len(UI_HEADERS)))
        self.assertIn('# DESKCARD-GN-BEGIN CORE', updated)
        self.assertIn('# DESKCARD-GN-BEGIN LVGL', updated)
        self.assertIn('src/deskcard/desk_ws63_cmsis.c', updated)
        self.assertIn('src/deskcard/desk_http_json.c', updated)
        self.assertIn('src/deskcard/include', updated)
        self.assertLess(
            updated.index('# DESKCARD-GN-END LVGL'),
            updated.index('  }\n\n  # DESKCARD-GN-BEGIN CORE')
            if '  }\n\n  # DESKCARD-GN-BEGIN CORE' in updated else
            updated.index('# DESKCARD-GN-BEGIN CORE'),
        )
        self.assertEqual(self.build.read_text(encoding="utf-8"), SAMPLE_GN)

        cli = subprocess.run(
            [sys.executable, str(ROOT / "tools/ws63_gn_integrate.py"),
             "--root", str(self.root), "--overlay", str(self.overlay),
             "--expected-upstream-commit", self.sha],
            capture_output=True, text=True, check=True
        )
        self.assertIn("READ-ONLY PREVIEW", cli.stdout)
        self.assertIn("+", cli.stdout)
        self.assertFalse(self.dest.exists())

    def test_atomic_install_is_idempotent_and_sources_match_manifest(self):
        self.assertTrue(apply(self.root, self.overlay, self.sha))
        result = self.build.read_text(encoding="utf-8")
        self.assertEqual(result, patch_build_gn(SAMPLE_GN))
        manifest = json.loads((self.overlay / "manifest.json").read_text())
        for entry in manifest["files"]:
            relative = Path(entry["staged"]).relative_to("src/deskcard")
            self.assertEqual(
                (self.dest / relative).read_bytes(),
                (self.overlay / entry["staged"]).read_bytes()
            )
        self.assertFalse(apply(self.root, self.overlay, self.sha))
        self.assertEqual(result, self.build.read_text(encoding="utf-8"))

    def test_mismatched_vendor_sha_is_fatal_and_does_not_modify_checkout(self):
        with self.assertRaisesRegex(ValueError, "commit mismatch"):
            apply(self.root, self.overlay, "0" * 40)
        self.assertEqual(self.build.read_text(encoding="utf-8"), SAMPLE_GN)
        self.assertFalse(self.dest.exists())
        with self.assertRaisesRegex(ValueError, "Full 40-character"):
            prepare(self.root, self.overlay, "main")

    def test_dirty_vendor_build_gn_is_refused_before_copy(self):
        self.build.write_text(SAMPLE_GN + "\n# local developer changes\n")
        with self.assertRaisesRegex(ValueError, "dirty"):
            apply(self.root, self.overlay, self.sha)
        self.assertFalse(self.dest.exists())
        self.assertIn("local developer changes", self.build.read_text())

    def test_corrupted_overlay_does_not_touch_vendor(self):
        source = self.overlay / "src/deskcard/desk_core.c"
        source.write_text("tampered", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "SHA256 mismatch"):
            apply(self.root, self.overlay, self.sha)
        self.assertFalse(self.dest.exists())
        self.assertEqual(self.build.read_text(), SAMPLE_GN)

    def test_missing_or_ambiguous_gn_scopes_refused(self):
        examples = (
            SAMPLE_GN.replace('static_library("xiaohong")',
                              'static_library("unrelated")'),
            SAMPLE_GN.replace("if (config_lvgl_enable)", "if (false)"),
            SAMPLE_GN + SAMPLE_GN,
            SAMPLE_GN.replace("include_dirs += [", "other_dirs += ["),
            SAMPLE_GN.replace('sources = [', 'other_sources = [')
                     .replace('sources += [', 'more_sources += ['),
            SAMPLE_GN.replace("  }\n}", "  }\n")  # unmatched outer block
        )
        for example in examples:
            with self.subTest(sample=example[:45]):
                with self.assertRaises(ValueError):
                    patch_build_gn(example)
        with self.assertRaisesRegex(ValueError, "Partial/ambiguous"):
            patch_build_gn(SAMPLE_GN + "\n# DESKCARD-GN-BEGIN CORE\n")

    def test_missing_output_without_gn_changes_is_not_mistaken_for_installed(self):
        self.assertTrue(apply(self.root, self.overlay, self.sha))
        for item in self.dest.iterdir():
            if item.is_file():
                item.unlink()
        with self.assertRaisesRegex(ValueError, "differs"):
            prepare(self.root, self.overlay, self.sha)

    def test_gn_lexer_ignores_braces_in_comments_or_strings(self):
        text = SAMPLE_GN.replace(
            '"src/main.c",',
            '"src/main.c", # ignored } {\n    "a{nested}.txt",'
        )
        patched = patch_build_gn(text)
        self.assertIn('# DESKCARD-GN-BEGIN LVGL', patched)
        self.assertTrue(patched.rstrip().endswith("}"))


if __name__ == "__main__":
    unittest.main()
