"""Offline tests of generated WS63 source overlay; no SDK nor hardware."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from tools.ws63_overlay import ROOT, stage, CORE_SOURCES, UI_SOURCES, gn_snippet


class Ws63OverlayTests(unittest.TestCase):
    def test_stages_all_sources_and_headers(self):
        with tempfile.TemporaryDirectory() as folder:
            target = Path(folder) / "overlay"
            info = stage(ROOT, target)
            self.assertEqual(info["sourceCount"], 12)
            self.assertEqual(info["headerCount"], 12)
            self.assertEqual(len(info["files"]), 24)
            self.assertEqual(len(set(item["staged"] for item in info["files"])), 24)
            for entry in info["files"]:
                staged = target / entry["staged"]
                original = ROOT / entry["source"]
                self.assertTrue(staged.exists())
                self.assertEqual(staged.read_bytes(), original.read_bytes())
                self.assertEqual(
                    entry["sha256"], hashlib.sha256(staged.read_bytes()).hexdigest()
                )
            parsed = json.loads((target / "manifest.json").read_text(encoding="utf-8"))
            self.assertEqual(parsed["format"], "XiaohongDeskCard-ws63-overlay-v1")
            self.assertFalse(parsed["upstreamSdkCompiled"])
            self.assertFalse(parsed["firmwareFlashValidated"])
            fragment = (target / "BUILD.gn.fragment").read_text(encoding="utf-8")
            self.assertIn('static_library("xiaohong")', fragment)
            self.assertIn('if (config_lvgl_enable)', fragment)
            for name in (*CORE_SOURCES, *UI_SOURCES):
                self.assertIn(f"src/deskcard/{name}", fragment)

    def test_is_repeatable_and_refuses_overwrite(self):
        with tempfile.TemporaryDirectory() as folder:
            first = Path(folder) / "first"
            second = Path(folder) / "second"
            info1 = stage(ROOT, first)
            info2 = stage(ROOT, second)
            self.assertEqual(info1, info2)
            self.assertEqual(gn_snippet(),
                             (first / "BUILD.gn.fragment").read_text(encoding="utf-8"))
            with self.assertRaises(FileExistsError):
                stage(ROOT, first)

    def test_missing_sources_produce_no_stage(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder) / "missing-source"
            root.mkdir()
            target = Path(folder) / "output"
            with self.assertRaises(FileNotFoundError):
                stage(root, target)
            self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
