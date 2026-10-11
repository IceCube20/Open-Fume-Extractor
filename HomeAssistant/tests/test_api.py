"""Real HTTP transport tests against a local aiohttp device simulator."""
import asyncio
from base64 import b64encode
import importlib.util
from pathlib import Path
import sys
import unittest
from unittest.mock import patch
import aiohttp
from aiohttp import web

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "ofe_api_test", ROOT / "custom_components/open_fume_extractor/api.py")
api = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = api
SPEC.loader.exec_module(api)
UID = "000000001234ABCD"


class HostTests(unittest.TestCase):
    def test_normal_hosts(self):
        self.assertEqual(api.normalize_host("192.168.20.12"), "http://192.168.20.12")
        self.assertEqual(api.normalize_host("http://[::1]:8080/"), "http://[::1]:8080")

    def test_unsafe_origins_rejected(self):
        for host in ("http://user:pass@host", "host/config", "ftp://host", "host?password=secret", "host#abc", ""):
            with self.subTest(host=host), self.assertRaises(ValueError):
                api.normalize_host(host)


class ApiTests(unittest.IsolatedAsyncioTestCase):
    async def asyncSetUp(self):
        self.uid = UID
        self.csrf = "0123456789ABCDEF"
        self.calls = []
        self.active = self.maximum_active = 0
        self.fail = None
        self.bad_state = None
        self.malformed_session = None
        self.reject_csrf_once = False
        app = web.Application()
        app.router.add_route("*", "/{tail:.*}", self.handle)
        self.runner = web.AppRunner(app)
        await self.runner.setup()
        site = web.TCPSite(self.runner, "127.0.0.1", 0)
        await site.start()
        port = self.runner.addresses[0][1]
        self.session = aiohttp.ClientSession()
        self.client = api.OfeApi(self.session, f"http://127.0.0.1:{port}", "admin", "local-test-password")

    async def asyncTearDown(self):
        await self.session.close()
        await self.runner.cleanup()

    async def handle(self, request):
        self.calls.append((request.path_qs, request.method, dict(await request.post()),
                           request.headers.get("X-CSRF-Token")))
        if request.headers.get("Authorization") != "Basic " + b64encode(b"admin:local-test-password").decode("ascii"):
            return web.Response(status=401)
        self.active += 1
        self.maximum_active = max(self.maximum_active, self.active)
        try:
            await asyncio.sleep(0.005)
            if self.fail:
                return web.Response(status=self.fail[0], text=self.fail[1])
            if request.path == "/api/ha/session":
                return web.json_response(self.malformed_session or {
                    "api_version": 1, "master_uid": self.uid, "csrf": self.csrf, "firmware": "2.0.31RC"})
            if request.path == "/state":
                return web.json_response(self.bad_state if self.bad_state is not None else {
                    "master_uid": self.uid, "master_fw": "2.0.31RC", "modules": []})
            if self.reject_csrf_once:
                self.reject_csrf_once = False
                self.csrf = "FEDCBA9876543210"
                return web.Response(status=403, text="bad csrf token")
            return web.Response(text="ok")
        finally:
            self.active -= 1

    async def test_session_and_auth(self):
        identity = await self.client.connect()
        self.assertEqual(identity["master_uid"], UID)
        self.client.authorization = "Basic " + b64encode(b"admin:wrong").decode("ascii")
        with self.assertRaises(api.InvalidAuth):
            await self.client.state()

    async def test_old_firmware(self):
        self.fail = (404, "not found")
        with self.assertRaises(api.UnsupportedFirmware):
            await self.client.connect()

    async def test_initial_setup_and_redirect_do_not_follow(self):
        for code in (302, 428):
            self.fail = (code, "")
            with self.assertRaises(api.ApiError):
                await self.client.connect()
        self.assertEqual(len(self.calls), 2)

    async def test_identity_change_blocks_read_and_command_retry(self):
        await self.client.connect()
        self.uid = "00000000DEADBEEF"
        with self.assertRaises(api.ApiError):
            await self.client.state()
        self.reject_csrf_once = True
        with self.assertRaises(api.ApiError):
            await self.client.command({"action": "restart"})
        self.assertEqual(sum(c[1] == "POST" for c in self.calls), 1)

    async def test_csrf_refresh_once_only(self):
        await self.client.connect()
        self.reject_csrf_once = True
        await self.client.command({"action": "entity", "addr": "32", "uid": UID, "id": "20", "value": "55"})
        posts = [c for c in self.calls if c[1] == "POST"]
        self.assertEqual(len(posts), 2)
        self.assertEqual(posts[1][3], self.csrf)
        self.assertEqual(posts[1][2]["value"], "55")

    async def test_ambiguous_server_failure_is_never_retried(self):
        await self.client.connect()
        self.fail = (500, "could have run")
        with self.assertRaises(api.CommandRejected):
            await self.client.command({"action": "restart"})
        self.assertEqual(sum(c[1] == "POST" for c in self.calls), 1)

    async def test_timeout_is_never_retried(self):
        await self.client.connect()
        with patch.object(self.client, "_request", side_effect=api.ApiError("Master connection failed")) as request:
            with self.assertRaises(api.ApiError):
                await self.client.command({"action": "restart"})
            self.assertEqual(request.await_count, 1)

    async def test_requests_serialized(self):
        await self.client.connect()
        await asyncio.gather(self.client.state(), self.client.state(True), self.client.command({"action": "restart"}))
        self.assertEqual(self.maximum_active, 1)
        self.assertTrue(any(c[0] == "/state?ha=1&desc=1" for c in self.calls))

    async def test_invalid_state_schema(self):
        await self.client.connect()
        for state in ([], {"modules": [7], "master_fw": "2.0.31", "master_uid": UID},
                      {"modules": [], "master_fw": "2.0.31", "master_uid": 123}):
            self.bad_state = state
            with self.assertRaises(api.ApiError):
                await self.client.state()

    async def test_integer_identity_rejected(self):
        self.malformed_session = {"api_version": 1, "master_uid": 1234567890123456, "csrf": self.csrf}
        with self.assertRaises(api.UnsupportedFirmware):
            await self.client.connect()

    async def test_response_limit(self):
        self.fail = (200, "x" * 1025)
        with patch.object(api, "MAX_RESPONSE", 1024), self.assertRaises(api.ApiError):
            await self.client.connect()

    def test_invalid_json(self):
        for text in ("invalid", "[]", "null"):
            with self.assertRaises(api.ApiError):
                api.OfeApi._json(text)


if __name__ == "__main__":
    unittest.main()
