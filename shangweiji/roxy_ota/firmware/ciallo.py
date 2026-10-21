"""Read-only validation of the established ``info.ciallo`` package format."""

from __future__ import annotations

import hashlib
import json
import struct
import zlib
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

from ..core.protocol import OtaError


SLOT_RANGES = {
    "A": (0x08004000, 0x08011800),
    "B": (0x08011800, 0x0801F000),
}
SRAM_START = 0x20000000
SRAM_END = 0x20004000
MAX_FIRMWARE_SIZE = 0xD800


@dataclass(frozen=True)
class FirmwareImage:
    slot: str
    file_name: str
    data: bytes
    size: int
    crc32: int
    sha256: str
    msp: int
    reset_handler: int
    linked_slot: str


@dataclass(frozen=True)
class CialloPackage:
    path: Path
    image_a: FirmwareImage
    image_b: FirmwareImage

    def image_for_slot(self, slot: str) -> FirmwareImage:
        normalized = slot.upper()
        if normalized == "A":
            return self.image_a
        if normalized == "B":
            return self.image_b
        raise OtaError(f"invalid firmware slot: {slot!r}")


def _manifest_uint32(entry: dict[str, Any], key: str) -> int:
    value = entry.get(key)
    if not isinstance(value, str):
        raise OtaError(f"manifest field {key!r} must be a hexadecimal string")
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise OtaError(f"manifest field {key!r} is not valid hexadecimal") from exc
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise OtaError(f"manifest field {key!r} is outside uint32 range")
    return parsed


def _validate_vector_table(data: bytes, slot: str, source_name: str) -> tuple[int, int, str]:
    if not 0 < len(data) <= MAX_FIRMWARE_SIZE:
        raise OtaError(
            f"{source_name}: size {len(data)} is invalid; "
            f"must be in 1..0x{MAX_FIRMWARE_SIZE:X} bytes"
        )
    if len(data) < 8:
        raise OtaError(f"{source_name}: firmware must contain MSP and Reset_Handler")

    msp, reset_handler = struct.unpack_from("<II", data, 0)
    if not SRAM_START < msp <= SRAM_END:
        raise OtaError(
            f"{source_name}: MSP 0x{msp:08X} is outside legal SRAM "
            f"(0x{SRAM_START:08X} < MSP <= 0x{SRAM_END:08X})"
        )
    if (reset_handler & 1) == 0:
        raise OtaError(f"{source_name}: Reset_Handler 0x{reset_handler:08X} is not Thumb")

    reset_address = reset_handler & ~1
    start, end = SLOT_RANGES[slot]
    if not start <= reset_address < end:
        raise OtaError(
            f"{source_name}: Reset_Handler 0x{reset_handler:08X} is outside Slot {slot}"
        )
    return msp, reset_handler, slot


def validate_firmware_bytes(data: bytes, slot: str, source_name: str = "firmware") -> FirmwareImage:
    """Validate vector table and calculate metadata from immutable BIN bytes."""

    slot = slot.upper()
    if slot not in SLOT_RANGES:
        raise OtaError(f"invalid firmware slot: {slot!r}")
    msp, reset_handler, linked_slot = _validate_vector_table(data, slot, source_name)
    return FirmwareImage(
        slot=slot,
        file_name=f"Application_{slot}.bin",
        data=bytes(data),
        size=len(data),
        crc32=zlib.crc32(data) & 0xFFFFFFFF,
        sha256=hashlib.sha256(data).hexdigest(),
        msp=msp,
        reset_handler=reset_handler,
        linked_slot=linked_slot,
    )


def _validate_manifest_image(
    entry: Any,
    slot: str,
    data: bytes,
    package_path: Path,
) -> FirmwareImage:
    if not isinstance(entry, dict):
        raise OtaError(f"manifest image {slot} must be an object")
    file_name = f"Application_{slot}.bin"
    required = ("file", "size", "crc32", "msp", "reset_handler", "linked_slot", "sha256")
    missing = [key for key in required if key not in entry]
    if missing:
        raise OtaError(f"manifest image {slot} is missing: {', '.join(missing)}")
    if entry["file"] != file_name:
        raise OtaError(
            f"manifest image {slot} file is {entry['file']!r}; expected {file_name!r}"
        )
    if isinstance(entry["size"], bool) or not isinstance(entry["size"], int):
        raise OtaError(f"manifest image {slot} size must be an integer")

    source_name = f"{package_path}!{file_name}"
    actual = validate_firmware_bytes(data, slot, source_name)
    if entry["size"] != actual.size:
        raise OtaError(
            f"manifest image {slot} size mismatch: "
            f"manifest={entry['size']}, actual={actual.size}"
        )
    if _manifest_uint32(entry, "crc32") != actual.crc32:
        raise OtaError(f"manifest image {slot} CRC32 mismatch")
    if _manifest_uint32(entry, "msp") != actual.msp:
        raise OtaError(f"manifest image {slot} MSP mismatch")
    if _manifest_uint32(entry, "reset_handler") != actual.reset_handler:
        raise OtaError(f"manifest image {slot} Reset_Handler mismatch")
    if entry["linked_slot"] != actual.linked_slot:
        raise OtaError(f"manifest image {slot} linked_slot mismatch")
    sha256 = entry["sha256"]
    if not isinstance(sha256, str) or sha256.lower() != actual.sha256:
        raise OtaError(f"manifest image {slot} SHA256 mismatch")
    return actual


def load_ciallo(path: str | Path) -> CialloPackage:
    """Read and strictly validate an existing ciallo archive without modifying it."""

    package_path = Path(path).expanduser().resolve()
    if not package_path.exists():
        raise OtaError(f"firmware package does not exist:\n{package_path}")
    if not package_path.is_file():
        raise OtaError(f"firmware package is not a regular file:\n{package_path}")

    try:
        with zipfile.ZipFile(package_path, "r") as package:
            names = package.namelist()
            if names.count("manifest.json") != 1:
                raise OtaError("manifest.json is missing or duplicated")
            for slot in ("A", "B"):
                file_name = f"Application_{slot}.bin"
                if names.count(file_name) != 1:
                    raise OtaError(f"{file_name} is missing or duplicated")
            bad_member = package.testzip()
            if bad_member is not None:
                raise OtaError(f"ZIP CRC check failed for {bad_member}")

            try:
                manifest = json.loads(package.read("manifest.json").decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise OtaError(f"manifest.json is invalid: {exc}") from exc
            if not isinstance(manifest, dict):
                raise OtaError("manifest.json must contain a JSON object")
            if manifest.get("format") != "ciallo" or manifest.get("format_version") != 1:
                raise OtaError("unsupported ciallo manifest format")
            images = manifest.get("images")
            if not isinstance(images, dict) or not all(slot in images for slot in ("A", "B")):
                raise OtaError("manifest.json must contain images A and B")

            data_a = package.read("Application_A.bin")
            data_b = package.read("Application_B.bin")
            image_a = _validate_manifest_image(images["A"], "A", data_a, package_path)
            image_b = _validate_manifest_image(images["B"], "B", data_b, package_path)
    except OtaError:
        raise
    except (OSError, zipfile.BadZipFile, KeyError) as exc:
        raise OtaError(f"cannot read firmware package {package_path}: {exc}") from exc

    return CialloPackage(package_path, image_a, image_b)

