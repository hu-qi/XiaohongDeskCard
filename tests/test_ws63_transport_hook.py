from __future__ import annotations

import unittest

from tools.ws63_transport_hook import (
    BEGIN,
    BLE_SERVICE_ANCHOR,
    BLE_WRITE_ANCHOR,
    END,
    _hook_ble,
)

BLE_SAMPLE = (
    "#include <stdbool.h>\n"
    '#include "net_ble_prov.h"\n'
    "\n"
    "static void ble_prov_write_req_cbk(uint8_t server_id, uint16_t conn_id,\n"
    "    gatts_req_write_cb_t *write_cb_para, errcode_t status)\n"
    "{\n"
    "    (void)server_id;\n"
    "    (void)conn_id;\n"
    "    (void)status;\n"
    "    if ((write_cb_para != NULL) && (write_cb_para->handle == g_rx_handle)) {\n"
    "        ble_prov_on_rx_frame(write_cb_para->value, write_cb_para->length);\n"
    "    }\n"
    "}\n"
    "\n"
    "static void ble_prov_ble_enable_cbk(errcode_t status)\n"
    "{\n"
    "    if (ble_prov_add_service() == ERRCODE_BT_SUCCESS) {\n"
    "        (void)ble_prov_start_adv();\n"
    "    }\n"
    "}\n"
    "\n"
    "static void BleProvTask(void)\n"
    "{\n"
    "    while (1) {\n"
    "        if (osMessageQueueGet(g_prov_qid, &evt, NULL, BLE_PROV_LOOP_TICK_MS / 10) == osOK) {\n"
    "            break;\n"
    "        }\n"
    "    }\n"
    "}\n"
)

class BleHookTests(unittest.TestCase):
    def test_inserts_three_marked_blocks(self):
        patched = _hook_ble(BLE_SAMPLE)
        self.assertEqual(patched.count(BEGIN), 3)
        self.assertEqual(patched.count(END), 3)
        self.assertIn('#include "desk_ws63_adapters.h"', patched)
        self.assertIn("desk_ble_on_write(", patched)
        self.assertIn("desk_ble_register_service()", patched)

    def test_vendor_write_path_is_preserved(self):
        patched = _hook_ble(BLE_SAMPLE)
        # The vendor's own provisioning frame handler must still run.
        self.assertIn("ble_prov_on_rx_frame(write_cb_para->value, "
                      "write_cb_para->length);", patched)

    def test_forwarding_runs_after_the_vendor_branch(self):
        patched = _hook_ble(BLE_SAMPLE)
        self.assertLess(patched.index("ble_prov_on_rx_frame"),
                        patched.index("desk_ble_on_write("))

    def test_service_registration_follows_provisioning_service(self):
        patched = _hook_ble(BLE_SAMPLE)
        self.assertLess(patched.index("ble_prov_add_service()"),
                        patched.index("desk_ble_register_service()"))

    def test_no_radio_work_is_added_to_the_provisioning_loop(self):
        # The provisioning task only runs when the device has no Wi-Fi config.
        # Anything that must work on a normal device belongs in the UI task.
        patched = _hook_ble(BLE_SAMPLE)
        loop = patched[patched.index("static void BleProvTask"):]
        self.assertNotIn("desk_", loop)

    def test_idempotent(self):
        once = _hook_ble(BLE_SAMPLE)
        self.assertEqual(_hook_ble(once), once)

    def test_refuses_missing_anchor(self):
        with self.assertRaises(ValueError):
            _hook_ble(BLE_SAMPLE.replace(BLE_WRITE_ANCHOR, ""))

    def test_refuses_ambiguous_anchor(self):
        doubled = BLE_SAMPLE + BLE_SERVICE_ANCHOR
        with self.assertRaises(ValueError):
            _hook_ble(doubled)

    def test_refuses_dangling_marker(self):
        with self.assertRaises(ValueError):
            _hook_ble(BLE_SAMPLE + "\n" + BEGIN + "\n")


if __name__ == "__main__":
    unittest.main()
