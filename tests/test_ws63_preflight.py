"""Offline preflight tests use tiny fake source-tree fixtures, not full OH SDK."""
from pathlib import Path
import tempfile
import unittest

from tools.ws63_preflight import inspect, REQUIRED, OPTIONAL


class Ws63PreflightTests(unittest.TestCase):
    def test_missing_source_never_claims_compile(self):
        with tempfile.TemporaryDirectory() as directory:
            result = inspect(Path(directory))
            self.assertFalse(result["allRequiredPresent"])
            self.assertEqual(len(result["missingRequired"]), len(REQUIRED))
            self.assertFalse(result["sdkCompiled"])
            self.assertFalse(result["hardwareValidated"])

    def test_expected_source_paths_and_anchors(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for key, rel in REQUIRED.items():
                file = root / rel
                file.parent.mkdir(parents=True, exist_ok=True)
                if key == "lvgl_task":
                    file.write_text("lv_timer_handler(); g_disp_event_qid;", encoding="utf-8")
                elif key == "disp_driver":
                    file.write_text("enum {eDisp_Max};", encoding="utf-8")
                elif key == "vendor_build":
                    file.write_text('static_library("xiaohong") {}', encoding="utf-8")
                else:
                    file.write_text("wifi_task", encoding="utf-8")
            report = inspect(root)
            self.assertTrue(report["allRequiredPresent"])
            self.assertFalse(report["missingRequired"])
            self.assertTrue(all(
                all(item.get("anchors", {}).values()) for item in report["checks"].values()
            ))
            self.assertFalse(report["sdkCompiled"])

    def test_optional_files_do_not_block_preflight(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for rel in REQUIRED.values():
                file = root / rel
                file.parent.mkdir(parents=True, exist_ok=True)
                file.write_text("placeholder", encoding="utf-8")
            report = inspect(root)
            self.assertTrue(report["allRequiredPresent"])
            self.assertFalse(any(report["checks"][key]["exists"] for key in OPTIONAL))


if __name__ == "__main__":
    unittest.main()
