from __future__ import annotations

import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from roxy_ota.utils import find_external_package, get_app_dir, resource_path
from roxy_ota.utils import paths


class PathHelperTests(unittest.TestCase):
    def test_source_app_dir_is_project_directory(self) -> None:
        self.assertEqual(get_app_dir(), Path(__file__).resolve().parents[1])

    def test_frozen_app_dir_is_next_to_executable(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            executable = Path(tmp) / "RoxyOTA.exe"
            with patch.object(paths.sys, "frozen", True, create=True), patch.object(
                paths.sys, "executable", str(executable)
            ):
                self.assertEqual(get_app_dir(), executable.parent.resolve())

    def test_source_resource_is_next_to_entrypoint(self) -> None:
        expected = Path(__file__).resolve().parents[1] / "e221ec38138fda53cbb4bc72ccbd3b5e.jpg"
        self.assertEqual(resource_path(expected.name), expected)

    def test_frozen_resource_uses_meipass(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            with patch.object(paths.sys, "_MEIPASS", tmp, create=True):
                self.assertEqual(resource_path("background.jpg"), Path(tmp) / "background.jpg")

    def test_external_package_discovery_present_and_absent(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            app_dir = Path(tmp)
            with patch.object(paths, "get_app_dir", return_value=app_dir):
                self.assertIsNone(find_external_package())
                package = app_dir / "info.ciallo"
                package.write_bytes(b"test")
                self.assertEqual(find_external_package(), package)


if __name__ == "__main__":
    unittest.main()
