# -*- mode: python ; coding: utf-8 -*-
"""PyInstaller onedir build for the Roxy OTA GUI."""

from pathlib import Path


# PyInstaller executes spec files without defining ``__file__``.
SPEC_DIR = Path(SPECPATH).resolve()
ENTRYPOINT = SPEC_DIR / "roxy_ota_gui.py"
BACKGROUND = SPEC_DIR / "e221ec38138fda53cbb4bc72ccbd3b5e.jpg"

a = Analysis(
    [str(ENTRYPOINT)],
    pathex=[str(SPEC_DIR)],
    binaries=[],
    datas=[(str(BACKGROUND), ".")],
    hiddenimports=[
        "PySide6.QtCore",
        "PySide6.QtGui",
        "PySide6.QtWidgets",
        "serial.tools.list_ports",
        "bleak",
        "bleak.backends.winrt",
        "bleak.backends.winrt.client",
        "bleak.backends.winrt.scanner",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[str(SPEC_DIR / "pyi_rth_roxy.py")],
    excludes=[],
    noarchive=False,
    optimize=0,
)

# The build machine exposes a Poppler ICU DLL on PATH.  It exports suffixed
# ICU-78 symbols, while Qt6Core expects the Windows ICU ABI; keeping that
# unrelated DLL beside the executable makes QtWidgets fail to import.  Let
# Windows resolve its own ICU implementation instead.
_CONFLICTING_ICU = {"icuuc.dll", "icudt78.dll"}
a.binaries = [
    item for item in a.binaries if Path(item[0]).name.lower() not in _CONFLICTING_ICU
]
a.datas = [
    item for item in a.datas if Path(item[0]).name.lower() not in _CONFLICTING_ICU
]

pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name="RoxyOTA",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=False,
    console=False,
)

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=False,
    upx_exclude=[],
    name="RoxyOTA",
)
