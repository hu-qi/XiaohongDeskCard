# WS63 integration (not yet connected)

This folder intentionally contains no invented hardware SDK bindings.

Integration checklist with the upstream xiaohong vendor project:

1. Build a known-good unmodified firmware image and record the upstream commit.
2. Add `desk_core.c` and `desk_wire.c` to the upstream `xiaohong/BUILD.gn`.
3. Initialize `desk_core_t` after platform storage and UI task initialization.
4. Implement `load/save` using actual vendor NV storage APIs. Flash writes must be atomic or use two slots + CRC for power-loss safety.
5. Implement `render` using the installed LVGL version. Keep all LVGL operations on the UI task and enqueue updates from wireless callbacks.
6. Integrate each transport separately: BLE GATT server, SLE SSAP server, and Wi-Fi local HTTP endpoint. Authorization must be checked before calling `desk_core_apply()`.
7. Add BEGIN/CHUNK/COMMIT framing in BLE/SLE transports using `desk_wire_rx_*`; reset the RX state on disconnect/timeout.
8. Test radio coexistence separately; do not assume simultaneous BLE/SLE/Wi-Fi operation is supported.

Official sample locations to inspect:

- `vendor/atomgit/xiaohong/xiaohong/src/display/lvgl_task.c`
- `vendor/atomgit/xiaohong/xiaohong/src/network/net_wifi_task.c`
- `vendor/atomgit/xiaohong/samples/13_sle_uart/`

**No WS63 build or device flash has been executed by this repository yet.**
