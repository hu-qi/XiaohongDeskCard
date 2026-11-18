#!/usr/bin/env python3
"""Local desk-card simulator using the compiled C99 Desk Core.

Wi-Fi endpoint = local HTTP. BLE and SLE endpoints = MOCK transport framing
only: NO Bluetooth, NearLink, WS63, ArkTS or LVGL hardware stack is exercised.
Never expose this developer-only HTTP server to an external network.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
from pathlib import Path
import struct
import sys
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any
import zlib


class Content(ctypes.Structure):
    _fields_ = [
        ("name", ctypes.c_char * 49),
        ("title", ctypes.c_char * 65),
        ("organization", ctypes.c_char * 97),
        ("theme", ctypes.c_char * 17),
    ]


def content_json(value: Content) -> dict[str, str]:
    return {name: bytes(getattr(value, name)).split(b"\0", 1)[0].decode("utf-8")
            for name in ("name", "title", "organization", "theme")}


def frame_for(command: dict[str, Any]) -> bytes:
    """Python reference encoder for C binary Desk Wire v1."""
    payload = bytearray()
    content = command["content"]
    for field, cap in (("name", 49), ("title", 65),
                       ("organization", 97), ("theme", 17)):
        value = content.get(field, "")
        if not isinstance(value, str):
            raise ValueError(field + " must be a string")
        value_bytes = value.encode("utf-8")
        if len(value_bytes) >= cap:
            raise ValueError(field + " exceeds max UTF-8 bytes")
        payload.append(len(value_bytes))
        payload.extend(value_bytes)
    return struct.pack("<BBBBIIHI", 0xD5, 1, 1, 0,
                       command["requestId"], command["expectedRevision"],
                       len(payload), zlib.crc32(payload)) + bytes(payload)


def parse_command(value: Any) -> dict[str, Any]:
    if not isinstance(value, dict) or not isinstance(value.get("content"), dict):
        raise ValueError("expected an object with content")
    for key in ("requestId", "expectedRevision"):
        number = value.get(key)
        if type(number) is not int or number < (1 if key == "requestId" else 0) or number > 0xFFFFFFFF:
            raise ValueError("invalid " + key)
    content = value["content"]
    result: dict[str, str] = {}
    for key in ("name", "title", "organization", "theme"):
        text = content.get(key, "")
        if not isinstance(text, str):
            raise ValueError(key + " must be a string")
        result[key] = text
    return {"requestId": value["requestId"],
            "expectedRevision": value["expectedRevision"], "content": result}


class DeskSimulator:
    def __init__(self, library_path: str, snapshot_path: str):
        self.lock = threading.Lock()
        self.lib = ctypes.CDLL(library_path)
        self.lib.desk_sim_init.argtypes = [ctypes.c_char_p]
        self.lib.desk_sim_init.restype = ctypes.c_int
        self.lib.desk_sim_snapshot.argtypes = [
            ctypes.POINTER(Content), ctypes.POINTER(Content), ctypes.POINTER(ctypes.c_uint32)
        ]
        self.lib.desk_sim_snapshot.restype = ctypes.c_int
        self.lib.desk_sim_apply_set.argtypes = [
            ctypes.c_uint32, ctypes.c_uint32, ctypes.c_char_p,
            ctypes.c_char_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.c_int
        ]
        self.lib.desk_sim_apply_set.restype = ctypes.c_int
        self.lib.desk_sim_apply_fragmented.argtypes = [
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t,
            ctypes.c_size_t, ctypes.c_int, ctypes.c_int
        ]
        self.lib.desk_sim_apply_fragmented.restype = ctypes.c_int
        self.lib.desk_sim_encode_ack.argtypes = [
            ctypes.c_uint32, ctypes.c_int,
            ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t
        ]
        self.lib.desk_sim_encode_ack.restype = ctypes.c_int
        self.lib.desk_sim_result_name.argtypes = [ctypes.c_int]
        self.lib.desk_sim_result_name.restype = ctypes.c_char_p
        if self.lib.desk_sim_init(os.fsencode(snapshot_path)) != 1:
            raise RuntimeError("unable to initialize desktop Desk Core")

    def snapshot(self) -> dict[str, Any]:
        with self.lock:
            return self._snapshot_locked()

    def _snapshot_locked(self) -> dict[str, Any]:
        configured, displayed, revision = Content(), Content(), ctypes.c_uint32()
        if self.lib.desk_sim_snapshot(ctypes.byref(configured),
                                      ctypes.byref(displayed),
                                      ctypes.byref(revision)) != 1:
            raise RuntimeError("snapshot failed")
        return {"revision": revision.value, "content": content_json(configured),
                "display": content_json(displayed), "device": "XiaohongDesk-MOCK",
                "hardwareValidated": False}

    def submit(self, transport: str, command: dict[str, Any], authorized: bool) -> dict[str, Any]:
        with self.lock:
            if transport == "wifi":
                c = command["content"]
                result = self.lib.desk_sim_apply_set(
                    command["requestId"], command["expectedRevision"],
                    *(c[key].encode("utf-8") for key in ("name", "title", "organization", "theme")),
                    int(authorized))
            elif transport in ("ble", "sle"):
                frame = frame_for(command)
                array = (ctypes.c_uint8 * len(frame)).from_buffer_copy(frame)
                # Simulate smaller BLE chunks, larger SLE chunks; NOT radio transport.
                fragment_bytes = 20 if transport == "ble" else 64
                result = self.lib.desk_sim_apply_fragmented(
                    array, len(frame), fragment_bytes,
                    1 if transport == "ble" else 2, int(authorized))
            else:
                raise ValueError("unknown mock transport")
            state = self._snapshot_locked()
            answer = {
                "result": self.lib.desk_sim_result_name(result).decode("ascii"),
                "transport": transport.upper() + ("_MOCK" if transport != "wifi" else "_HTTP"),
                "state": state,
            }
            if transport in ("ble", "sle"):
                ack_buffer = (ctypes.c_uint8 * 16)()
                if self.lib.desk_sim_encode_ack(command["requestId"], result,
                                                ack_buffer, 16) != 1:
                    raise RuntimeError("cannot encode business ACK")
                ack_bytes = bytes(ack_buffer)
                header = ack_bytes[:12]
                magic, version, code, reserved, req_id, rev = struct.unpack(
                    "<BBBBII", header)
                ack_crc = struct.unpack("<I", ack_bytes[12:])[0]
                if (magic, version, reserved) != (0xD6, 1, 0) or (
                    req_id != command["requestId"] or rev != state["revision"]
                    or ack_crc != zlib.crc32(header)
                ):
                    raise RuntimeError("C business ACK failed independent Python validation")
                answer["radioAck"] = {
                    "requestId": req_id,
                    "revision": rev,
                    "resultCode": code,
                    "wireHex": ack_bytes.hex()
                }
            return answer


def make_handler(simulator: DeskSimulator, token: str, html_path: Path):
    class Handler(BaseHTTPRequestHandler):
        def reply(self, code: int, value: Any) -> None:
            body = json.dumps(value, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", "application/json; charset=utf-8")
            self.send_header("Cache-Control", "no-store")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self) -> None:
            if self.path == "/api/v1/desk/status":
                self.reply(200, simulator.snapshot())
            elif self.path in ("/", "/index.html"):
                body = html_path.read_bytes()
                self.send_response(200)
                self.send_header("Content-Type", "text/html; charset=utf-8")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)
            else:
                self.reply(404, {"error": "not found"})

        def do_PUT(self) -> None:
            self.update()

        def do_POST(self) -> None:
            self.update()

        def update(self) -> None:
            paths = {"/api/v1/desk/content": "wifi",
                     "/sim/ble/write": "ble", "/sim/sle/write": "sle"}
            transport = paths.get(self.path)
            if transport is None or (
                transport == "wifi" and self.command != "PUT"
            ) or (transport != "wifi" and self.command != "POST"):
                self.reply(404, {"error": "not found"})
                return
            if self.headers.get("Authorization") != "Bearer " + token:
                self.reply(403, {"result": "UNAUTHORIZED"})
                return
            try:
                length = int(self.headers.get("Content-Length", "0"))
                if not 0 < length <= 2048:
                    raise ValueError("body size exceeds limit")
                data = json.loads(self.rfile.read(length).decode("utf-8"))
                command = parse_command(data)
                result = simulator.submit(transport, command, True)
            except (ValueError, UnicodeError, KeyError, json.JSONDecodeError) as exc:
                self.reply(400, {"result": "BAD_INPUT", "detail": str(exc)})
                return
            status = result["result"]
            http_code = 200 if status == "OK" else (
                409 if status in ("CONFLICT", "DUPLICATE_ID") else (
                    400 if status == "BAD_INPUT" else 500))
            self.reply(http_code, result)

        def log_message(self, format_string: str, *args: Any) -> None:
            # Developer-only; do not log tokens or the entire request.
            print("[desk-sim] " + format_string % args, file=sys.stderr)

    return Handler


def run() -> None:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--lib", default=os.environ.get(
        "DESK_SIM_LIB", str(root / "build" / (
            "libdesk_simulator.dylib" if sys.platform == "darwin" else "libdesk_simulator.so"))))
    parser.add_argument("--snapshot", default=str(root / "build" / "desk-sim-state.bin"))
    args = parser.parse_args()
    if not Path(args.lib).is_file():
        parser.error("shared library not found; run: cmake -S . -B build && cmake --build build")
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    Path(args.snapshot).parent.mkdir(parents=True, exist_ok=True)
    simulator = DeskSimulator(args.lib, args.snapshot)
    token = os.environ.get("DESK_SIM_TOKEN", "dev-only-token")
    if not token:
        parser.error("DESK_SIM_TOKEN cannot be empty")
    handler = make_handler(simulator, token, Path(__file__).with_name("index.html"))
    # The mock is intentionally loopback ONLY, without any --host flag.
    server = ThreadingHTTPServer(("127.0.0.1", args.port), handler)
    print(f"Desk simulator: http://127.0.0.1:{args.port}/ (loopback-only)")
    print("BLE and SLE transport choices are simulated; no physical radio is used.")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == "__main__":
    run()
