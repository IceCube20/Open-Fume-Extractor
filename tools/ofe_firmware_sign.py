#!/usr/bin/env python3
"""Create and verify signed Open Fume Extractor firmware packages.

The signed file remains an ESP .bin image followed by a printable OFE auth
trailer. The web updater transfers only the original image bytes and verifies
the trailer against the streamed SHA-256 digest before committing OTA.
"""

from __future__ import annotations

import argparse
import hashlib
import re
from dataclasses import dataclass
from pathlib import Path

from cryptography.exceptions import InvalidSignature
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import (
    Ed25519PrivateKey,
    Ed25519PublicKey,
)


LEGACY_RE = re.compile(
    rb"OFE_FW_SIG:v1;target=([A-Z0-9_]+);version=([^;\x00-\x1f]+);"
)
AUTH_RE = re.compile(
    rb"OFE_FW_AUTH:v1;target=([A-Z0-9_]+);version=([^;]+);"
    rb"size=([0-9]+);sha256=([0-9a-f]{64});keyid=([0-9a-f]{16});"
    rb"sig=([0-9a-f]{128});$"
)


@dataclass(frozen=True)
class FirmwareInfo:
    target: str
    version: str
    image_bytes: int
    sha256: str
    key_id: str = ""
    signed: bool = False


def public_raw(key: Ed25519PublicKey) -> bytes:
    return key.public_bytes(
        encoding=serialization.Encoding.Raw,
        format=serialization.PublicFormat.Raw,
    )


def key_id(key: Ed25519PublicKey) -> str:
    return hashlib.sha256(public_raw(key)).hexdigest()[:16]


def load_private(path: Path) -> Ed25519PrivateKey:
    key = serialization.load_pem_private_key(path.read_bytes(), password=None)
    if not isinstance(key, Ed25519PrivateKey):
        raise ValueError(f"{path} is not an Ed25519 private key")
    return key


def load_public(path: Path) -> Ed25519PublicKey:
    key = serialization.load_pem_public_key(path.read_bytes())
    if not isinstance(key, Ed25519PublicKey):
        raise ValueError(f"{path} is not an Ed25519 public key")
    return key


def split_package(data: bytes) -> tuple[bytes, re.Match[bytes] | None]:
    marker = data.rfind(b"OFE_FW_AUTH:v1;")
    if marker < 0:
        return data, None
    match = AUTH_RE.fullmatch(data[marker:])
    if match is None or int(match.group(3)) != marker:
        return data, None
    return data[:marker], match


def canonical(target: str, version: str, size: int, digest: str, kid: str) -> str:
    return (
        f"OFE_FW_AUTH:v1;target={target};version={version};size={size};"
        f"sha256={digest};keyid={kid};"
    )


def inspect_package(data: bytes) -> FirmwareInfo:
    raw, auth = split_package(data)
    legacy = LEGACY_RE.search(raw)
    if legacy is None:
        raise ValueError("No complete OFE_FW_SIG:v1 target/version marker found")
    target = legacy.group(1).decode("ascii")
    version = legacy.group(2).decode("ascii")
    return FirmwareInfo(
        target=target,
        version=version,
        image_bytes=len(raw),
        sha256=hashlib.sha256(raw).hexdigest(),
        key_id=auth.group(5).decode("ascii") if auth is not None else "",
        signed=auth is not None,
    )


def inspect_firmware(path: Path) -> FirmwareInfo:
    return inspect_package(path.read_bytes())


def sign_package(private: Ed25519PrivateKey, data: bytes) -> tuple[bytes, FirmwareInfo]:
    raw, _ = split_package(data)
    legacy = LEGACY_RE.search(raw)
    if legacy is None:
        raise ValueError("No complete OFE_FW_SIG:v1 target/version marker found")
    target = legacy.group(1).decode("ascii")
    version = legacy.group(2).decode("ascii")
    public = private.public_key()
    digest = hashlib.sha256(raw).hexdigest()
    kid = key_id(public)
    text = canonical(target, version, len(raw), digest, kid)
    signature = private.sign(text.encode("ascii")).hex()
    package = raw + (text + f"sig={signature};").encode("ascii")
    info = verify_package(package, public)
    return package, info


def sign_firmware(key_path: Path, source: Path, output: Path) -> FirmwareInfo:
    private = load_private(key_path)
    package, info = sign_package(private, source.read_bytes())
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(package)
    return info


def verify_package(data: bytes, public: Ed25519PublicKey) -> FirmwareInfo:
    raw, match = split_package(data)
    if match is None:
        raise ValueError("No valid OFE_FW_AUTH:v1 trailer found")
    target = match.group(1).decode("ascii")
    version = match.group(2).decode("ascii")
    declared_size = int(match.group(3))
    digest = match.group(4).decode("ascii")
    kid = match.group(5).decode("ascii")
    signature = bytes.fromhex(match.group(6).decode("ascii"))
    actual = hashlib.sha256(raw).hexdigest()
    if declared_size != len(raw):
        raise ValueError(f"Image size mismatch: expected {declared_size}, got {len(raw)}")
    if actual != digest:
        raise ValueError(f"SHA-256 mismatch: expected {digest}, got {actual}")
    expected_kid = key_id(public)
    if kid != expected_kid:
        raise ValueError(f"Key ID mismatch: expected {expected_kid}, got {kid}")
    text = canonical(target, version, len(raw), digest, kid)
    public.verify(signature, text.encode("ascii"))
    return FirmwareInfo(target, version, len(raw), digest, kid, True)


def verify_firmware(key_path: Path, source: Path) -> FirmwareInfo:
    return verify_package(source.read_bytes(), load_public(key_path))


def command_generate(args: argparse.Namespace) -> None:
    private_path = Path(args.private)
    public_path = Path(args.public)
    if private_path.exists() or public_path.exists():
        raise SystemExit("Refusing to overwrite an existing signing key")
    private_path.parent.mkdir(parents=True, exist_ok=True)
    public_path.parent.mkdir(parents=True, exist_ok=True)
    private = Ed25519PrivateKey.generate()
    private_path.write_bytes(
        private.private_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PrivateFormat.PKCS8,
            encryption_algorithm=serialization.NoEncryption(),
        )
    )
    public_path.write_bytes(
        private.public_key().public_bytes(
            encoding=serialization.Encoding.PEM,
            format=serialization.PublicFormat.SubjectPublicKeyInfo,
        )
    )
    print(f"Private key: {private_path}")
    print(f"Public key : {public_path}")
    print(f"Key ID     : {key_id(private.public_key())}")


def command_sign(args: argparse.Namespace) -> None:
    source = Path(args.input)
    output = Path(args.output) if args.output else source.with_name(
        source.stem + ".signed.bin"
    )
    info = sign_firmware(Path(args.key), source, output)
    print(f"Signed      : {output}")
    print(f"Target      : {info.target}")
    print(f"Version     : {info.version}")
    print(f"Image bytes : {info.image_bytes}")
    print(f"SHA-256     : {info.sha256}")
    print(f"Key ID      : {info.key_id}")


def command_verify(args: argparse.Namespace) -> None:
    info = verify_firmware(Path(args.key), Path(args.input))
    print(
        f"OK: {info.version} {info.target}, "
        "SHA-256 and Ed25519 signature valid"
    )


def command_header(args: argparse.Namespace) -> None:
    public = load_public(Path(args.key))
    raw = public_raw(public)
    values = ", ".join(f"0x{value:02X}" for value in raw)
    print("#pragma once")
    print()
    print(f'#define OFE_FW_AUTH_KEY_ID "{key_id(public)}"')
    print(f"static const uint8_t OFE_FW_AUTH_PUBLIC_KEY[32] = {{{values}}};")


def parser() -> argparse.ArgumentParser:
    result = argparse.ArgumentParser(description=__doc__)
    commands = result.add_subparsers(dest="command", required=True)

    generate = commands.add_parser("generate-key", help="create an Ed25519 key pair")
    generate.add_argument("--private", required=True)
    generate.add_argument("--public", required=True)
    generate.set_defaults(func=command_generate)

    sign = commands.add_parser("sign", help="append a signed auth trailer")
    sign.add_argument("--key", required=True, help="private PEM key")
    sign.add_argument("--input", required=True, help="Arduino firmware .bin")
    sign.add_argument("--output", help="signed output .bin")
    sign.set_defaults(func=command_sign)

    verify = commands.add_parser("verify", help="verify a signed firmware package")
    verify.add_argument("--key", required=True, help="public PEM key")
    verify.add_argument("--input", required=True)
    verify.set_defaults(func=command_verify)

    header = commands.add_parser("header", help="print the C++ public-key header")
    header.add_argument("--key", required=True, help="public PEM key")
    header.set_defaults(func=command_header)
    return result


def main() -> None:
    args = parser().parse_args()
    try:
        args.func(args)
    except (OSError, ValueError, InvalidSignature) as exc:
        raise SystemExit(str(exc)) from None


if __name__ == "__main__":
    main()
