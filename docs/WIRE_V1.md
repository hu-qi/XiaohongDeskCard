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
