# WS63 integration

Status: **compiles into the upstream firmware and links into the final image.**
The last verified build (upstream `25ca698`, `FW_VERSION=0x090900` = 9.9.0)
produced `ws63-liteos-app_all.fwpkg` with 74 `desk_` symbols in
`ws63-liteos-app.elf`, including every transport entry point. Flashing and
on-device behaviour are still unverified — that is the operator's step.

## Files

| File | Role |
| --- | --- |
| `desk_ws63_glue.c/h` | Binds Desk Core to the vendor LvglTask and owns the dispatcher port. |
| `desk_ws63_cmsis.c/h` | CMSIS-RTOS2 display-event adapter. |
| `desk_ws63_board.c/h` | NV HAL over `uapi_nv_*`, callback wiring, transport ownership, worker startup. |
| `desk_ws63_transport.c/h` | Channel-agnostic BLE/SLE/HTTP ingress, generations, reply mailbox. |
| `desk_ws63_adapters.c/h` | The only SDK-facing file: GATT service, SSAP service, HTTP routes. |
| `desk_ws63_http.c/h` | Station-mode lwIP listener (the vendor server is AP-only and not compiled). |

## Why the board binding exists

`xiaohong` is listed in the SDK's `ram_component`, so `build_component.cmake`
links `libxiaohong.a` with normal archive semantics rather than
`--whole-archive`. Nothing in the vendor tree called `desk_*`, so the linker
dropped every deskcard object. `desk_board_init()` / `desk_board_poll()` called
from `lvgl_task.c` create the reference that keeps them.

## Transport wiring

- **BLE** — `desk_ble_register_service()` builds the Desk Card GATT service on
  its own server id after `enable_ble()` completes. `gatts_register_callbacks()`
  is global and the vendor already installed it, so the vendor's write callback
  forwards handles it does not own to `desk_ble_on_write()`.
- **SLE** — the upstream tree had no SLE code at all. `desk_sle_start()` installs
  the announce/connection/SSAP callbacks and enables the stack;
  `desk_sle_register_service()` then builds the SSAP server, service,
  write-only command property, notify-only ACK property and CCC descriptor.
- **Wi-Fi** — `desk_ws63_http.c` listens on `wlan0:80` for
  `/api/v1/desk/status` (GET) and `/api/v1/desk/content` (PUT). It waits for the
  station to obtain an IP, so it survives association order and reconnects.

ACKs are pumped from the UI task (`desk_board_poll`), not from a radio worker:
the vendor only runs its BLE provisioning task when the device has no Wi-Fi
config, so it is not a reliable pump.

SLE and the HTTP listener start from `desk_board_init()`. The BLE GATT service
cannot: `gatts_register_server()` requires `enable_ble()` to have completed, so
it is registered from the vendor's own BLE-enable callback.

## Applying the vendor hooks

```sh
tools/ws63_board_hook.py   --root <oh-root>          # preview
tools/ws63_board_hook.py   --root <oh-root> --apply  # writes lvgl_task.c
tools/ws63_transport_hook.py --root <oh-root>          # preview
tools/ws63_transport_hook.py --root <oh-root> --apply  # writes net_ble_prov.c
```

Both are review-first, idempotent and marker-guarded, and refuse to patch a tree
whose anchors are missing or ambiguous.

The GN side additionally needs the SLE include directory, which the vendor
`BUILD.gn` did not have:

```
"${ws63_sdk_path}/include/middleware/services/bts/sle",
```

## Still unverified

- No real-device flash or radio test has been run.
- BLE/SLE coexistence on this board is assumed from the SDK's own
  `ohos_connect` sample, which enables both, but has not been measured here.
- The HTTP bearer token is provisioned through `desk_transport_set_http_token()`;
  the BLE/SLE ownership handshake that should supply it is not implemented yet.
