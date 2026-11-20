"""Offline SDK fingerprint tests with invented source and secrets."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest

from tools.ws63_sdk_probe import inspect
from tools.ws63_overlay import ROOT


class SDKProbeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.vendor = self.root / "vendor/atomgit/xiaohong"
        self.base = self.vendor / "xiaohong"
        self.base.mkdir(parents=True)
        subprocess.run(["git", "init", "-q", str(self.vendor)],
                       check=True, capture_output=True)
        subprocess.run(["git", "-C", str(self.vendor), "config",
                        "user.email", "fixture@example.invalid"], check=True)
        subprocess.run(["git", "-C", str(self.vendor), "config",
                        "user.name", "Test"], check=True)
        gn = self.base / "BUILD.gn"
        gn.write_text(
            'static_library("xiaohong") { sources = []'
            ' include_dirs = [] if (config_lvgl_enable) { } }\n'
            '# synthetic-secret-do-not-report\n', encoding="utf-8"
        )
        d = self.base / "src/display"
        d.mkdir(parents=True)
        (d / "lvgl_task.c").write_text(
            "g_disp_event_qid osMessageQueueNew lv_timer_handler\n"
            "// private-fixture-credential\n", encoding="utf-8"
        )
        (d / "disp_driver.h").write_text(
            "typedef enum { eDisp_Max } disp_event_t;\n", encoding="utf-8"
        )
        network = self.base / "src/network"
        network.mkdir(parents=True)
        (network / "net_wifi_task.c").write_text(
            "void wifi_init(void);\n", encoding="utf-8"
        )
        (network / "net_http_server.c").write_text(
            "int http_server(void);\n", encoding="utf-8"
        )
        (network / "wifi_password_private.c").write_text(
            "FAKE_SECRET=must-not-report\n", encoding="utf-8"
        )
        (network / "ble_service.c").write_text(
            "void gatt_init(void);\n", encoding="utf-8"
        )
        lvgl = self.root / "third_party/lvgl/src/lv_version.h"
        lvgl.parent.mkdir(parents=True)
        lvgl.write_text(
            "#define LVGL_VERSION_MAJOR 9\n"
            "#define LVGL_VERSION_MINOR 3\n"
            "#define LVGL_VERSION_PATCH 0\n", encoding="utf-8"
        )
        subprocess.run(["git", "-C", str(self.vendor), "add", "."],
                       check=True, capture_output=True)
        subprocess.run(
            ["git", "-C", str(self.vendor), "commit", "-qm", "synthetic fixture"],
            check=True, capture_output=True
        )

    def tearDown(self):
        self.temp.cleanup()

    def test_identifiers_hash_and_version_without_secrets(self):
        result = inspect(self.root)
        self.assertTrue(result["vendorTreePresent"])
        self.assertRegex(result["vendorCommit"], r"^[0-9a-f]{40}$")
        self.assertTrue(result["sourceFiles"]["gn"]["exists"])
        self.assertTrue(result["sourceFiles"]["gn"]["identifiers"][
            'static_library("xiaohong")'
        ])
        self.assertTrue(result["sourceFiles"]["lvgl_task"]["identifiers"][
            "osMessageQueueNew"
        ])
        self.assertEqual(result["lvglVersion"]["LVGL_VERSION_MAJOR"], 9)
        self.assertEqual(result["lvglVersion"]["LVGL_VERSION_MINOR"], 3)
        self.assertIn("ble_service.c", result["candidateSourceNames"]["network"])
        encoded = json.dumps(result, ensure_ascii=False)
        self.assertNotIn("synthetic-secret", encoded)
        self.assertNotIn("private-fixture-credential", encoded)
        self.assertNotIn("must-not-report", encoded)
        self.assertNotIn("wifi_password_private.c", encoded)
        self.assertNotIn(str(self.root), encoded)
        self.assertFalse(result["boardSdkCompiled"])
        self.assertFalse(result["boardFirmwareFlashed"])
        self.assertFalse(result["phoneDeviceTested"])

    def test_missing_sdk_does_not_claim_built_or_connected(self):
        with tempfile.TemporaryDirectory() as tmp:
            result = inspect(Path(tmp))
            self.assertFalse(result["vendorTreePresent"])
            self.assertIsNone(result["vendorCommit"])
            self.assertFalse(result["sourceFiles"]["gn"]["exists"])
            self.assertFalse(result["boardSdkCompiled"])

    def test_cli_writes_once_only_without_echoing_source(self):
        output = self.root / "report.json"
        command = [
            sys.executable, str(ROOT / "tools/ws63_sdk_probe.py"),
            "--root", str(self.root), "--output", str(output)
        ]
        first = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertTrue(output.is_file())
        report = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(report["format"], "XiaohongDeskCard-sdk-probe-v1")
        before = hashlib.sha256(output.read_bytes()).hexdigest()
        second = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(second.returncode, 2)
        self.assertIn("already exists", second.stderr)
        self.assertEqual(before, hashlib.sha256(output.read_bytes()).hexdigest())
        self.assertNotIn("synthetic-secret", second.stderr)


if __name__ == "__main__":
    unittest.main()
