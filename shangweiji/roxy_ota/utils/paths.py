"""Resolve files relative to the application, independently of the CWD."""

from __future__ import annotations

import sys
from pathlib import Path


def get_app_dir() -> Path:
    """Return the directory containing the script or frozen executable."""

    if getattr(sys, "frozen", False):
        return Path(sys.executable).resolve().parent
    # paths.py is shangweiji/roxy_ota/utils/paths.py.
    return Path(__file__).resolve().parents[2]


def resource_path(relative_path: str | Path) -> Path:
    """Return a bundled resource path in source and PyInstaller modes."""

    bundle_root = getattr(sys, "_MEIPASS", None)
    base = Path(bundle_root) if bundle_root else get_app_dir()
    return base / Path(relative_path)


def external_package_path(filename: str = "info.ciallo") -> Path:
    """Return the package location beside the script or executable."""

    return get_app_dir() / filename


def find_external_package(filename: str = "info.ciallo") -> Path | None:
    """Find a regular external package without touching or copying it."""

    candidate = external_package_path(filename)
    return candidate if candidate.is_file() else None
