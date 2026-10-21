"""Fixed-38400 serial transport for QUERY_INFO."""

from __future__ import annotations

from typing import Any

from .device import DeviceInfo, parse_device_info
from .protocol import (
    CMD_QUERY_INFO,
    RSP_ACK,
    RSP_INFO,
    RSP_NACK,
    OtaError,
    Response,
    build_frame,
    wait_response,
)


BAUDRATE = 38400


class SerialTransportError(OtaError):
    """A serial port could not be opened or used."""


def _import_serial() -> Any:
    try:
        import serial  # type: ignore
    except ImportError as exc:
        raise SerialTransportError(
            "未安装 pyserial，请运行：python -m pip install PySide6 pyserial"
        ) from exc
    return serial


def list_serial_ports() -> list[Any]:
    """Return available pyserial port descriptors, sorted by device name."""

    _import_serial()
    try:
        from serial.tools import list_ports  # type: ignore

        ports = list(list_ports.comports())
    except Exception as exc:  # pragma: no cover - depends on host drivers
        raise SerialTransportError(f"cannot enumerate serial ports: {exc}") from exc
    return sorted(ports, key=lambda item: str(getattr(item, "device", item)).lower())


class RoxyDevice:
    """One short-lived connection used to query a Roxy device."""

    def __init__(self, port: str):
        self.port = port
        self._serial: Any = None

    def open(self) -> None:
        if self._serial is not None:
            return
        serial = _import_serial()
        try:
            self._serial = serial.Serial(
                port=self.port,
                baudrate=BAUDRATE,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0.05,
                xonxoff=False,
                rtscts=False,
                dsrdtr=False,
            )
        except Exception as exc:
            raise SerialTransportError(
                f"cannot open serial port {self.port}: {exc}"
            ) from exc

        for name in ("dtr", "rts"):
            try:
                setattr(self._serial, name, False)
            except Exception:
                pass
        try:
            self._serial.reset_input_buffer()
            self._serial.reset_output_buffer()
        except Exception as exc:
            self.close()
            raise SerialTransportError(
                f"cannot reset serial buffers on {self.port}: {exc}"
            ) from exc

    def close(self) -> None:
        if self._serial is not None:
            try:
                self._serial.close()
            finally:
                self._serial = None

    def query_info(self, timeout: float = 3.0) -> DeviceInfo:
        """Send QUERY_INFO and return the decoded 44-byte INFO payload."""

        if self._serial is None:
            raise SerialTransportError("serial device is not open")

        request = build_frame(CMD_QUERY_INFO, 0, b"")
        try:
            response = self.send_frame_and_wait_response(request, 0, timeout)
        except SerialTransportError:
            raise
        except Exception as exc:
            raise SerialTransportError(
                f"serial communication failed on {self.port}: {exc}"
            ) from exc

        if response.kind == "TIMEOUT":
            raise SerialTransportError(
                f"QUERY_INFO timeout on {self.port}: no INFO response"
            )
        if response.kind == "ERROR":
            raise SerialTransportError(
                f"QUERY_INFO frame error on {self.port}: {response.error}"
            )
        frame = response.frame
        if frame is None:
            raise SerialTransportError("QUERY_INFO response did not contain a frame")
        if frame.frame_type == RSP_NACK:
            raise SerialTransportError("QUERY_INFO received NACK")
        if frame.frame_type == RSP_ACK:
            raise SerialTransportError("unexpected ACK for QUERY_INFO; expected INFO")
        if frame.frame_type != RSP_INFO:
            raise SerialTransportError(
                f"unexpected response TYPE 0x{frame.frame_type:02X}; expected 0x82"
            )
        if frame.seq != 0:
            raise SerialTransportError(
                f"INFO sequence mismatch: expected 0, received {frame.seq}"
            )
        return parse_device_info(frame.payload)

    def send_frame_and_wait_response(
        self, frame: bytes, expected_seq: int, timeout: float = 10.0
    ) -> Response:
        """Write one frame and wait once; callers decide how to handle failure."""

        if self._serial is None:
            raise SerialTransportError("serial device is not open")
        try:
            written = self._serial.write(frame)
            if written is not None and written != len(frame):
                raise SerialTransportError(
                    f"serial write incomplete: {written}/{len(frame)} bytes"
                )
            self._serial.flush()
        except SerialTransportError:
            raise
        except Exception as exc:
            raise SerialTransportError(f"serial write failed on {self.port}: {exc}") from exc
        return wait_response(self._serial, expected_seq, timeout)

    def __enter__(self) -> "RoxyDevice":
        self.open()
        return self

    def __exit__(self, _exc_type: Any, _exc_value: Any, _traceback: Any) -> None:
        self.close()
