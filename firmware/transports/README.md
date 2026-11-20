# Transport adapter contract (planned)

`desk_wire_encode_set`/`desk_wire_decode_set` already provide a binary application frame. BLE and SLE adapters must implement discovery, bonding/authorization, BEGIN/CHUNK/COMMIT reassembly, and ACK/notification; Wi-Fi HTTP must validate authenticated JSON and map to the same `desk_set_command_t`.

Adapters MUST NOT touch LVGL directly. Instead, validate transport-layer authorization, decode the request and queue it to the single Desk owner/UI task. The owner calls `desk_core_apply` and sends an ACK with the resulting `desk_result_t` and revision.

Use real SDK headers and version-pinned examples when implementing hardware adapters. No simulated `BLE connected` or `SLE supported` status may be presented as proof of hardware compatibility.
