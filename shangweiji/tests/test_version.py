from __future__ import annotations

import unittest

from roxy_ota.version import APP_DISPLAY_NAME, APP_NAME, APP_VERSION


class VersionTests(unittest.TestCase):
    def test_single_source_application_identity(self) -> None:
        self.assertEqual(APP_NAME, "Roxy OTA Tool")
        self.assertEqual(APP_VERSION, "1.0.0")
        self.assertEqual(APP_DISPLAY_NAME, "Roxy OTA Tool v1.0.0")


if __name__ == "__main__":
    unittest.main()
