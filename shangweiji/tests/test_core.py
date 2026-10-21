from __future__ import annotations

import struct
import unittest

from roxy_ota.core import device, protocol
from roxy_ota.core.serial_transport import RoxyDevice


class MemorySerial:
    def __init__(self, data: bytes, chunk_size: int = 3) -> None:
        self._data = data
        self._chunk_size = chunk_size
        self.writes: list[bytes] = []
        self.closed = False

    def read(self, _size: int) -> bytes:
        if not self._data:
            return b""
        count = min(self._chunk_size, len(self._data))
        result = self._data[:count]
        self._data = self._data[count:]
        return result

    def write(self, data: bytes) -> int:
        self.writes.append(bytes(data))
        return len(data)

    def flush(self) -> None:
        return None


class CoreTests(unittest.TestCase):
    def test_query_frame_vector(self) -> None:
        self.assertEqual(
            protocol.build_frame(protocol.CMD_QUERY_INFO, 0, b"").hex(" ").upper(),
            "AA 55 05 00 00 00 00 5B 32",
        )

    def test_info_parse_and_names(self) -> None:
        payload = struct.pack(
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
        info = device.parse_device_info(payload)
        self.assertEqual(device.slot_name(info.current_slot), "B")
        self.assertEqual(device.state_name(info.slot_a.state), "CONFIRMED")
        self.assertEqual(info.slot_a.version, 4)
        self.assertEqual(info.slot_b.version, 5)

    def test_unknown_state_is_displayable(self) -> None:
        self.assertEqual(device.state_name(99), "UNKNOWN(99)")
        self.assertEqual(device.slot_name(99), "UNKNOWN(99)")

    def test_upgrade_target_and_version_rules(self) -> None:
        self.assertEqual(device.inactive_slot(1), "B")
        self.assertEqual(device.inactive_slot(2), "A")
        self.assertIsNone(device.inactive_slot(0))
        self.assertEqual(device.next_version(9, 8), 10)
        with self.assertRaises(protocol.OtaError):
            device.next_version(0xFFFFFFFF, 8)

    def test_info_size_must_be_exact(self) -> None:
        with self.assertRaises(protocol.OtaError):
            device.parse_device_info(b"\x00" * 43)
        with self.assertRaises(protocol.OtaError):
            device.parse_device_info(b"\x00" * 45)

    def test_bad_frame_crc_is_rejected(self) -> None:
        frame = bytearray(protocol.build_frame(protocol.RSP_INFO, 0, b"\x00" * 44))
        frame[-1] ^= 0x01
        with self.assertRaises(protocol.FrameError):
            protocol.parse_frame(bytes(frame))

    def test_stream_parser_handles_split_info(self) -> None:
        payload = b"\x00" * 44
        parser = protocol.FrameStreamParser()
        frame = protocol.build_frame(protocol.RSP_INFO, 0, payload)
        events = []
        for offset in range(0, len(frame), 2):
            events.extend(parser.feed(frame[offset : offset + 2]))
        decoded = [item for item, error in events if item is not None and error is None]
        self.assertEqual(len(decoded), 1)
        self.assertEqual(decoded[0].payload, payload)

    def test_transport_query_reuses_frame_and_parser(self) -> None:
        payload = struct.pack(
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
        serial = MemorySerial(protocol.build_frame(protocol.RSP_INFO, 0, payload), 2)
        device_handle = RoxyDevice("TEST")
        device_handle._serial = serial
        info = device_handle.query_info(timeout=1.0)
        self.assertEqual(info.current_slot, 2)
        self.assertEqual(serial.writes, [protocol.build_frame(protocol.CMD_QUERY_INFO, 0, b"")])


if __name__ == "__main__":
    unittest.main()
