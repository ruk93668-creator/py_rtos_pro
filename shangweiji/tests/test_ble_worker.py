from __future__ import annotations

import unittest
from types import SimpleNamespace

from roxy_ota.gui.ble_worker import analyze_gatt_services, normalize_property


class BleGattAnalysisTests(unittest.TestCase):
    def test_property_normalization(self) -> None:
        self.assertEqual(normalize_property("write-without-response"), "write-without-response")
        self.assertEqual(normalize_property("CharacteristicFlags.NOTIFY"), "notify")

    def test_unique_uart_hints_are_reported(self) -> None:
        services = [
            SimpleNamespace(
                uuid="service-1",
                characteristics=[
                    SimpleNamespace(uuid="write-1", properties=["write-without-response"]),
                    SimpleNamespace(uuid="notify-1", properties=["notify"]),
                ],
            )
        ]
        result = analyze_gatt_services(services)
        self.assertEqual(result["write_uuid"], "write-1")
        self.assertEqual(result["notify_uuid"], "notify-1")
        self.assertEqual(result["services"][0]["characteristics"][0]["properties"], ["write-without-response"])

    def test_non_unique_hints_are_left_unresolved(self) -> None:
        services = [
            SimpleNamespace(
                uuid="service-1",
                characteristics=[
                    SimpleNamespace(uuid="write-1", properties=["write"]),
                    SimpleNamespace(uuid="write-2", properties=["write"]),
                    SimpleNamespace(uuid="notify-1", properties=["notify"]),
                    SimpleNamespace(uuid="notify-2", properties=["notify"]),
                ],
            )
        ]
        result = analyze_gatt_services(services)
        self.assertIsNone(result["write_uuid"])
        self.assertIsNone(result["notify_uuid"])


if __name__ == "__main__":
    unittest.main()
