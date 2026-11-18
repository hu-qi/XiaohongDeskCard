"""Black-box HTTP tests against the actual compiled C99 core through ctypes."""
import json
import os
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from http.server import ThreadingHTTPServer
from urllib.error import HTTPError
from urllib.request import Request, urlopen

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools" / "simulator"))
from desk_sim_server import DeskSimulator, make_handler  # noqa: E402


class SimulatorTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        root = Path(__file__).resolve().parents[1]
        lib = os.environ.get("DESK_SIM_LIB", str(root / "build" / "libdesk_simulator.so"))
        cls.snapshot_file = str(Path(cls.temp.name) / "state.bin")
        cls.sim = DeskSimulator(lib, cls.snapshot_file)
        html = root / "tools" / "simulator" / "index.html"
        cls.server = ThreadingHTTPServer(
            ("127.0.0.1", 0), make_handler(cls.sim, "test-secret", html))
        cls.address = f"http://127.0.0.1:{cls.server.server_port}"
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.next_id = 10000

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)
        cls.temp.cleanup()

    def request(self, route, method="GET", data=None, token="test-secret"):
        headers = {}
        if token is not None:
            headers["Authorization"] = "Bearer " + token
        if data is not None:
            headers["Content-Type"] = "application/json; charset=utf-8"
        req = Request(self.address + route, method=method,
                      data=None if data is None else json.dumps(
                          data, ensure_ascii=False).encode("utf-8"), headers=headers)
        try:
            with urlopen(req, timeout=5) as response:
                return response.status, json.loads(response.read().decode("utf-8"))
        except HTTPError as error:
            return error.code, json.loads(error.read().decode("utf-8"))

    def payload(self, expected_revision=None, name="张三"):
        type(self).next_id += 1
        return {
            "requestId": type(self).next_id,
            "expectedRevision": self.sim.snapshot()["revision"]
            if expected_revision is None else expected_revision,
            "content": {
                "name": name,
                "title": "开发者",
                "organization": "OpenHarmony 社区",
                "theme": "classic"
            }
        }

    def test_01_wifi_http(self):
        before = self.sim.snapshot()["revision"]
        status, body = self.request("/api/v1/desk/content", "PUT", self.payload())
        self.assertEqual(status, 200)
        self.assertEqual(body["result"], "OK")
        self.assertEqual(body["transport"], "WIFI_HTTP")
        self.assertEqual(body["state"]["revision"], before + 1)
        self.assertEqual(body["state"]["display"]["name"], "张三")

    def test_02_mock_ble_and_sle_use_real_c_binary_decoder(self):
        for transport in ("ble", "sle"):
            with self.subTest(transport=transport):
                before = self.sim.snapshot()["revision"]
                status, body = self.request(
                    f"/sim/{transport}/write", "POST", self.payload(name="李四"))
                self.assertEqual(status, 200)
                self.assertEqual(body["result"], "OK")
                self.assertEqual(body["state"]["revision"], before + 1)
                self.assertEqual(body["state"]["display"]["name"], "李四")
                self.assertEqual(body["transport"], transport.upper() + "_MOCK")

    def test_03_unauthorized_rejected(self):
        previous = self.sim.snapshot()
        status, body = self.request("/sim/sle/write", "POST", self.payload(), token=None)
        self.assertEqual(status, 403)
        self.assertEqual(body["result"], "UNAUTHORIZED")
        self.assertEqual(self.sim.snapshot()["revision"], previous["revision"])

    def test_04_revision_conflict_rejected(self):
        previous = self.sim.snapshot()["revision"]
        stale = previous - 1 if previous > 0 else 123
        status, body = self.request(
            "/api/v1/desk/content", "PUT", self.payload(expected_revision=stale))
        self.assertEqual(status, 409)
        self.assertEqual(body["result"], "CONFLICT")
        self.assertEqual(self.sim.snapshot()["revision"], previous)

    def test_05_idempotent_retry(self):
        command = self.payload()
        for attempt in range(2):
            status, body = self.request("/sim/ble/write", "POST", command)
            self.assertEqual(status, 200)
            self.assertEqual(body["result"], "OK")
        self.assertEqual(self.sim.snapshot()["revision"], command["expectedRevision"] + 1)

    def test_06_invalid_utf8_boundary(self):
        before = self.sim.snapshot()["revision"]
        command = self.payload()
        command["content"]["name"] = "A" * 49
        status, body = self.request("/sim/sle/write", "POST", command)
        self.assertEqual(status, 400)
        self.assertEqual(body["result"], "BAD_INPUT")
        command = self.payload()
        command["content"]["name"] = "姓名\n错误"
        status, body = self.request("/api/v1/desk/content", "PUT", command)
        self.assertEqual(status, 400)
        self.assertEqual(body["result"], "BAD_INPUT")
        self.assertEqual(self.sim.snapshot()["revision"], before)

    def test_07_snapshot_restores_content(self):
        before = self.sim.snapshot()
        self.assertTrue(Path(self.snapshot_file).exists())
        self.assertEqual(self.sim.lib.desk_sim_init(
            os.fsencode(self.snapshot_file)), 1)
        self.assertEqual(self.sim.snapshot()["revision"], before["revision"])
        self.assertEqual(self.sim.snapshot()["display"], before["display"])

    def test_08_browser(self):
        with urlopen(self.address + "/", timeout=5) as response:
            page = response.read().decode("utf-8")
        self.assertIn("240×240", page)
        self.assertIn("模拟", page)
        self.assertIn("SLE", page)


if __name__ == "__main__":
    unittest.main()
