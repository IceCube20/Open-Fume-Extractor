from __future__ import annotations

import json
import sys
import threading
import unittest
import urllib.error
import urllib.request
from pathlib import Path

from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey


TOOLS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOLS))

import ofe_firmware_sign_app as app


class FirmwareSignAppTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        app.STATE.private_key = Ed25519PrivateKey.generate()
        app.STATE.key_source = "integration test"
        cls.server = app.ThreadingHTTPServer((app.HOST, 0), app.Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base = f"http://{app.HOST}:{cls.server.server_port}"

    @classmethod
    def tearDownClass(cls) -> None:
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=2)

    def post(self, path: str, data: bytes) -> bytes:
        request = urllib.request.Request(
            self.base + path,
            data=data,
            method="POST",
            headers={
                "Content-Type": "application/octet-stream",
                "X-OFE-Token": app.STATE.token,
            },
        )
        return urllib.request.urlopen(request, timeout=5).read()

    def test_inspect_sign_verify_and_reject_tampering(self) -> None:
        raw = b"ESP-IMAGE\x00OFE_FW_SIG:v1;target=MASTER;version=9.9.9test;\x00payload"
        inspected = json.loads(self.post("/api/inspect", raw))
        self.assertEqual(inspected["firmware"]["target"], "MASTER")
        self.assertEqual(inspected["firmware"]["version"], "9.9.9test")
        self.assertFalse(inspected["firmware"]["signed"])

        signed = self.post("/api/sign", raw)
        verified = json.loads(self.post("/api/verify", signed))
        self.assertTrue(verified["firmware"]["signed"])
        self.assertEqual(verified["firmware"]["version"], "9.9.9test")

        tampered = bytearray(signed)
        tampered[1] ^= 0x01
        with self.assertRaises(urllib.error.HTTPError) as caught:
            self.post("/api/verify", bytes(tampered))
        self.assertEqual(caught.exception.code, 400)


if __name__ == "__main__":
    unittest.main()
