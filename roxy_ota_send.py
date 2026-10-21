#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PY32F071 OTA sender.

The sender follows the MCU's existing stop-and-wait protocol.  It does not
reset the target, send ABORT, or retry a frame automatically.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib
import zipfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable, Optional


SOF = b"\xAA\x55"
FRAME_HEADER_SIZE = 7
FRAME_CRC_SIZE = 2
FRAME_MIN_SIZE = FRAME_HEADER_SIZE + FRAME_CRC_SIZE
MAX_DATA_SIZE = 256
DATA_OFFSET_SIZE = 4
MAX_PAYLOAD_SIZE = DATA_OFFSET_SIZE + MAX_DATA_SIZE
INFO_PAYLOAD_SIZE = 44
SERIAL_BAUD = 38400
RESPONSE_TIMEOUT = 10.0

CMD_BEGIN = 0x01
CMD_DATA = 0x02
CMD_END = 0x03
CMD_ABORT = 0x04
CMD_QUERY_INFO = 0x05
RSP_ACK = 0x80
RSP_NACK = 0x81
RSP_INFO = 0x82

SLOT_INFO = {
    "A": (0x08004000, 0x08011800, 0xD800),
    "B": (0x08011800, 0x0801F000, 0xD800),
}
SRAM_START = 0x20000000
SRAM_END = 0x20004000
SLOT_STATE_NAMES = {
    0: "EMPTY",
    1: "DOWNLOADING",
    2: "PENDING",
    3: "CONFIRMED",
    4: "BAD",
}


class OtaError(Exception):
    """Expected OTA validation, protocol, or serial error."""


class FrameError(OtaError):
    """A received or constructed frame is malformed."""


class TimeoutError(OtaError):
    """No complete response arrived before the deadline."""


@dataclass(frozen=True)
class OtaFrame:
    frame_type: int
    seq: int
    payload: bytes


@dataclass(frozen=True)
class DeviceInfo:
    current_slot: int
    slot_a_state: int
    slot_a_version: int
    slot_a_size: int
    slot_a_crc32: int
    slot_a_boot_attempt: int
    slot_b_state: int
    slot_b_version: int
    slot_b_size: int
    slot_b_crc32: int
    slot_b_boot_attempt: int


@dataclass(frozen=True)
class FirmwareInfo:
    path: Path
    source_axf: Optional[Path]
    generated_bin: Optional[Path]
    source_ciallo: Optional[Path]
    version: int
    data: bytes
    size: int
    crc32: int
    msp: int
    reset_handler: int
    reset_address: int
    slot: str
    linked_slot: str
    packet_count: int
    last_packet_size: int


@dataclass(frozen=True)
class Response:
    kind: str
    frame: Optional[OtaFrame] = None
    error: Optional[str] = None


def crc16_ccitt_false(data: bytes) -> int:
    """Return CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF)."""

    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(frame_type: int, seq: int, payload: bytes) -> bytes:
    """Build AA 55 + TYPE/SEQ/LENGTH/PAYLOAD + little-endian CRC16."""

    if not 0 <= frame_type <= 0xFF:
        raise FrameError(f"frame type out of range: {frame_type}")
    if not 0 <= seq <= 0xFFFF:
        raise FrameError(f"sequence out of range: {seq}")
    if len(payload) > 0xFFFF:
        raise FrameError(f"payload too large: {len(payload)} bytes")

    # CRC16 只覆盖 TYPE、SEQ、LENGTH 和 PAYLOAD，不包含 AA 55 或 CRC 字段。
    body = bytes([frame_type]) + struct.pack("<HH", seq, len(payload)) + payload
    crc = crc16_ccitt_false(body)
    return SOF + body + struct.pack("<H", crc)


def build_begin_frame(version: int, firmware_size: int, firmware_crc32: int) -> bytes:
    if not 0 <= version <= 0xFFFFFFFF:
        raise FrameError(f"version out of uint32 range: {version}")
    if not 0 <= firmware_size <= 0xFFFFFFFF:
        raise FrameError(f"firmware size out of uint32 range: {firmware_size}")
    if not 0 <= firmware_crc32 <= 0xFFFFFFFF:
        raise FrameError(f"firmware CRC32 out of uint32 range: {firmware_crc32}")
    payload = struct.pack("<III", version, firmware_size, firmware_crc32)
    return build_frame(CMD_BEGIN, 0, payload)


def build_data_frame(seq: int, offset: int, data: bytes) -> bytes:
    if not 0 <= offset <= 0xFFFFFFFF:
        raise FrameError(f"offset out of uint32 range: {offset}")
    if not 0 < len(data) <= MAX_DATA_SIZE:
        raise FrameError(f"DATA length must be 1..{MAX_DATA_SIZE}, got {len(data)}")
    return build_frame(CMD_DATA, seq, struct.pack("<I", offset) + data)


def build_end_frame(seq: int) -> bytes:
    return build_frame(CMD_END, seq, b"")


def parse_frame(raw: bytes) -> OtaFrame:
    """Validate a complete frame and return its decoded fields."""

    if len(raw) < FRAME_MIN_SIZE:
        raise FrameError(f"frame too short: {len(raw)} bytes")
    if raw[:2] != SOF:
        raise FrameError("invalid frame start")

    frame_type = raw[2]
    seq, payload_length = struct.unpack_from("<HH", raw, 3)
    if payload_length > MAX_PAYLOAD_SIZE:
        raise FrameError(f"payload length exceeds {MAX_PAYLOAD_SIZE}: {payload_length}")
    expected_length = FRAME_HEADER_SIZE + payload_length + FRAME_CRC_SIZE
    if len(raw) != expected_length:
        raise FrameError(
            f"frame length mismatch: got {len(raw)}, expected {expected_length}"
        )

    received_crc = struct.unpack_from("<H", raw, FRAME_HEADER_SIZE + payload_length)[0]
    calculated_crc = crc16_ccitt_false(raw[2 : FRAME_HEADER_SIZE + payload_length])
    if received_crc != calculated_crc:
        raise FrameError(
            "CRC16 mismatch: "
            f"received=0x{received_crc:04X}, calculated=0x{calculated_crc:04X}"
        )

    payload = raw[FRAME_HEADER_SIZE : FRAME_HEADER_SIZE + payload_length]
    return OtaFrame(frame_type, seq, payload)


class FrameStreamParser:
    """Incrementally find and validate frames in a byte stream."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[tuple[Optional[OtaFrame], Optional[FrameError]]]:
        events: list[tuple[Optional[OtaFrame], Optional[FrameError]]] = []
        self._buffer.extend(data)

        while True:
            start = self._buffer.find(SOF)
            if start < 0:
                # 保留末尾 AA，它可能是下一次读取到的帧头第一字节；其余垃圾丢弃。
                if self._buffer[-1:] == SOF[:1]:
                    self._buffer[:] = SOF[:1]
                else:
                    self._buffer.clear()
                break
            if start:
                del self._buffer[:start]
            if len(self._buffer) < FRAME_HEADER_SIZE:
                break

            payload_length = struct.unpack_from("<H", self._buffer, 5)[0]
            if payload_length > MAX_PAYLOAD_SIZE:
                # 长度超过 MCU 接收缓存，丢掉当前候选帧头并继续找下一组 AA 55。
                del self._buffer[0]
                continue

            total_length = FRAME_HEADER_SIZE + payload_length + FRAME_CRC_SIZE
            if len(self._buffer) < total_length:
                break

            candidate = bytes(self._buffer[:total_length])
            try:
                frame = parse_frame(candidate)
            except FrameError as exc:
                # ACK/NACK/INFO CRC 错误必须立即停止；其它畸形候选按串口垃圾跳过。
                if (
                    len(candidate) >= FRAME_HEADER_SIZE
                    and (
                        candidate[2] == RSP_INFO
                        or (
                            candidate[2] in (RSP_ACK, RSP_NACK)
                            and payload_length == 0
                        )
                    )
                ):
                    events.append((None, exc))
                del self._buffer[0]
                continue

            del self._buffer[:total_length]
            events.append((frame, None))

        return events


def receive_frame(serial_port: Any, timeout: float) -> OtaFrame:
    """Read one complete frame without assuming serial.read returns all bytes."""

    parser = FrameStreamParser()
    deadline = time.monotonic() + timeout
    while True:
        remaining = deadline - time.monotonic()
        if remaining <= 0:
            raise TimeoutError("response timeout")

        # 串口使用短超时分块读取，解析器负责处理一次读取被拆分的情况。
        chunk = serial_port.read(64)
        if not chunk:
            continue
        for frame, error in parser.feed(bytes(chunk)):
            if error is not None:
                raise error
            if frame is not None:
                return frame


def wait_response(serial_port: Any, expected_seq: int, timeout: float) -> Response:
    """Wait for an ACK/NACK for exactly expected_seq."""

    try:
        frame = receive_frame(serial_port, timeout)
    except TimeoutError as exc:
        return Response("TIMEOUT", error=str(exc))
    except FrameError as exc:
        return Response("ERROR", error=str(exc))
    except Exception as exc:
        return Response("ERROR", error=f"serial read failed: {exc}")

    if frame.frame_type == RSP_INFO:
        if frame.seq != expected_seq:
            return Response(
                "ERROR",
                frame=frame,
                error=(
                    "INFO sequence mismatch: "
                    f"expected={expected_seq}, received={frame.seq}"
                ),
            )
        return Response("INFO", frame=frame)
    if frame.frame_type not in (RSP_ACK, RSP_NACK):
        return Response(
            "ERROR",
            frame=frame,
            error=f"unexpected response type: 0x{frame.frame_type:02X}",
        )
    if frame.payload:
        return Response(
            "ERROR",
            frame=frame,
            error=f"response payload must be empty, got {len(frame.payload)} bytes",
        )
    if frame.seq != expected_seq:
        return Response(
            "ERROR",
            frame=frame,
            error=f"ACK sequence mismatch: expected={expected_seq}, received={frame.seq}",
        )
    return Response("ACK" if frame.frame_type == RSP_ACK else "NACK", frame=frame)


def _parse_uint32(value: str) -> int:
    try:
        number = int(value, 0)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected uint32, got {value!r}") from exc
    if not 0 <= number <= 0xFFFFFFFF:
        raise argparse.ArgumentTypeError(f"uint32 out of range: {value!r}")
    return number


def _parse_positive_float(value: str) -> float:
    try:
        number = float(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"expected positive number, got {value!r}") from exc
    if number <= 0:
        raise argparse.ArgumentTypeError("value must be greater than zero")
    return number


def _is_bootloader_path(path: Path) -> bool:
    """Return whether an AXF path is clearly a Bootloader output."""

    for part in path.parts:
        name = part.lower()
        if name == "boot" or "bootloader" in name:
            return True
    return False


def find_axf_files(project_root: Path) -> list[Path]:
    """Recursively find AXFs while excluding generated/cache directories."""

    root = project_root.resolve()
    if not root.exists() or not root.is_dir():
        raise OtaError(f"project root does not exist or is not a directory:\n{root}")

    ignored = {".git", "__pycache__"}
    files = []
    for candidate in root.rglob("*.axf"):
        try:
            relative_parts = candidate.relative_to(root).parts
        except ValueError:
            continue
        if any(part.lower() in ignored for part in relative_parts):
            continue
        # Bootloader 产物不是可升级的 Application AXF。
        if candidate.is_file() and not _is_bootloader_path(candidate):
            files.append(candidate.resolve())
    return sorted(files, key=lambda item: str(item).lower())


def _slot_path_tokens(slot: str) -> tuple[str, ...]:
    return {
        "A": ("applicationa", "application_a", "appa"),
        "B": ("applicationb", "application_b", "appb"),
    }[slot]


def select_axf(project_root: Path, slot: str, axf_path: Optional[str] = None) -> Path:
    """Select an explicit or uniquely identifiable Application AXF."""

    slot = slot.upper()
    if slot not in SLOT_INFO:
        raise OtaError(f"invalid slot: {slot!r}; choose A or B")

    if axf_path is not None:
        selected = Path(axf_path).expanduser().resolve()
        if not selected.is_file():
            raise OtaError(f"AXF file does not exist:\n{selected}")
        if selected.suffix.lower() != ".axf":
            raise OtaError(f"--axf must point to an .axf file:\n{selected}")
        if _is_bootloader_path(selected):
            raise OtaError(f"Bootloader AXF cannot be used for OTA:\n{selected}")
        return selected

    all_axfs = find_axf_files(project_root)
    if not all_axfs:
        raise OtaError(f"no AXF file found under:\n{project_root.resolve()}")

    tokens = _slot_path_tokens(slot)
    candidates = [
        candidate
        for candidate in all_axfs
        if not _is_bootloader_path(candidate)
        and any(token in str(candidate).lower() for token in tokens)
    ]
    if not candidates:
        raise OtaError(
            f"no Application AXF candidate found for Slot {slot} under:\n"
            f"{project_root.resolve()}"
        )
    if len(candidates) > 1:
        print("Multiple AXF candidates found:")
        for index, candidate in enumerate(candidates, 1):
            print(f"[{index}] {candidate}")
        raise OtaError(
            "multiple Application AXF candidates; cannot choose automatically"
        )
    return candidates[0]


def resolve_fromelf(user_path: Optional[str] = None) -> Path:
    """Resolve ARMCC fromelf without downloading or installing anything."""

    if user_path is not None:
        selected = Path(user_path).expanduser().resolve()
        if selected.is_file():
            return selected
        raise OtaError(f"fromelf not found:\n{selected}")

    default = Path(r"D:\32\ARM\ARMCC\bin\fromelf.exe")
    if default.is_file():
        return default.resolve()
    for name in ("fromelf.exe", "fromelf"):
        found = shutil.which(name)
        if found:
            return Path(found).resolve()
    raise OtaError(
        "fromelf not found; install ARMCC or put fromelf.exe/fromelf on PATH"
    )


def generate_bin(axf_path: Path, slot: str, fromelf_path: Path) -> Path:
    """Generate the slot-specific BIN beside the selected AXF."""

    slot = slot.upper()
    output_name = f"Application_{slot}.bin"
    output_path = axf_path.parent / output_name
    print("AXF:")
    print(f"  path = {axf_path}")
    print("Generating BIN:")
    print(f"  fromelf = {fromelf_path}")
    print(f"  output   = {output_path}")

    command = [
        str(fromelf_path),
        "--bin",
        "--output",
        str(output_path),
        str(axf_path),
    ]
    try:
        result = subprocess.run(
            command,
            capture_output=True,
            text=True,
            errors="replace",
            check=False,
        )
    except OSError as exc:
        raise OtaError(f"failed to execute fromelf: {exc}") from exc

    if result.returncode != 0:
        details = [f"fromelf failed with exit code {result.returncode}"]
        if result.stdout.strip():
            details.append(f"stdout:\n{result.stdout.strip()}")
        if result.stderr.strip():
            details.append(f"stderr:\n{result.stderr.strip()}")
        raise OtaError("\n".join(details))
    if not output_path.is_file() or output_path.stat().st_size <= 0:
        raise OtaError(f"fromelf did not produce a non-empty BIN:\n{output_path}")

    print("BIN generated successfully.")
    return output_path.resolve()


def detect_linked_slot(reset_handler: int) -> str:
    """Infer A/B from the real (Thumb bit cleared) Reset_Handler address."""

    if (reset_handler & 1) == 0:
        return "UNKNOWN"
    reset_address = reset_handler & ~1
    for slot, (start, end, _size) in SLOT_INFO.items():
        if start <= reset_address < end:
            return slot
    return "UNKNOWN"


def analyze_firmware(
    path: Path,
    version: int,
    slot: str,
    source_axf: Optional[Path] = None,
    generated_bin: Optional[Path] = None,
    source_ciallo: Optional[Path] = None,
) -> FirmwareInfo:
    """Read a BIN and verify its vector table belongs to the requested slot."""

    try:
        firmware = path.read_bytes()
    except OSError as exc:
        raise OtaError(f"cannot read firmware {path}: {exc}") from exc

    return analyze_firmware_bytes(
        firmware,
        path,
        version,
        slot,
        source_axf=source_axf,
        generated_bin=generated_bin,
        source_ciallo=source_ciallo,
    )


def analyze_firmware_bytes(
    firmware: bytes,
    path: Path,
    version: int,
    slot: str,
    source_axf: Optional[Path] = None,
    generated_bin: Optional[Path] = None,
    source_ciallo: Optional[Path] = None,
) -> FirmwareInfo:
    """Analyze already-loaded BIN bytes, including ciallo members."""

    slot = slot.upper()
    if slot not in SLOT_INFO:
        raise OtaError(f"invalid slot: {slot!r}; choose A or B")
    _slot_start, _slot_end, slot_size = SLOT_INFO[slot]

    size = len(firmware)
    if not 0 < size <= slot_size:
        raise OtaError(
            f"firmware size {size} is invalid for Slot {slot}; "
            f"must be in 1..0x{slot_size:X} bytes"
        )
    if size < 8:
        raise OtaError("firmware must contain the initial MSP and Reset_Handler (8 bytes)")

    msp, reset_handler = struct.unpack_from("<II", firmware, 0)
    if not SRAM_START < msp <= SRAM_END:
        raise OtaError(
            f"initial MSP 0x{msp:08X} is outside SRAM "
            f"[0x{SRAM_START:08X}, 0x{SRAM_END:08X}]"
        )
    if (reset_handler & 1) == 0:
        raise OtaError(f"Reset_Handler 0x{reset_handler:08X} is not a Thumb address")

    reset_address = reset_handler & ~1
    linked_slot = detect_linked_slot(reset_handler)
    if linked_slot != slot:
        raise OtaError(
            "firmware slot mismatch\n"
            f"requested slot = {slot}\n"
            f"linked slot    = {linked_slot}\n"
            f"Reset_Handler = 0x{reset_handler:08X}"
        )

    packet_count = (size + MAX_DATA_SIZE - 1) // MAX_DATA_SIZE
    if packet_count + 1 > 0xFFFF:
        raise OtaError("firmware requires more than the protocol's uint16 sequence space")

    last_packet_size = size % MAX_DATA_SIZE or MAX_DATA_SIZE

    return FirmwareInfo(
        path=path,
        source_axf=source_axf,
        generated_bin=generated_bin,
        source_ciallo=source_ciallo,
        version=version,
        data=firmware,
        size=size,
        crc32=zlib.crc32(firmware) & 0xFFFFFFFF,
        msp=msp,
        reset_handler=reset_handler,
        reset_address=reset_address,
        slot=slot,
        linked_slot=linked_slot,
        packet_count=packet_count,
        last_packet_size=last_packet_size,
    )


def validate_firmware(path: Path, version: int, slot: str) -> FirmwareInfo:
    """Backward-compatible wrapper for callers of the original script."""

    return analyze_firmware(path, version, slot)


def _manifest_hex32(entry: dict[str, Any], key: str) -> int:
    value = entry.get(key)
    if not isinstance(value, str):
        raise OtaError(f"manifest field {key!r} must be a hexadecimal string")
    try:
        parsed = int(value, 0)
    except ValueError as exc:
        raise OtaError(f"manifest field {key!r} is not valid hexadecimal: {value!r}") from exc
    if not 0 <= parsed <= 0xFFFFFFFF:
        raise OtaError(f"manifest field {key!r} is outside uint32 range")
    return parsed


def _manifest_entry(info: FirmwareInfo, slot: str) -> dict[str, Any]:
    return {
        "file": f"Application_{slot}.bin",
        "size": info.size,
        "crc32": f"0x{info.crc32:08X}",
        "msp": f"0x{info.msp:08X}",
        "reset_handler": f"0x{info.reset_handler:08X}",
        "linked_slot": info.linked_slot,
        "sha256": hashlib.sha256(info.data).hexdigest(),
    }


def _validate_manifest_entry(
    entry: Any,
    slot: str,
    data: bytes,
    package_path: Path,
) -> FirmwareInfo:
    if not isinstance(entry, dict):
        raise OtaError(f"manifest image {slot} must be an object")
    expected_file = f"Application_{slot}.bin"
    if entry.get("file") != expected_file:
        raise OtaError(
            f"manifest image {slot} has invalid file: {entry.get('file')!r}; "
            f"expected {expected_file!r}"
        )
    required = ("size", "crc32", "msp", "reset_handler", "linked_slot", "sha256")
    missing = [key for key in required if key not in entry]
    if missing:
        raise OtaError(f"manifest image {slot} is missing: {', '.join(missing)}")

    virtual_path = Path(f"{package_path}!{expected_file}")
    info = analyze_firmware_bytes(data, virtual_path, 0, slot, source_ciallo=package_path)
    if entry["size"] != info.size:
        raise OtaError(f"ciallo {slot} size mismatch")
    if _manifest_hex32(entry, "crc32") != info.crc32:
        raise OtaError(f"ciallo {slot} CRC32 mismatch")
    if _manifest_hex32(entry, "msp") != info.msp:
        raise OtaError(f"ciallo {slot} MSP mismatch")
    if _manifest_hex32(entry, "reset_handler") != info.reset_handler:
        raise OtaError(f"ciallo {slot} Reset_Handler mismatch")
    if entry["linked_slot"] != info.linked_slot:
        raise OtaError(f"ciallo {slot} linked_slot mismatch")
    sha256 = entry["sha256"]
    if not isinstance(sha256, str) or sha256.lower() != hashlib.sha256(data).hexdigest():
        raise OtaError(f"ciallo {slot} SHA256 mismatch")
    return info


def build_ciallo(project_root: Path) -> Path:
    """Build both Application images and package them with manifest.json."""

    selected_fromelf = resolve_fromelf()
    infos: dict[str, FirmwareInfo] = {}
    for slot in ("A", "B"):
        selected_axf = select_axf(project_root, slot)
        generated_bin = generate_bin(selected_axf, slot, selected_fromelf)
        infos[slot] = analyze_firmware(
            generated_bin,
            0,
            slot,
            source_axf=selected_axf,
            generated_bin=generated_bin,
        )

    package_path = project_root.resolve() / "info.ciallo"
    write_ciallo_package(package_path, infos)
    print("CIALLO generated successfully.")
    print(f"  output = {package_path}")
    return package_path


def write_ciallo_package(package_path: Path, infos: dict[str, FirmwareInfo]) -> Path:
    """Write a ciallo archive from two already-validated slot images."""

    if set(infos) != {"A", "B"}:
        raise OtaError("ciallo package requires validated images for both A and B")
    manifest = {
        "format": "ciallo",
        "format_version": 1,
        "images": {slot: _manifest_entry(infos[slot], slot) for slot in ("A", "B")},
    }
    package_path = package_path.resolve()
    try:
        with zipfile.ZipFile(package_path, "w", compression=zipfile.ZIP_DEFLATED) as package:
            package.writestr(
                "manifest.json",
                json.dumps(manifest, ensure_ascii=False, indent=4, sort_keys=True) + "\n",
            )
            for slot in ("A", "B"):
                package.write(infos[slot].path, arcname=f"Application_{slot}.bin")
    except (OSError, zipfile.BadZipFile) as exc:
        raise OtaError(f"failed to create ciallo package {package_path}: {exc}") from exc
    if not package_path.is_file() or package_path.stat().st_size <= 0:
        raise OtaError(f"ciallo package was not created:\n{package_path}")
    return package_path


def load_ciallo(package_path: Path, target_slot: str, version: int) -> FirmwareInfo:
    """Load and validate the target image from an existing .ciallo package."""

    target_slot = target_slot.upper()
    if target_slot not in SLOT_INFO:
        raise OtaError(f"invalid target slot: {target_slot!r}; choose A or B")
    package_path = package_path.resolve()
    try:
        with zipfile.ZipFile(package_path, "r") as package:
            names = package.namelist()
            if names.count("manifest.json") != 1:
                raise OtaError("ciallo manifest.json is missing or duplicated")
            if package.testzip() is not None:
                raise OtaError("ciallo ZIP data CRC check failed")
            try:
                manifest = json.loads(package.read("manifest.json").decode("utf-8"))
            except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                raise OtaError(f"ciallo manifest.json is invalid: {exc}") from exc

            if not isinstance(manifest, dict):
                raise OtaError("ciallo manifest must be a JSON object")
            if manifest.get("format") != "ciallo" or manifest.get("format_version") != 1:
                raise OtaError("unsupported ciallo manifest format")
            images = manifest.get("images")
            if not isinstance(images, dict) or not all(slot in images for slot in ("A", "B")):
                raise OtaError("ciallo manifest must contain images A and B")

            validated: dict[str, FirmwareInfo] = {}
            for slot in ("A", "B"):
                file_name = f"Application_{slot}.bin"
                if names.count(file_name) != 1:
                    raise OtaError(f"ciallo {file_name} is missing or duplicated")
                data = package.read(file_name)
                validated[slot] = _validate_manifest_entry(images[slot], slot, data, package_path)
    except OtaError:
        raise
    except (OSError, zipfile.BadZipFile) as exc:
        raise OtaError(f"cannot read ciallo package {package_path}: {exc}") from exc

    selected = validated[target_slot]
    return FirmwareInfo(
        path=selected.path,
        source_axf=None,
        generated_bin=None,
        source_ciallo=package_path,
        version=version,
        data=selected.data,
        size=selected.size,
        crc32=selected.crc32,
        msp=selected.msp,
        reset_handler=selected.reset_handler,
        reset_address=selected.reset_address,
        slot=selected.slot,
        linked_slot=selected.linked_slot,
        packet_count=selected.packet_count,
        last_packet_size=selected.last_packet_size,
    )


def print_firmware_info(info: FirmwareInfo) -> None:
    print("Firmware:")
    if info.source_ciallo is not None:
        print(f"  source        = {info.source_ciallo}")
    elif info.source_axf is not None:
        print(f"  source AXF    = {info.source_axf}")
        print(f"  generated BIN = {info.generated_bin}")
    else:
        print(f"  source BIN    = {info.path}")
    print(f"  version       = {info.version}")
    print(f"  size          = {info.size} bytes")
    print(f"  crc32         = 0x{info.crc32:08X}")
    print(f"  MSP           = 0x{info.msp:08X}")
    print(f"  Reset_Handler = 0x{info.reset_handler:08X}")
    print(f"  linked slot   = {info.linked_slot}")
    print(f"  target slot   = {info.slot}")
    print(f"  packets       = {info.packet_count}")
    print(f"  last packet   = {info.last_packet_size} bytes")
    print(f"  END seq       = {info.packet_count + 1}")


def _write_frame(serial_port: Any, frame: bytes) -> None:
    try:
        sent = serial_port.write(frame)
        if sent is not None and sent != len(frame):
            raise OtaError(f"serial write incomplete: {sent}/{len(frame)} bytes")
        serial_port.flush()
    except OtaError:
        raise
    except Exception as exc:
        raise OtaError(f"serial write failed: {exc}") from exc


def query_device_info(serial_port: Any, timeout: float = 10.0) -> DeviceInfo:
    """Send QUERY_INFO and decode the MCU's fixed 44-byte INFO payload."""

    request = build_frame(CMD_QUERY_INFO, 0, b"")
    _write_frame(serial_port, request)
    response = wait_response(serial_port, 0, timeout)
    if response.kind != "INFO" or response.frame is None:
        _raise_response_error(response, 0)
    if len(response.frame.payload) != INFO_PAYLOAD_SIZE:
        raise OtaError(
            f"INFO payload length mismatch: got {len(response.frame.payload)}, "
            f"expected {INFO_PAYLOAD_SIZE}"
        )

    values = struct.unpack("<11I", response.frame.payload)
    info = DeviceInfo(*values)
    if info.current_slot not in (0, 1, 2):
        raise OtaError(f"invalid current_slot in INFO: {info.current_slot}")
    for slot_name, state in (("A", info.slot_a_state), ("B", info.slot_b_state)):
        if state not in SLOT_STATE_NAMES:
            raise OtaError(f"invalid Slot {slot_name} state in INFO: {state}")
    return info


def _slot_label(value: int) -> str:
    return {0: "NONE", 1: "A", 2: "B"}.get(value, "UNKNOWN")


def _state_label(value: int) -> str:
    return SLOT_STATE_NAMES.get(value, f"UNKNOWN({value})")


def print_device_info(info: DeviceInfo) -> None:
    print("Device:")
    print(f"  running slot = {_slot_label(info.current_slot)}")
    print()
    print("Slot A:")
    print(f"  state        = {_state_label(info.slot_a_state)}")
    print(f"  version      = {info.slot_a_version}")
    print(f"  size         = {info.slot_a_size}")
    print(f"  crc32        = 0x{info.slot_a_crc32:08X}")
    print(f"  boot_attempt = {info.slot_a_boot_attempt}")
    print()
    print("Slot B:")
    print(f"  state        = {_state_label(info.slot_b_state)}")
    print(f"  version      = {info.slot_b_version}")
    print(f"  size         = {info.slot_b_size}")
    print(f"  crc32        = 0x{info.slot_b_crc32:08X}")
    print(f"  boot_attempt = {info.slot_b_boot_attempt}")


def determine_target_slot(info: DeviceInfo, requested_slot: Optional[str] = None) -> str:
    """Choose the inactive slot, or validate an explicitly requested target."""

    running = _slot_label(info.current_slot)
    if running not in ("A", "B"):
        raise OtaError("cannot determine current running slot")
    target = requested_slot.upper() if requested_slot is not None else ("B" if running == "A" else "A")
    if target == running:
        raise OtaError(f"target slot {target} is currently executing")
    return target


def next_version(info: DeviceInfo) -> int:
    maximum = max(info.slot_a_version, info.slot_b_version)
    if maximum == 0xFFFFFFFF:
        raise OtaError("cannot increment firmware version beyond uint32 maximum")
    return maximum + 1


def send_and_wait_ack(serial_port: Any, frame: bytes, seq: int, timeout: float) -> Response:
    _write_frame(serial_port, frame)
    return wait_response(serial_port, seq, timeout)


def _raise_response_error(response: Response, seq: int, offset: Optional[int] = None) -> None:
    if response.kind == "NACK":
        details = f"NACK received\nseq = {seq}"
    elif response.kind == "TIMEOUT":
        details = f"ACK timeout\nexpected seq = {seq}"
    else:
        details = response.error or "serial/protocol error"
        details = f"{details}\nexpected seq = {seq}"
    if offset is not None:
        details += f"\noffset = 0x{offset:08X}"
    raise OtaError(details)


def _require_ack(serial_port: Any, frame: bytes, seq: int, timeout: float, offset: Optional[int] = None) -> None:
    response = send_and_wait_ack(serial_port, frame, seq, timeout)
    if response.kind != "ACK":
        _raise_response_error(response, seq, offset)


def prepare_firmware(
    project_root: Path,
    slot: str,
    version: int,
    bin_path: Optional[str] = None,
    axf_path: Optional[str] = None,
    fromelf_path: Optional[str] = None,
) -> FirmwareInfo:
    """Resolve the BIN source according to --bin, --axf, then auto-discovery."""

    slot = slot.upper()
    if bin_path is not None:
        # --bin is the highest-priority source and must not invoke fromelf.
        selected_bin = Path(bin_path).expanduser().resolve()
        return analyze_firmware(selected_bin, version, slot)

    selected_axf = select_axf(project_root, slot, axf_path)
    selected_fromelf = resolve_fromelf(fromelf_path)
    generated_bin = generate_bin(selected_axf, slot, selected_fromelf)
    return analyze_firmware(
        generated_bin,
        version,
        slot,
        source_axf=selected_axf,
        generated_bin=generated_bin,
    )


def prepare_target_firmware(project_root: Path, target_slot: str, version: int) -> FirmwareInfo:
    """Use info.ciallo when present; only absent packages may fall back to AXF."""

    project_root = project_root.resolve()
    package_path = project_root / "info.ciallo"
    if package_path.exists():
        if not package_path.is_file():
            raise OtaError(f"info.ciallo is not a regular file:\n{package_path}")
        return load_ciallo(package_path, target_slot, version)

    selected_axf = select_axf(project_root, target_slot)
    selected_fromelf = resolve_fromelf()
    generated_bin = generate_bin(selected_axf, target_slot, selected_fromelf)
    return analyze_firmware(
        generated_bin,
        version,
        target_slot,
        source_axf=selected_axf,
        generated_bin=generated_bin,
    )


def open_serial(port: str) -> Any:
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise OtaError("pyserial 未安装，请先执行：pip install pyserial") from exc

    try:
        serial_port = serial.Serial(
            port=port,
            baudrate=SERIAL_BAUD,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=0.05,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
    except Exception as exc:
        raise OtaError(f"cannot open serial port {port}: {exc}") from exc

    # DTR/RTS 仅尽力拉低；部分 USB-UART 驱动不允许写属性，不能因此中止。
    for name in ("dtr", "rts"):
        try:
            setattr(serial_port, name, False)
        except Exception:
            pass
    try:
        serial_port.reset_input_buffer()
        serial_port.reset_output_buffer()
    except Exception as exc:
        serial_port.close()
        raise OtaError(f"cannot reset serial buffers: {exc}") from exc
    return serial_port


def run_ota_on_serial(info: FirmwareInfo, serial_port: Any, timeout: float) -> None:
    # 严格 stop-and-wait：当前帧未收到同序号 ACK 前，绝不发送下一帧。
    begin = build_begin_frame(info.version, info.size, info.crc32)
    _require_ack(serial_port, begin, 0, timeout)
    print("BEGIN ... ACK")

    sent_size = 0
    for packet_index in range(info.packet_count):
        offset = packet_index * MAX_DATA_SIZE
        chunk = info.data[offset : offset + MAX_DATA_SIZE]
        seq = packet_index + 1
        frame = build_data_frame(seq, offset, chunk)
        _require_ack(serial_port, frame, seq, timeout, offset)
        sent_size += len(chunk)
        print(f"[{seq:3d}/{info.packet_count}] {sent_size} / {info.size} bytes")

    end_seq = info.packet_count + 1
    _require_ack(serial_port, build_end_frame(end_seq), end_seq, timeout)
    print("END ... ACK")
    print("OTA SUCCESS")
    print(f"{sent_size} / {info.size} bytes sent")
    print(f"CRC32 = 0x{info.crc32:08X}")


def run_ota(info: FirmwareInfo, port: str, timeout: float) -> None:
    """Compatibility wrapper for sending a prepared image at fixed 38400."""

    print_firmware_info(info)
    serial_port = open_serial(port)
    try:
        run_ota_on_serial(info, serial_port, timeout)
    finally:
        serial_port.close()


def run_query_info(port: str) -> None:
    """Query the device only; this path never builds or sends OTA data."""

    serial_port = open_serial(port)
    try:
        info = query_device_info(serial_port, RESPONSE_TIMEOUT)
        print_device_info(info)
    finally:
        serial_port.close()


def run_normal_ota(project_root: Path, port: str, requested_slot: Optional[str]) -> None:
    """Query first, choose the inactive slot/version, then run OTA."""

    serial_port = open_serial(port)
    try:
        device_info = query_device_info(serial_port, RESPONSE_TIMEOUT)
        print_device_info(device_info)
        target_slot = determine_target_slot(device_info, requested_slot)
        version = next_version(device_info)
        firmware = prepare_target_firmware(project_root, target_slot, version)

        print()
        print("Upgrade:")
        print(f"  running slot = {_slot_label(device_info.current_slot)}")
        print(f"  target slot  = {target_slot}")
        print(f"  new version  = {version}")
        if firmware.source_ciallo is not None:
            print(f"  source       = {firmware.source_ciallo}")
        else:
            print("  source       = AXF -> generated BIN")
        print()
        print_firmware_info(firmware)
        run_ota_on_serial(firmware, serial_port, RESPONSE_TIMEOUT)
    finally:
        serial_port.close()


class _MemorySerial:
    """Small serial-like source used only by the offline self-test."""

    def __init__(self, data: bytes, chunk_size: int = 3) -> None:
        self._data = data
        self._chunk_size = chunk_size
        self.writes: list[bytes] = []

    def read(self, size: int) -> bytes:
        if not self._data:
            return b""
        count = min(size, self._chunk_size, len(self._data))
        result, self._data = self._data[:count], self._data[count:]
        return result

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)

    def flush(self) -> None:
        return None


def run_self_test() -> None:
    """Run protocol and validation checks without importing or opening pyserial."""

    assert SERIAL_BAUD == 38400
    assert build_frame(RSP_ACK, 0, b"").hex(" ").upper() == "AA 55 80 00 00 00 00 DC 33"
    assert build_frame(RSP_ACK, 1, b"").hex(" ").upper() == "AA 55 80 01 00 00 00 68 45"
    assert build_frame(CMD_QUERY_INFO, 0, b"").hex(" ").upper() == "AA 55 05 00 00 00 00 5B 32"

    with tempfile.TemporaryDirectory(prefix="ota_send_selftest_") as temp_dir:
        root = Path(temp_dir)
        synthetic_a = bytearray(34644)
        synthetic_b = bytearray(34676)
        struct.pack_into("<II", synthetic_a, 0, 0x20003CA0, 0x08004215)
        struct.pack_into("<II", synthetic_b, 0, 0x20003C60, 0x08011A15)
        temp_path = root / "Application_A.bin"
        temp_path.write_bytes(synthetic_a)
        b_path = root / "Application_B.bin"
        b_path.write_bytes(synthetic_b)
        info = validate_firmware(temp_path, 0, "A")
        assert info.size == 34644
        assert info.packet_count == 136
        assert len(info.data[-84:]) == 84
        assert info.last_packet_size == 84
        assert info.packet_count + 1 == 137
        assert info.reset_handler == 0x08004215
        b_info = validate_firmware(b_path, 0, "B")
        assert b_info.linked_slot == "B"
        assert detect_linked_slot(0x08004215) == "A"
        assert detect_linked_slot(0x08011A15) == "B"
        assert detect_linked_slot(0x08004214) == "UNKNOWN"

        bad_slot = bytearray(synthetic_a)
        struct.pack_into("<I", bad_slot, 4, 0x08011A15)
        bad_path = root / "wrong_slot.bin"
        bad_path.write_bytes(bad_slot)
        try:
            analyze_firmware(bad_path, 4, "A")
        except OtaError as exc:
            assert "firmware slot mismatch" in str(exc)
            assert "linked slot    = B" in str(exc)
        else:
            raise AssertionError("slot mismatch was not rejected")

        last_data = build_data_frame(136, 135 * 256, info.data[135 * 256 :])
        assert len(parse_frame(last_data).payload[4:]) == 84

        ack = build_frame(RSP_ACK, 42, b"")
        stream = _MemorySerial(b"noise\xAA" + ack, chunk_size=2)
        response = wait_response(stream, 42, 1.0)
        assert response.kind == "ACK"

        bad_ack = bytearray(build_frame(RSP_ACK, 0, b""))
        bad_ack[-1] ^= 0x01
        bad_response = wait_response(_MemorySerial(bytes(bad_ack)), 0, 1.0)
        assert bad_response.kind == "ERROR"
        assert "CRC16 mismatch" in (bad_response.error or "")

        device_payload = struct.pack(
            "<11I",
            2,
            3,
            4,
            27776,
            0x89B17743,
            0,
            3,
            5,
            34676,
            0xE5483854,
            0,
        )
        info_response = build_frame(RSP_INFO, 0, device_payload)
        info_serial = _MemorySerial(info_response, chunk_size=2)
        device_info = query_device_info(info_serial, 1.0)
        assert device_info.current_slot == 2
        assert device_info.slot_a_version == 4
        assert device_info.slot_b_version == 5
        assert info_serial.writes == [build_frame(CMD_QUERY_INFO, 0, b"")]
        bad_info = bytearray(info_response)
        bad_info[-1] ^= 0x01
        bad_info_response = wait_response(_MemorySerial(bytes(bad_info)), 0, 1.0)
        assert bad_info_response.kind == "ERROR"
        assert "CRC16 mismatch" in (bad_info_response.error or "")
        assert determine_target_slot(device_info) == "A"
        assert determine_target_slot(device_info, "A") == "A"
        try:
            determine_target_slot(device_info, "B")
        except OtaError as exc:
            assert "currently executing" in str(exc)
        else:
            raise AssertionError("running slot was accepted as the target")
        assert next_version(device_info) == 6
        overflow_info = DeviceInfo(1, 3, 0xFFFFFFFF, 0, 0, 0, 3, 0, 0, 0, 0)
        try:
            next_version(overflow_info)
        except OtaError as exc:
            assert "uint32 maximum" in str(exc)
        else:
            raise AssertionError("uint32 version overflow was not rejected")

        ciallo_path = root / "info.ciallo"
        write_ciallo_package(ciallo_path, {"A": info, "B": b_info})
        loaded = load_ciallo(ciallo_path, "B", 6)
        assert loaded.source_ciallo == ciallo_path.resolve()
        assert loaded.version == 6
        assert loaded.linked_slot == "B"
        assert loaded.data == bytes(synthetic_b)
        package_info = prepare_target_firmware(root, "A", 6)
        assert package_info.source_ciallo == ciallo_path.resolve()

        corrupt_path = root / "corrupt.ciallo"
        with zipfile.ZipFile(ciallo_path, "r") as source_package:
            manifest_bytes = source_package.read("manifest.json")
            a_bytes = source_package.read("Application_A.bin")
            b_bytes = bytearray(source_package.read("Application_B.bin"))
        b_bytes[-1] ^= 0x01
        with zipfile.ZipFile(corrupt_path, "w", compression=zipfile.ZIP_DEFLATED) as corrupt_package:
            corrupt_package.writestr("manifest.json", manifest_bytes)
            corrupt_package.writestr("Application_A.bin", a_bytes)
            corrupt_package.writestr("Application_B.bin", b_bytes)
        try:
            load_ciallo(corrupt_path, "B", 6)
        except OtaError as exc:
            assert "CRC32 mismatch" in str(exc) or "SHA256 mismatch" in str(exc)
        else:
            raise AssertionError("corrupt ciallo package was accepted")

        axf_root = root / "axf_project"
        (axf_root / "MDK-ARM" / "Objects" / "ApplicationA").mkdir(parents=True)
        (axf_root / "MDK-ARM" / "Objects" / "ApplicationB").mkdir(parents=True)
        (axf_root / "MDK-ARM" / "Objects" / "Bootloader").mkdir(parents=True)
        axf_a = axf_root / "MDK-ARM" / "Objects" / "ApplicationA" / "yxj.axf"
        axf_b = axf_root / "MDK-ARM" / "Objects" / "ApplicationB" / "yxj.axf"
        boot_axf = axf_root / "MDK-ARM" / "Objects" / "Bootloader" / "yxj.axf"
        axf_a.write_bytes(b"")
        axf_b.write_bytes(b"")
        boot_axf.write_bytes(b"")
        assert select_axf(axf_root, "A") == axf_a.resolve()
        assert select_axf(axf_root, "B") == axf_b.resolve()
        assert boot_axf.resolve() not in find_axf_files(axf_root)

    print("offline self-test: PASS")


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="PY32F071 stop-and-wait OTA sender")
    parser.add_argument("--port", help="serial port, for example COM7")
    parser.add_argument("--slot", type=lambda value: value.upper(), choices=("A", "B"))
    parser.add_argument(
        "--query-info",
        action="store_true",
        help="query device and slot information only",
    )
    parser.add_argument(
        "--ciallo",
        action="store_true",
        help="build info.ciallo without opening a serial port",
    )
    parser.add_argument("--self-test", action="store_true", help=argparse.SUPPRESS)
    return parser


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = build_arg_parser()
    args = parser.parse_args(argv)
    if args.self_test:
        run_self_test()
        return 0

    try:
        if args.ciallo:
            if args.port is not None or args.query_info or args.slot is not None:
                parser.error("--ciallo cannot be combined with --port, --query-info, or --slot")
            build_ciallo(Path(__file__).resolve().parent)
            return 0

        if args.query_info:
            if args.port is None:
                parser.error("--query-info requires --port")
            if args.slot is not None:
                parser.error("--query-info cannot be combined with --slot")
            run_query_info(args.port)
            return 0

        if args.port is None:
            parser.error("normal OTA requires --port")
        run_normal_ota(Path(__file__).resolve().parent, args.port, args.slot)
    except KeyboardInterrupt:
        print("OTA cancelled by user.")
        return 130
    except OtaError as exc:
        print(f"ERROR:\n{exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
