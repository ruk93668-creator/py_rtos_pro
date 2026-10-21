from __future__ import annotations

import importlib.util
import struct
import sys
import unittest
from pathlib import Path

from roxy_ota.core.device import DeviceInfo, SlotInfo
from roxy_ota.core.ota_updater import OtaUpdater, make_ota_plan
from roxy_ota.core.protocol import OtaError, OtaFrame, Response, RSP_ACK
from roxy_ota.firmware.ciallo import validate_firmware_bytes


def _load_legacy_cli():
    path = Path(__file__).resolve().parents[2] / "roxy_ota_send.py"
    spec = importlib.util.spec_from_file_location("legacy_roxy_ota_send", path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"cannot load legacy CLI: {path}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def _image_b(size: int = 300):
    data = bytearray(size)
    struct.pack_into("<II", data, 0, 0x20003EE0, 0x08011A15)
    for index in range(8, size):
        data[index] = index & 0xFF
    return validate_firmware_bytes(bytes(data), "B")


class ScriptedTransport:
    def __init__(self, outcomes=None) -> None:
        self.outcomes = list(outcomes or [])
        self.frames: list[bytes] = []

    def send_frame_and_wait_response(self, frame, expected_seq, timeout):
        self.frames.append(bytes(frame))
        if self.outcomes:
            return self.outcomes.pop(0)
        return Response("ACK", OtaFrame(RSP_ACK, expected_seq, b""))


def _ack(seq: int) -> Response:
    return Response("ACK", OtaFrame(RSP_ACK, seq, b""))


class OtaUpdaterTests(unittest.TestCase):
    def test_plan_recomputes_inactive_slot_and_version(self) -> None:
        image_b = _image_b()
        info_a = DeviceInfo(1, SlotInfo(3, 9, 1, 0, 0), SlotInfo(3, 8, 1, 0, 0))
        plan_b = make_ota_plan(info_a, image_b)
        self.assertEqual(plan_b.running_slot, "A")
        self.assertEqual(plan_b.target_slot, "B")
        self.assertEqual(plan_b.version, 10)

        image_a_data = bytearray(image_b.data)
        struct.pack_into("<I", image_a_data, 4, 0x08004215)
        image_a = validate_firmware_bytes(bytes(image_a_data), "A")
        info_b = DeviceInfo(2, SlotInfo(3, 9, 1, 0, 0), SlotInfo(3, 8, 1, 0, 0))
        plan_a = make_ota_plan(info_b, image_a)
        self.assertEqual(plan_a.running_slot, "B")
        self.assertEqual(plan_a.target_slot, "A")

        with self.assertRaises(OtaError):
            make_ota_plan(info_a, image_a)
        overflow = DeviceInfo(
            1,
            SlotInfo(3, 0xFFFFFFFF, 1, 0, 0),
            SlotInfo(3, 8, 1, 0, 0),
        )
        with self.assertRaises(OtaError):
            make_ota_plan(overflow, image_b)

    def test_frames_match_legacy_cli_and_progress_follows_data_ack(self) -> None:
        legacy = _load_legacy_cli()
        image = _image_b()
        transport = ScriptedTransport()
        progress: list[tuple[int, int]] = []
        statuses: list[str] = []
        result = OtaUpdater(transport, image, "B", 7).perform(
            progress_callback=lambda sent, total: progress.append((sent, total)),
            status_callback=statuses.append,
        )

        self.assertEqual(result.packet_count, 2)
        self.assertEqual(result.end_seq, 3)
        self.assertEqual(result.sent_bytes, 300)
        self.assertEqual(progress, [(0, 300), (256, 300), (300, 300)])
        self.assertEqual(
            transport.frames,
            [
                legacy.build_begin_frame(7, 300, image.crc32),
                legacy.build_data_frame(1, 0, image.data[:256]),
                legacy.build_data_frame(2, 256, image.data[256:]),
                legacy.build_end_frame(3),
            ],
        )
        self.assertEqual(statuses, ["Preparing...", "Sending BEGIN...", "Uploading...", "Finishing..."])

    def test_data_nack_stops_without_retry(self) -> None:
        image = _image_b()
        transport = ScriptedTransport([_ack(0), Response("NACK", OtaFrame(0x81, 1, b""))])
        progress: list[tuple[int, int]] = []
        with self.assertRaisesRegex(OtaError, "DATA NACK at seq 1"):
            OtaUpdater(transport, image, "B", 7).perform(
                progress_callback=lambda sent, total: progress.append((sent, total))
            )
        self.assertEqual(len(transport.frames), 2)
        self.assertEqual(progress, [(0, 300)])

    def test_data_timeout_stops_without_retry(self) -> None:
        image = _image_b()
        transport = ScriptedTransport([_ack(0), Response("TIMEOUT", error="response timeout")])
        with self.assertRaisesRegex(OtaError, "DATA timeout at seq 1"):
            OtaUpdater(transport, image, "B", 7).perform()
        self.assertEqual(len(transport.frames), 2)

    def test_begin_failure_sends_no_data(self) -> None:
        image = _image_b()
        transport = ScriptedTransport([Response("TIMEOUT", error="response timeout")])
        with self.assertRaisesRegex(OtaError, "BEGIN timeout"):
            OtaUpdater(transport, image, "B", 7).perform()
        self.assertEqual(len(transport.frames), 1)

    def test_end_failure_is_not_success(self) -> None:
        image = _image_b()
        transport = ScriptedTransport(
            [_ack(0), _ack(1), _ack(2), Response("NACK", OtaFrame(0x81, 3, b""))]
        )
        with self.assertRaisesRegex(OtaError, "END NACK"):
            OtaUpdater(transport, image, "B", 7).perform()
        self.assertEqual(len(transport.frames), 4)

    def test_wrong_ack_sequence_fails(self) -> None:
        image = _image_b()
        transport = ScriptedTransport([_ack(0), _ack(99)])
        with self.assertRaisesRegex(OtaError, "Unexpected ACK sequence"):
            OtaUpdater(transport, image, "B", 7).perform()
        self.assertEqual(len(transport.frames), 2)


if __name__ == "__main__":
    unittest.main()
