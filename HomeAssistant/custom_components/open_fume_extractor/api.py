"""Bounded, serialized HTTP transport. Credentials never enter logs/URLs."""
from __future__ import annotations
import asyncio
from base64 import b64encode
import json
import re
import aiohttp
from yarl import URL

MAX_RESPONSE = 2 * 1024 * 1024


class ApiError(Exception):
    """Device or transport error."""


class InvalidAuth(ApiError):
    """HTTP credentials were rejected."""


class UnsupportedFirmware(ApiError):
    """Firmware does not provide the HA session endpoint."""


class CommandRejected(ApiError):
    """A command was explicitly rejected, rather than silently retried."""


def normalize_host(value: str) -> str:
    """Accept a host or HTTP(S) origin, never credentials or a path."""
    value = value.strip()
    url = URL(value if "://" in value else "http://" + value)
    if (url.scheme not in ("http", "https") or not url.host or url.user is not None
            or url.password is not None or url.path not in ("", "/")
            or url.query_string or url.fragment):
        raise ValueError("Enter only the master host or HTTP(S) origin")
    return str(url.origin())


class OfeApi:
    def __init__(self, session, host: str, username: str, password: str):
        self.session = session
        self.host = normalize_host(host)
        if ":" in username:
            raise InvalidAuth("Invalid web username")
        self.authorization = "Basic " + b64encode(f"{username}:{password}".encode("utf-8")).decode("ascii")
        self.lock = asyncio.Lock()
        self.identity = {}

    async def _request(self, path: str, data: dict | None = None) -> tuple[int, str]:
        try:
            async with self.session.request(
                "POST" if data is not None else "GET", self.host + path,
                data=data, allow_redirects=False,
                headers={"Authorization": self.authorization,
                         **({"X-CSRF-Token": self.identity.get("csrf", "")} if data is not None else {})},
                timeout=aiohttp.ClientTimeout(total=10),
            ) as response:
                if response.status == 401:
                    raise InvalidAuth("Web login rejected")
                if 300 <= response.status < 400 or response.status == 428:
                    raise ApiError("Complete master Wi-Fi/password setup first")
                body = bytearray()
                async for chunk in response.content.iter_chunked(8192):
                    body.extend(chunk)
                    if len(body) > MAX_RESPONSE:
                        raise ApiError("Master response exceeds size limit")
                return response.status, body.decode("utf-8", errors="strict")
        except (aiohttp.ClientError, TimeoutError, UnicodeError) as err:
            raise ApiError("Master connection failed") from err

    @staticmethod
    def _json(body: str) -> dict:
        try:
            data = json.loads(body)
        except (ValueError, TypeError) as err:
            raise ApiError("Invalid master JSON") from err
        if not isinstance(data, dict):
            raise ApiError("Invalid master response")
        return data

    async def _session(self) -> dict:
        status, body = await self._request("/api/ha/session")
        if status == 404:
            raise UnsupportedFirmware("Master firmware 2.0.31 or later is required")
        if status != 200:
            raise ApiError("HA session unavailable")
        identity = self._json(body)
        if (identity.get("api_version") != 1
                or not isinstance(identity.get("master_uid"), str)
                or not isinstance(identity.get("csrf"), str)
                or not re.fullmatch(r"[0-9A-Fa-f]{16}", str(identity.get("master_uid", "")))
                or not re.fullmatch(r"[0-9A-Fa-f]{16}", str(identity.get("csrf", "")))):
            raise UnsupportedFirmware("Unsupported HA API")
        identity["master_uid"] = identity["master_uid"].upper()
        if self.identity and self.identity["master_uid"] != identity["master_uid"]:
            raise ApiError("A different master is now using this address")
        self.identity = identity
        return identity

    async def connect(self) -> dict:
        async with self.lock:
            return await self._session()

    async def state(self, descriptors: bool = False) -> dict:
        async with self.lock:
            status, body = await self._request("/state?ha=1&desc=1" if descriptors else "/state?ha=1")
            if status != 200:
                raise ApiError("Master status unavailable")
            state = self._json(body)
            if (not isinstance(state.get("modules"), list) or "master_fw" not in state
                    or not isinstance(state.get("master_uid"), str)
                    or not all(isinstance(module, dict) for module in state["modules"])):
                raise ApiError("Not an Open Fume Extractor state response")
            if self.identity and state.get("master_uid", "").upper() != self.identity["master_uid"]:
                raise ApiError("A different master is now using this address")
            return state

    async def command(self, data: dict) -> None:
        async with self.lock:
            if not self.identity:
                await self._session()
            status, body = await self._request("/api/ha/control", data)
            # This rejection happens before dispatch. Never retry a timeout or
            # an ambiguous response: a button/restart may already have run.
            if status == 403 and body.strip() == "bad csrf token":
                await self._session()
                status, body = await self._request("/api/ha/control", data)
            if not 200 <= status < 300:
                raise CommandRejected(f"Master rejected command (HTTP {status})")
