# Desk Wire v1 — transport-independent frame

The binary codec is implemented in `firmware/core/src/desk_wire.c` and covered by `tests/test_core.c`. This is a **host-tested protocol design**, not a claim of WS63 BLE/SLE interoperability.

## Set-content frame

| Byte offset | Size | Meaning |
|---|---:|---|
| 0 | 1 | magic = 0xD5 |
| 1 | 1 | version = 1 |
| 2 | 1 | opcode = 1 (set content) |
| 3 | 1 | reserved = 0 |
| 4 | 4 | requestId, little-endian |
| 8 | 4 | expectedRevision, little-endian |
| 12 | 2 | payload length, little-endian |
| 14 | 4 | payload CRC32, little-endian |
| 18 | N | four length-prefixed UTF-8 strings |

The payload contains four fields in order: name, title, organization, and theme. Each has one byte for length followed by raw bytes. NUL terminators are not sent. Valid theme values: `classic`, `modern`, `minimal`. Max complete frame size: 384 bytes.

CRC32 checks for unintentional corruption; **it provides no authentication or message integrity against attackers**. BLE/SLE transports must also implement authorization and pairing. Request ID and optimistic revision support retries and conflict handling.

## Reassembly state machine

1. BEGIN(frameLength, frameCrc32)
2. CHUNK(offset, byte[]), sequential offsets; exact duplicate chunks accepted
3. FINISH: full-frame CRC32, then decode and validate
4. Transport adapter queues the decoded command to a serialized Desk/UI task
5. Desk Core validates revision, applies save & render callbacks, responds with business ACK

The framing of BEGIN/CHUNK/FINISH into **GATT characteristics or SSAP attributes remains to be implemented**, and maximum fragment length must come from the real negotiated link parameters. The RX buffer is bounded (384 bytes) and has no heap allocation.

## HTTP JSON mapping

For Wi-Fi, the authenticated HTTP handler may parse JSON into `desk_set_command_t` and send it directly to the Desk task; it does not need to use the binary BLE/SLE representation.

## Known limitations

- Only `desk.setContent` has an implemented binary codec; status/ACK are forthcoming.
- Current retry de-duplication remembers only the last request in RAM; persisted cross-reboot anti-replay and full command audit need platform support.
- A saved content update may report render failure; repainting on the UI task can recover from that failure.
- Storage callbacks must implement power-loss-safe persistence; the core library doesn't implement flash storage.
- The protocol version is experimental and may change before any SDK-based device release.


## Business ACK v1 (implemented in C99, decoded in ArkTS source)

Do not treat a successful GATT characteristic write or SLE property write
as proof that the desk has displayed the requested content. The WS63 Desk
owner task shall **send a business ACK after calling the real Desk Core**,
using either GATT notification or SSAP property notification.

16-byte ACK packet (little-endian):

| Offset | Size | Field |
|---|---:|---|
| 0 | 1 | Magic `0xD6` |
| 1 | 1 | Version `0x01` |
| 2 | 1 | Desk Core result code: OK=0, BAD_INPUT=1, UNAUTHORIZED=2, CONFLICT=3, DUPLICATE_ID=4, STORE_ERROR=5, RENDER_ERROR=6, REVISION_EXHAUSTED=7 |
| 3 | 1 | Reserved `0x00` |
| 4 | 4 | Request ID (uint32 LE, nonzero) |
| 8 | 4 | Current revision (uint32 LE) |
| 12 | 4 | CRC32 of bytes [0..11] (uint32 LE) |

**Code paths**:
- C99: `desk_wire_encode_ack` / `desk_wire_decode_ack`.
- ArkTS: `DeskAckFrame.decode` / `DeskAckWaiter`.
- BLE: `BleGattLink` subscribes to `BLECharacteristicChange`.
- SLE: `SleSsapLink` subscribes to SSAP `propertyChange`.
- Simulator: C creates real ACK frames, Python independently checks request ID/revision/CRC and includes them in BLE/SLE mock responses.

ACK correlation uses requestId, not the transport connection ID. ACK timeout is **outcome unknown**: the device might have applied the operation but its notification was lost. A future fallback manager must first query device revision/content before switching transport. No blind cross-transport resend.

### Provisional BLE / SLE UUIDs

| Role | UUID |
|---|---|
| Desk service | `79b468bd-306c-58a8-9415-c9e9c993bdfa` |
| Desk command write | `44b4cc5b-14fe-54c2-bc82-e980802e2052` |
| Desk ACK notification | `e740d824-fa27-58c3-8bb9-085f74a31869` |
| Desk status (reserved) | `5f52d590-af6d-57c8-beca-4c48e4841ce3` |

Advertised test name: `XiaohongDesk`.

These UUIDs are project-defined v5 identifiers. **They are provisional**, not Bluetooth SIG assigned numbers. They must be reflected in the firmware GATT and SSAP server tables and checked for any NearLink UUID restrictions before declaring them stable.

### Pending radio transport details

- BLE command characteristic must support authenticated write-with-response.
- BLE ACK characteristic must support notify and contain a CCCD.
- SLE command property must support WRITE_WITH_RESPONSE.
- SLE ACK property must support NOTIFY.
- Device must enforce ownership/authorization. The service UUID alone is NOT a security boundary.
- Enable notifications **before** sending any BEGIN/CHUNK/COMMIT packet.
- Real SSAP/GATT MTUs and maximum property write lengths remain unverified.
- Radio callbacks must queue decoded commands to Desk Core's serialized owner task; never call LVGL from the radio callback.
