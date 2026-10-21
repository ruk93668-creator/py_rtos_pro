"""Make PySide6 and shiboken6 DLLs discoverable in a frozen build."""

from __future__ import annotations

import os
import sys
import ctypes
from pathlib import Path


if sys.platform == "win32" and getattr(sys, "frozen", False):
    _bundle_root = Path(getattr(sys, "_MEIPASS", Path(sys.executable).parent))
    _dll_handles = []
    for _directory in (_bundle_root, _bundle_root / "PySide6", _bundle_root / "shiboken6"):
        if _directory.is_dir():
            os.environ["PATH"] = os.fspath(_directory) + os.pathsep + os.environ.get("PATH", "")
            try:
                _dll_handles.append(os.add_dll_directory(os.fspath(_directory)))
            except (AttributeError, OSError):
                pass
    # Load shiboken before any Qt extension.  This mirrors PySide6's own
    # startup order and avoids Windows resolving the ABI DLL from another Qt
    # installation on PATH.
    _shiboken_dll = _bundle_root / "shiboken6" / "shiboken6.abi3.dll"
    if _shiboken_dll.is_file():
        try:
            _dll_handles.append(ctypes.WinDLL(os.fspath(_shiboken_dll)))
        except OSError:
            pass
