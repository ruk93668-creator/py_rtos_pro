"""Stop-and-wait OTA sender for the GUI.

The module deliberately has no PySide6 dependency.  It sends the same
BEGIN/DATA/END frames as the validated CLI and never retries a frame.
"""

from __future__ import annotations

import zlib
from dataclasses import dataclass
from typing import Any, Callable, Optional

from .device import DeviceInfo, inactive_slot, next_version, slot_name
from .protocol import (
    MAX_DATA_SIZE,
    OtaError,
    Response,
    build_begin_frame,
    build_data_frame,
    build_end_frame,
)


ProgressCallback = Callable[[int, int], None]
StatusCallback = Callable[[str], None]
LogCallback = Callable[[str], None]


@dataclass(frozen=True)
class OtaPlan:
    running_slot: str
    target_slot: str
    version: int
    image: Any


@dataclass(frozen=True)
class OtaResult:
    target_slot: str
    version: int
    size: int
    crc32: int
    packet_count: int
    end_seq: int
    sent_bytes: int


def make_ota_plan(device_info: DeviceInfo, image: Any) -> OtaPlan:
    """Recompute running slot, target slot and version from fresh INFO."""

    running_slot = slot_name(device_info.current_slot)
    target_slot = inactive_slot(device_info.current_slot)
    if target_slot is None:
        raise OtaError("Invalid running slot")
    if target_slot == running_slot:
        raise OtaError(f"target slot {target_slot} is currently executing")
    if image is None:
        raise OtaError(f"no firmware image is available for Slot {target_slot}")
    if getattr(image, "slot", None) != target_slot:
        raise OtaError(
            f"firmware image slot mismatch: target={target_slot}, "
            f"image={getattr(image, 'slot', None)!r}"
        )
    if getattr(image, "linked_slot", None) != target_slot:
        raise OtaError(
            f"firmware linked slot mismatch: target={target_slot}, "
            f"linked={getattr(image, 'linked_slot', None)!r}"
        )
    version = next_version(device_info.slot_a.version, device_info.slot_b.version)
    return OtaPlan(running_slot, target_slot, version, image)


class OtaUpdater:
    """Send one fixed firmware image over an already-open transport."""

    def __init__(
        self,
        transport: Any,
        image: Any,
        target_slot: str,
        version: int,
        timeout: float = 10.0,
    ) -> None:
        self.transport = transport
        self.image = image
        self.target_slot = target_slot.upper()
        self.version = version
        self.timeout = timeout
        self._validate_inputs()

    def _validate_inputs(self) -> None:
        if self.target_slot not in ("A", "B"):
            raise OtaError(f"invalid target slot: {self.target_slot!r}")
        if getattr(self.image, "slot", None) != self.target_slot:
            raise OtaError(
                f"firmware image slot mismatch: target={self.target_slot}, "
                f"image={getattr(self.image, 'slot', None)!r}"
            )
        if getattr(self.image, "linked_slot", None) != self.target_slot:
            raise OtaError(
                f"firmware linked slot mismatch: target={self.target_slot}, "
                f"linked={getattr(self.image, 'linked_slot', None)!r}"
            )
        data = bytes(getattr(self.image, "data", b""))
        size = getattr(self.image, "size", len(data))
        if size != len(data) or size <= 0:
            raise OtaError("firmware image size is invalid")
        crc32 = getattr(self.image, "crc32", None)
        if crc32 != (zlib.crc32(data) & 0xFFFFFFFF):
            raise OtaError("firmware image CRC32 does not match its data")
        if not 0 <= self.version <= 0xFFFFFFFF:
            raise OtaError("firmware version is outside uint32 range")

    def _wait_ack(self, frame: bytes, seq: int, stage: str) -> None:
        response: Response = self.transport.send_frame_and_wait_response(
            frame, seq, self.timeout
        )
        if response.kind == "ACK":
            if response.frame is None or response.frame.seq != seq:
                raise OtaError(f"Unexpected ACK sequence at seq {seq}")
            return
        if response.kind == "TIMEOUT":
            if stage == "DATA":
                raise OtaError(f"DATA timeout at seq {seq}")
            raise OtaError(f"{stage} timeout")
        if response.kind == "NACK":
            if stage == "DATA":
                raise OtaError(f"DATA NACK at seq {seq}")
            raise OtaError(f"{stage} NACK")
        error = response.error or f"unexpected response during {stage}"
        if "sequence mismatch" in error.lower():
            raise OtaError(f"Unexpected ACK sequence at seq {seq}")
        raise OtaError(f"{stage} response error: {error}")

    def perform(
        self,
        progress_callback: Optional[ProgressCallback] = None,
        status_callback: Optional[StatusCallback] = None,
        log_callback: Optional[LogCallback] = None,
    ) -> OtaResult:
        """Perform BEGIN -> DATA -> END once, with no automatic retry."""

        def status(value: str) -> None:
            if status_callback is not None:
                status_callback(value)

        def log(value: str) -> None:
            if log_callback is not None:
                log_callback(value)

        data = bytes(self.image.data)
        total_size = len(data)
        packet_count = (total_size + MAX_DATA_SIZE - 1) // MAX_DATA_SIZE
        if packet_count + 1 > 0xFFFF:
            raise OtaError("firmware requires too many DATA sequence numbers")

        status("Preparing...")
        if progress_callback is not None:
            progress_callback(0, total_size)

        status("Sending BEGIN...")
        begin = build_begin_frame(self.version, total_size, self.image.crc32)
        self._wait_ack(begin, 0, "BEGIN")
        log("BEGIN ... ACK")

        status("Uploading...")
        log("Upload started")
        sent_bytes = 0
        for packet_index in range(packet_count):
            offset = packet_index * MAX_DATA_SIZE
            chunk = data[offset : offset + MAX_DATA_SIZE]
            seq = packet_index + 1
            frame = build_data_frame(seq, offset, chunk)
            self._wait_ack(frame, seq, "DATA")
            # Progress advances only after the corresponding DATA ACK.
            sent_bytes += len(chunk)
            if progress_callback is not None:
                progress_callback(sent_bytes, total_size)

        status("Finishing...")
        end_seq = packet_count + 1
        self._wait_ack(build_end_frame(end_seq), end_seq, "END")
        log("END ... ACK")
        return OtaResult(
            target_slot=self.target_slot,
            version=self.version,
            size=total_size,
            crc32=self.image.crc32,
            packet_count=packet_count,
            end_seq=end_seq,
            sent_bytes=sent_bytes,
        )

