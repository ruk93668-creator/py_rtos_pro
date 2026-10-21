from __future__ import annotations

import hashlib
import json
import struct
import tempfile
import unittest
import zlib
import zipfile
from pathlib import Path

from roxy_ota.core.protocol import OtaError
from roxy_ota.firmware.ciallo import load_ciallo


def _image(slot: str) -> bytes:
    reset = 0x08004215 if slot == "A" else 0x08011A15
    data = bytearray(64)
    struct.pack_into("<II", data, 0, 0x20003EE0, reset)
    data[8:] = bytes(range(56))
    return bytes(data)


def _manifest_entry(slot: str, data: bytes) -> dict[str, object]:
    reset = struct.unpack_from("<I", data, 4)[0]
    return {
        "file": f"Application_{slot}.bin",
        "size": len(data),
        "crc32": f"0x{zlib.crc32(data) & 0xFFFFFFFF:08X}",
        "msp": f"0x{struct.unpack_from('<I', data, 0)[0]:08X}",
        "reset_handler": f"0x{reset:08X}",
        "linked_slot": slot,
        "sha256": hashlib.sha256(data).hexdigest(),
    }


_DEFAULT = object()


def _write_package(path: Path, a: bytes | None | object = _DEFAULT, b: bytes | None | object = _DEFAULT, manifest_override=None) -> None:
    if a is _DEFAULT:
        a = _image("A")
    if b is _DEFAULT:
        b = _image("B")
    assert a is None or isinstance(a, bytes)
    assert b is None or isinstance(b, bytes)
    a_data = _image("A") if a is None else a
    b_data = _image("B") if b is None else b
    manifest = {
        "format": "ciallo",
        "format_version": 1,
        "images": {"A": _manifest_entry("A", a_data), "B": _manifest_entry("B", b_data)},
    }
    if manifest_override is not None:
        manifest = manifest_override
    with zipfile.ZipFile(path, "w", compression=zipfile.ZIP_DEFLATED) as package:
        package.writestr("manifest.json", json.dumps(manifest))
        if a is not None:
            package.writestr("Application_A.bin", a)
        if b is not None:
            package.writestr("Application_B.bin", b)


class CialloTests(unittest.TestCase):
    def test_valid_package_roundtrip_and_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "info.ciallo"
            _write_package(path)
            package = load_ciallo(path)
            self.assertEqual(package.image_a.size, 64)
            self.assertEqual(package.image_b.size, 64)
            self.assertEqual(package.image_a.linked_slot, "A")
            self.assertEqual(package.image_b.linked_slot, "B")
            self.assertEqual(package.image_a.sha256, hashlib.sha256(package.image_a.data).hexdigest())
            self.assertEqual(package.image_b.crc32, zlib.crc32(package.image_b.data) & 0xFFFFFFFF)

    def test_corrupt_bin_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "corrupt.ciallo"
            a, b = _image("A"), bytearray(_image("B"))
            original_manifest = {"format": "ciallo", "format_version": 1, "images": {"A": _manifest_entry("A", a), "B": _manifest_entry("B", bytes(b))}}
            b[-1] ^= 0x01
            _write_package(path, a, bytes(b), original_manifest)
            with self.assertRaises(OtaError):
                load_ciallo(path)

    def test_manifest_crc_error_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "bad_crc.ciallo"
            _write_package(path)
            with zipfile.ZipFile(path, "r") as package:
                manifest = json.loads(package.read("manifest.json"))
                a = package.read("Application_A.bin")
                b = package.read("Application_B.bin")
            manifest["images"]["A"]["crc32"] = "0x00000000"
            _write_package(path, a, b, manifest)
            with self.assertRaises(OtaError):
                load_ciallo(path)

    def test_missing_application_a_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "missing_a.ciallo"
            b = _image("B")
            manifest = {"format": "ciallo", "format_version": 1, "images": {"A": _manifest_entry("A", _image("A")), "B": _manifest_entry("B", b)}}
            _write_package(path, None, b, manifest)
            with self.assertRaises(OtaError):
                load_ciallo(path)

    def test_wrong_linked_slot_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "wrong_slot.ciallo"
            a = bytearray(_image("A"))
            struct.pack_into("<I", a, 4, 0x08011A15)
            _write_package(path, bytes(a), _image("B"))
            with self.assertRaises(OtaError):
                load_ciallo(path)
