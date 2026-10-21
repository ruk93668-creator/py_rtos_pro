"""Launch Roxy OTA Tool in source or PyInstaller frozen mode."""

from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    missing: list[str] = []
    try:
        from PySide6.QtWidgets import QApplication
    except ImportError as exc:
        missing.append(f"PySide6 ({exc})")
    try:
        import serial  # type: ignore  # noqa: F401
    except ImportError as exc:
        missing.append(f"pyserial ({exc})")
    try:
        import bleak  # type: ignore  # noqa: F401
    except ImportError as exc:
        missing.append(f"bleak ({exc})")

    if missing:
        print(
            "缺少依赖："
            + ", ".join(missing)
            + "。请在构建环境中安装：python -m pip install PySide6 pyserial bleak",
            file=sys.stderr,
        )
        return 1

    script_dir = Path(__file__).resolve().parent
    if str(script_dir) not in sys.path:
        sys.path.insert(0, str(script_dir))

    try:
        from roxy_ota.gui.main_window import MainWindow

        app = QApplication(sys.argv)
        window = MainWindow()
    except Exception as exc:
        # Keep startup failures visible to a user launching the GUI directly.
        try:
            from PySide6.QtWidgets import QMessageBox

            if "app" not in locals():
                app = QApplication(sys.argv)
            QMessageBox.critical(None, "Roxy OTA Tool", f"程序启动失败：\n{exc}")
        except Exception:
            print(f"Roxy OTA Tool 启动失败：{exc}", file=sys.stderr)
        return 1

    window.show()
    return app.exec()


if __name__ == "__main__":
    raise SystemExit(main())
