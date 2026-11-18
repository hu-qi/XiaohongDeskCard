"""Structural checks only. Does not compile ArkTS or build a HAP."""
import json
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1] / "apps" / "harmony"


class HarmonyStructureTests(unittest.TestCase):
    def test_stage_config(self):
        paths = [
            ROOT / "AppScope" / "app.json5",
            ROOT / "build-profile.json5",
            ROOT / "oh-package.json5",
            ROOT / "hvigor" / "hvigor-config.json5",
            ROOT / "entry" / "build-profile.json5",
            ROOT / "entry" / "oh-package.json5",
            ROOT / "entry" / "src" / "main" / "module.json5",
        ]
        for path in paths:
            with self.subTest(path=path):
                self.assertTrue(path.is_file())
                with path.open(encoding="utf-8") as file:
                    json.load(file)  # files are intentionally also strict JSON
        with (ROOT / "entry/src/main/module.json5").open(encoding="utf-8") as file:
            module = json.load(file)["module"]
        self.assertEqual(module["mainElement"], "EntryAbility")
        self.assertEqual(module["type"], "entry")
        permissions = {p["name"] for p in module["requestPermissions"]}
        self.assertIn("ohos.permission.INTERNET", permissions)
        self.assertIn("ohos.permission.ACCESS_BLUETOOTH", permissions)
        self.assertIn("ohos.permission.ACCESS_NEARLINK", permissions)

    def test_pages_resources(self):
        with (ROOT / "entry/src/main/resources/base/profile/main_pages.json").open(
            encoding="utf-8"
        ) as file:
            pages = json.load(file)["src"]
        self.assertIn("pages/Index", pages)
        self.assertTrue((ROOT / "entry/src/main/ets/pages/Index.ets").exists())
        self.assertTrue((ROOT / "entry/src/main/ets/entryability/EntryAbility.ets").exists())
        ET.parse(ROOT / "AppScope/resources/base/media/app_icon.svg")
        ET.parse(ROOT / "entry/src/main/resources/base/media/app_icon.svg")

    def test_transport_separation(self):
        desk = ROOT / "entry/src/main/ets/desk"
        for name in (
            "DeskModels.ets", "MockDeskTransport.ets", "WifiHttpTransport.ets",
            "NearLinkCapability.ets", "DeskWireFrame.ets",
            "BleGattLink.ets", "SleSsapLink.ets"
        ):
            with self.subTest(file=name):
                self.assertTrue((desk / name).exists())
        page = (ROOT / "entry/src/main/ets/pages/Index.ets").read_text(encoding="utf-8")
        self.assertIn("MockDeskTransport", page)
        self.assertIn("WifiHttpTransport", page)
        self.assertIn("BLE Mock", page)
        self.assertIn("SLE Mock", page)
        self.assertIn("isNearLinkAvailable", page)

    def test_api_generation(self):
        nearlink = (ROOT / "entry/src/main/ets/desk/NearLinkCapability.ets").read_text(
            encoding="utf-8")
        self.assertIn("@kit.NearLinkKit", nearlink)
        self.assertNotIn("@kit.ConnectivityKit", nearlink)
        ble = (ROOT / "entry/src/main/ets/desk/BleGattLink.ets").read_text(
            encoding="utf-8")
        self.assertIn("ble.createGattClientDevice", ble)
        sle = (ROOT / "entry/src/main/ets/desk/SleSsapLink.ets").read_text(
            encoding="utf-8")
        self.assertIn("ssap.createClient", sle)

if __name__ == "__main__":
    unittest.main()
