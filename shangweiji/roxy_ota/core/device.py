"""Device metadata structures and INFO payload decoding."""

from __future__ import annotations

import struct
from dataclasses import dataclass

from .protocol import OtaError


INFO_PAYLOAD_SIZE = 44
UINT32_MAX = 0xFFFFFFFF

_SLOT_NAMES = {0: "NONE", 1: "A", 2: "B"}
_STATE_NAMES = {
    0: "EMPTY",
    1: "DOWNLOADING",
    2: "PENDING",
    3: "CONFIRMED",
    4: "BAD",
}


@dataclass(frozen=True)
class SlotInfo:
    state: int
    version: int
    size: int
    crc32: int
    boot_attempt: int


@dataclass(frozen=True)
class DeviceInfo:
    current_slot: int
    slot_a: SlotInfo
    slot_b: SlotInfo


def parse_device_info(payload: bytes) -> DeviceInfo:
    """Decode the fixed 44-byte little-endian INFO payload."""

    if len(payload) != INFO_PAYLOAD_SIZE:
        raise OtaError(
            f"INFO payload length mismatch: got {len(payload)}, "
            f"expected {INFO_PAYLOAD_SIZE}"
        )

    values = struct.unpack("<11I", payload)
    return DeviceInfo(
        current_slot=values[0],
        slot_a=SlotInfo(*values[1:6]),
        slot_b=SlotInfo(*values[6:11]),
    )


def slot_name(slot: int) -> str:
    """Return a display name for current_slot, preserving unknown values."""

    return _SLOT_NAMES.get(slot, f"UNKNOWN({slot})")


def state_name(state: int) -> str:
    """Return a display name for a slot state, preserving unknown values."""

    return _STATE_NAMES.get(state, f"UNKNOWN({state})")


def inactive_slot(current_slot: int) -> str | None:
    """Return the inactive slot, or None when the running slot is unknown."""

    if current_slot == 1:
        return "B"
    if current_slot == 2:
        return "A"
    return None


def next_version(slot_a_version: int, slot_b_version: int) -> int:
    """Calculate max(A, B) + 1 without uint32 wraparound."""

    maximum = max(slot_a_version, slot_b_version)
    if maximum == UINT32_MAX:
        raise OtaError("Version overflow")
    return maximum + 1
