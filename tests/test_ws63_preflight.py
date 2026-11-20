"""Offline preflight tests use tiny fake source-tree fixtures, not full OH SDK."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from tools.ws63_preflight import inspect, main, normalize_commit, REQUIRED, OPTIONAL


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


    def test_pinned_openharmony_commit_is_recorded(self):
        with tempfile.TemporaryDirectory() as directory:
            report = inspect(
                Path(directory), oh_commit="ad09f9721ee5ad09f9721ee5ad09f9721ee5ad09"
            )
            self.assertEqual(
                report["openHarmonyCommit"], "ad09f9721ee5ad09f9721ee5ad09f9721ee5ad09"
            )
            self.assertTrue(report["openHarmonyCommitPinned"])

    def test_unpinned_run_reports_no_commit_claim(self):
        with tempfile.TemporaryDirectory() as directory:
            report = inspect(Path(directory))
            self.assertIsNone(report["openHarmonyCommit"])
            self.assertFalse(report["openHarmonyCommitPinned"])

    def test_normalize_commit_lowercases_and_accepts_short_sha(self):
        self.assertEqual(normalize_commit("AD09F97"), "ad09f97")
        self.assertEqual(normalize_commit("ad09f9721ee5"), "ad09f9721ee5")

    def test_normalize_commit_rejects_non_sha_input(self):
        for bad in ("", "   ", "xyz1234", "ad09f9", "a" * 41, "ad09f9721ee5zz"):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    normalize_commit(bad)

    def test_cli_accepts_oh_commit_and_reports_it(self):
        with tempfile.TemporaryDirectory() as directory:
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                code = main(
                    ["--root", directory, "--json", "--oh-commit", "AD09F97"]
                )
            self.assertEqual(code, 2)  # fixture tree is intentionally incomplete
            report = json.loads(output.getvalue())
            self.assertEqual(report["openHarmonyCommit"], "ad09f97")
            self.assertTrue(report["openHarmonyCommitPinned"])

    def test_cli_rejects_invalid_oh_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            with contextlib.redirect_stdout(io.StringIO()):
                with contextlib.redirect_stderr(io.StringIO()):
                    with self.assertRaises(SystemExit) as caught:
                        main(["--root", directory, "--oh-commit", "not-a-commit"])
            self.assertEqual(caught.exception.code, 2)


if __name__ == "__main__":
    unittest.main()
