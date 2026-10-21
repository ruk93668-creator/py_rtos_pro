"""The validated Roxy OTA wire-format core used by the GUI.

This module intentionally contains no PySide6 or window code.  The frame
format and CRC implementation mirror the already validated CLI sender:
QUERY_INFO remains AA 55 05 00 00 00 00 5B 32.
"""

from __future__ import annotations

import struct
import time
from dataclasses import dataclass
from typing import Any, Optional


SOF = b"\xAA\x55"
FRAME_HEADER_SIZE = 7
FRAME_CRC_SIZE = 2
FRAME_MIN_SIZE = FRAME_HEADER_SIZE + FRAME_CRC_SIZE
MAX_DATA_SIZE = 256
DATA_OFFSET_SIZE = 4
MAX_PAYLOAD_SIZE = DATA_OFFSET_SIZE + MAX_DATA_SIZE

CMD_BEGIN = 0x01
CMD_DATA = 0x02
CMD_END = 0x03
CMD_ABORT = 0x04
CMD_QUERY_INFO = 0x05
RSP_ACK = 0x80
RSP_NACK = 0x81
RSP_INFO = 0x82


class OtaError(Exception):
    """Expected protocol or transport-facing error."""


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

    body = bytes([frame_type]) + struct.pack("<HH", seq, len(payload)) + payload
    return SOF + body + struct.pack("<H", crc16_ccitt_false(body))


def build_begin_frame(version: int, firmware_size: int, firmware_crc32: int) -> bytes:
    """Build the existing BEGIN payload: version, size, CRC32 (uint32 LE)."""

    values = (version, firmware_size, firmware_crc32)
    if any(not 0 <= value <= 0xFFFFFFFF for value in values):
        raise FrameError("BEGIN fields must be uint32 values")
    return build_frame(CMD_BEGIN, 0, struct.pack("<III", *values))


def build_data_frame(seq: int, offset: int, data: bytes) -> bytes:
    """Build one existing DATA frame with a uint32 byte offset."""

    if not 0 <= offset <= 0xFFFFFFFF:
        raise FrameError(f"DATA offset out of uint32 range: {offset}")
    if not 0 < len(data) <= MAX_DATA_SIZE:
        raise FrameError(
            f"DATA length must be 1..{MAX_DATA_SIZE}, got {len(data)}"
        )
    return build_frame(CMD_DATA, seq, struct.pack("<I", offset) + data)


def build_end_frame(seq: int) -> bytes:
    """Build the existing empty-payload END frame."""

    return build_frame(CMD_END, seq, b"")


def parse_frame(raw: bytes) -> OtaFrame:
    """Validate one complete frame and return its decoded fields."""

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
                # Preserve a trailing AA in case 55 arrives in the next read.
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
                del self._buffer[0]
                continue

            total_length = FRAME_HEADER_SIZE + payload_length + FRAME_CRC_SIZE
            if len(self._buffer) < total_length:
                break

            candidate = bytes(self._buffer[:total_length])
            try:
                frame = parse_frame(candidate)
            except FrameError as exc:
                # A response CRC error is actionable and must stop the query.
                if len(candidate) >= FRAME_HEADER_SIZE and candidate[2] in (
                    RSP_ACK,
                    RSP_NACK,
                    RSP_INFO,
                ):
                    events.append((None, exc))
                del self._buffer[0]
                continue

            del self._buffer[:total_length]
            events.append((frame, None))

        return events


def receive_frame(serial_port: Any, timeout: float) -> OtaFrame:
    """Read one complete frame without assuming read() returns all bytes."""

    parser = FrameStreamParser()
    deadline = time.monotonic() + timeout
    while True:
        if deadline - time.monotonic() <= 0:
            raise TimeoutError("response timeout")
        chunk = serial_port.read(64)
        if not chunk:
            continue
        for frame, error in parser.feed(bytes(chunk)):
            if error is not None:
                raise error
            if frame is not None:
                return frame


def wait_response(serial_port: Any, expected_seq: int, timeout: float) -> Response:
    """Decode a response while enforcing its sequence number."""

    try:
        frame = receive_frame(serial_port, timeout)
    except TimeoutError as exc:
        return Response("TIMEOUT", error=str(exc))
    except FrameError as exc:
        return Response("ERROR", error=str(exc))
    except Exception as exc:  # pragma: no cover - defensive serial boundary
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
