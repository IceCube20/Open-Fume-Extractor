#!/usr/bin/env python3
"""Local browser UI for signing Open Fume Extractor firmware packages."""

from __future__ import annotations

import json
import os
import re
import secrets
import threading
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import unquote, urlparse

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

from ofe_firmware_sign import inspect_package, key_id, sign_package, verify_package


HOST = "127.0.0.1"
MAX_BODY = 32 * 1024 * 1024
DEFAULT_KEY = (
    Path.home()
    / "Documents"
    / "Open Fume Extractor Signing Key"
    / "ofe_ed25519_private.pem"
)
HTML_FILE = Path(__file__).with_name("ofe_firmware_sign_app.html")


class AppState:
    def __init__(self) -> None:
        self.token = secrets.token_urlsafe(32)
        self.private_key: Ed25519PrivateKey | None = None
        self.key_source = ""
        if DEFAULT_KEY.is_file():
            try:
                self.set_key(DEFAULT_KEY.read_bytes(), f"Standard: {DEFAULT_KEY.name}")
            except (OSError, TypeError, ValueError):
                pass

    def set_key(self, pem: bytes, source: str) -> None:
        key = serialization.load_pem_private_key(pem, password=None)
        if not isinstance(key, Ed25519PrivateKey):
            raise ValueError("Die Datei ist kein privater Ed25519-Schlüssel.")
        self.private_key = key
        self.key_source = source

    def key_status(self) -> dict:
        if self.private_key is None:
            return {"loaded": False, "source": "", "key_id": ""}
        return {
            "loaded": True,
            "source": self.key_source,
            "key_id": key_id(self.private_key.public_key()),
        }


STATE = AppState()


def info_json(info) -> dict:
    return {
        "target": info.target,
        "version": info.version,
        "image_bytes": info.image_bytes,
        "sha256": info.sha256,
        "key_id": info.key_id,
        "signed": info.signed,
    }


class Handler(BaseHTTPRequestHandler):
    server_version = "OFEFirmwareSigner/1"

    def log_message(self, _format: str, *_args) -> None:
        pass

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header(
            "Content-Security-Policy",
            "default-src 'self'; style-src 'self' 'unsafe-inline'; "
            "script-src 'self' 'unsafe-inline'; connect-src 'self'; img-src 'self' data:",
        )
        super().end_headers()

    def do_GET(self) -> None:
        path = urlparse(self.path).path
        if path == "/":
            html = HTML_FILE.read_text(encoding="utf-8").replace("__OFE_TOKEN__", STATE.token)
            self._send(200, html.encode("utf-8"), "text/html; charset=utf-8")
            return
        if path == "/api/status":
            self._json(200, {"ok": True, "key": STATE.key_status()})
            return
        self._json(404, {"ok": False, "error": "Nicht gefunden"})

    def do_POST(self) -> None:
        if self.headers.get("X-OFE-Token", "") != STATE.token:
            self._json(403, {"ok": False, "error": "Ungültiges Sitzungstoken"})
            return
        path = urlparse(self.path).path
        try:
            body = self._read_body()
            if path == "/api/key":
                source = unquote(self.headers.get("X-File-Name", "Ausgewählter Schlüssel"))
                STATE.set_key(body, source)
                self._json(200, {"ok": True, "key": STATE.key_status()})
                return
            if path == "/api/inspect":
                self._json(200, {"ok": True, "firmware": info_json(inspect_package(body))})
                return
            if path == "/api/verify":
                if STATE.private_key is None:
                    raise ValueError("Bitte zuerst den privaten Ed25519-Schlüssel auswählen.")
                info = verify_package(body, STATE.private_key.public_key())
                self._json(200, {"ok": True, "firmware": info_json(info)})
                return
            if path == "/api/sign":
                if STATE.private_key is None:
                    raise ValueError("Bitte zuerst den privaten Ed25519-Schlüssel auswählen.")
                package, info = sign_package(STATE.private_key, body)
                filename = Path(unquote(self.headers.get("X-Output-Name", "firmware.signed.bin"))).name
                filename = re.sub(r"[^A-Za-z0-9._-]", "_", filename)
                self.send_response(200)
                self.send_header("Content-Type", "application/octet-stream")
                self.send_header("Content-Length", str(len(package)))
                self.send_header("Content-Disposition", f'attachment; filename="{filename}"')
                self.send_header("X-OFE-Target", info.target)
                self.send_header("X-OFE-Version", info.version)
                self.send_header("X-OFE-Key-ID", info.key_id)
                self.send_header("X-OFE-SHA256", info.sha256)
                self.end_headers()
                self.wfile.write(package)
                return
            if path == "/api/shutdown":
                self._json(200, {"ok": True})
                threading.Thread(target=self.server.shutdown, daemon=True).start()
                return
            self._json(404, {"ok": False, "error": "Nicht gefunden"})
        except (OSError, ValueError, TypeError, UnicodeError, InvalidSignature) as exc:
            self._json(400, {"ok": False, "error": str(exc) or "Signatur ungültig"})

    def _read_body(self) -> bytes:
        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError as exc:
            raise ValueError("Ungültige Dateigröße") from exc
        if length < 1:
            raise ValueError("Keine Datei empfangen")
        if length > MAX_BODY:
            raise ValueError("Datei ist größer als 32 MB")
        data = self.rfile.read(length)
        if len(data) != length:
            raise ValueError("Datei wurde unvollständig übertragen")
        return data

    def _json(self, status: int, value: dict) -> None:
        self._send(
            status,
            json.dumps(value, ensure_ascii=False).encode("utf-8"),
            "application/json; charset=utf-8",
        )

    def _send(self, status: int, data: bytes, content_type: str) -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)


def main() -> None:
    if not HTML_FILE.is_file():
        raise SystemExit(f"UI-Datei fehlt: {HTML_FILE}")
    server = ThreadingHTTPServer((HOST, 0), Handler)
    url = f"http://{HOST}:{server.server_port}/"
    print(url, flush=True)
    if os.environ.get("OFE_SIGNER_NO_BROWSER") != "1":
        threading.Timer(0.35, lambda: webbrowser.open(url)).start()
    try:
        server.serve_forever(poll_interval=0.25)
    finally:
        server.server_close()


if __name__ == "__main__":
    main()
