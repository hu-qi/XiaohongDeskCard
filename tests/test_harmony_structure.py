"""Structural checks only. Does not compile ArkTS or build a HAP."""
import json
import re
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
        entry_ability = module["abilities"][0]
        self.assertEqual(entry_ability["startWindowIcon"], "$media:app_icon")
        self.assertEqual(
            entry_ability["startWindowBackground"],
            "$color:start_window_background",
        )
        with (ROOT / "entry/src/main/resources/base/element/color.json").open(
            encoding="utf-8"
        ) as file:
            colors = json.load(file)["color"]
        self.assertIn(
            {"name": "start_window_background", "value": "#FFFFFF"}, colors
        )
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
            "BleGattLink.ets", "SleSsapLink.ets", "RadioDeskTransport.ets",
            "DeskAckFrame.ets", "DeskAckWaiter.ets", "DeskServiceIds.ets",
            "DeskDeviceDiscovery.ets", "DeskPermissions.ets"
        ):
            with self.subTest(file=name):
                self.assertTrue((desk / name).exists())
        page = (ROOT / "entry/src/main/ets/pages/Index.ets").read_text(encoding="utf-8")
        # The page must drive real radios, not the ArkTS mock.
        self.assertIn("RadioDeskTransport", page)
        self.assertIn("WifiHttpTransport", page)
        self.assertNotIn("MockDeskTransport", page)
        self.assertNotIn("Mock'", page)
        self.assertIn("isNearLinkAvailable", page)
        self.assertIn("DeskPermissions.request", page)
        self.assertIn("new DeskDeviceDiscovery", page)
        self.assertIn("扫描星闪", page)
        self.assertIn("(_error: Error) => {}", page)
        self.assertNotIn("(_error) => {}", page)

    def test_radio_transport_waits_for_the_device_ack(self):
        radio = (ROOT / "entry/src/main/ets/desk/RadioDeskTransport.ets").read_text(
            encoding="utf-8")
        # A radio write is not proof of application: the ACK is mandatory.
        self.assertIn("sendFrameAwaitAck", radio)
        self.assertIn("DeskWireFrame.encodeSet", radio)
        self.assertIn("DeskServiceIds.SERVICE", radio)
        self.assertIn("DeskChannel.BLE", radio)
        self.assertIn("DeskChannel.WIFI", radio)  # rejected: radio only
        self.assertIn("new SleSsapLink", radio)
        # BLE writes must respect the default-MTU payload, not the MTU.
        self.assertIn("BLE_WRITE_BYTES", radio)

    def test_api_generation(self):
        nearlink = (ROOT / "entry/src/main/ets/desk/NearLinkCapability.ets").read_text(
            encoding="utf-8")
        self.assertIn("@kit.NearLinkKit", nearlink)
        self.assertNotIn("import { manager } from '@kit.ConnectivityKit'", nearlink)
        ble = (ROOT / "entry/src/main/ets/desk/BleGattLink.ets").read_text(
            encoding="utf-8")
        self.assertIn("ble.createGattClientDevice", ble)
        sle = (ROOT / "entry/src/main/ets/desk/SleSsapLink.ets").read_text(
            encoding="utf-8")
        self.assertIn("ssap.createClient", sle)
        self.assertIn("setPropertyNotification", sle)
        self.assertIn("sendFrameAwaitAck", sle)
        self.assertIn("setCharacteristicChangeNotification", ble)
        discovery = (ROOT / "entry/src/main/ets/desk/DeskDeviceDiscovery.ets").read_text(
            encoding="utf-8"
        )
        self.assertIn("throw new Error(String(err))", discovery)
        self.assertNotIn("throw err;", discovery)
        self.assertIn("sendFrameAwaitAck", ble)

    def test_shared_profile_leaves_signing_to_local_ide(self):
        """Shared config keeps an empty signing slot and no product reference."""
        with (ROOT / "build-profile.json5").open(encoding="utf-8") as file:
            profile = json.load(file)
        app = profile["app"]
        self.assertEqual(app["signingConfigs"], [])
        products = {product["name"]: product for product in app["products"]}
        self.assertNotIn("signingConfig", products["default"])

    def test_nearlink_probe_waits_for_system_capability(self):
        nearlink = (ROOT / "entry/src/main/ets/desk/NearLinkCapability.ets").read_text(
            encoding="utf-8")
        self.assertIn("SystemCapability.Communication.NearLink.Base", nearlink)
        self.assertIn("canIUse(", nearlink)
        self.assertLess(
            nearlink.index("canIUse("), nearlink.index("manager.isNearLinkSupported()")
        )

    def test_handoff_carries_gate_evidence_tables(self):
        handoff = (ROOT.parent.parent / "docs/HAP_BUILD_HANDOFF.md").read_text(
            encoding="utf-8")
        for heading in ("Gate A 回传记录表", "Gate B 回传记录表", "脱敏禁止清单"):
            with self.subTest(heading=heading):
                self.assertIn(heading, handoff)
        for field in ("DevEco Studio", "hvigor", "ohpm", "Git HEAD", "SHA-256",
                      "--oh-commit", "Automatic signing"):
            with self.subTest(field=field):
                self.assertIn(field, handoff)
        for forbidden in ("keystore", "私钥", "设备号", "Token"):
            with self.subTest(forbidden=forbidden):
                self.assertIn(forbidden, handoff)

    def test_ack_and_service_ids(self):
        desk = ROOT / "entry/src/main/ets/desk"
        ack = (desk / "DeskAckFrame.ets").read_text(encoding="utf-8")
        self.assertIn("bytes[0] !== 0xD6", ack)
        self.assertIn("DeskWireFrame.crc32", ack)
        ids = (desk / "DeskServiceIds.ets").read_text(encoding="utf-8")
        uuids = re.findall(
            r"[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}", ids
        )
        self.assertEqual(len(uuids), 4)
        self.assertEqual(len(set(uuids)), 4)
        protocol = (ROOT.parent.parent / "docs/WIRE_V1.md").read_text(encoding="utf-8")
        for uid in uuids:
            self.assertIn(uid, protocol)
        hardware_gate = ROOT.parent.parent / "docs/HARDWARE_GATE.md"
        self.assertIn("HarmonyOS 7", hardware_gate.read_text(encoding="utf-8"))

if __name__ == "__main__":
    unittest.main()
