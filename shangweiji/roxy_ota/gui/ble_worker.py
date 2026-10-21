"""Bleak based BLE discovery worker used only by the GUI debug stage.

This module deliberately does not know the OTA protocol.  It only scans,
connects and reads the GATT table so that the possible UART characteristics
can be inspected manually.
"""

from __future__ import annotations

import asyncio
from typing import Any, Optional

from PySide6.QtCore import QObject, Signal, Slot

try:
    from bleak import BleakClient, BleakScanner
except ImportError:  # pragma: no cover - exercised on machines without bleak
    BleakClient = None  # type: ignore[assignment,misc]
    BleakScanner = None  # type: ignore[assignment,misc]


def normalize_property(value: Any) -> str:
    """Return a stable human-readable GATT property name."""

    name = str(value).strip().lower()
    if "." in name:
        name = name.rsplit(".", 1)[-1]
    return name.replace("_", "-")


def analyze_gatt_services(services: Any) -> dict[str, Any]:
    """Convert Bleak services into log-friendly data and find unique hints.

    A characteristic is considered writable when it has either ``write`` or
    ``write-without-response``.  The result is only a diagnostic hint; this
    function never assigns a protocol UUID or claims CF6208 compatibility.
    """

    service_items: list[dict[str, Any]] = []
    characteristics: list[dict[str, Any]] = []
    for service in services:
        service_item = {"uuid": str(service.uuid), "characteristics": []}
        for characteristic in service.characteristics:
            properties = [normalize_property(item) for item in characteristic.properties]
            item = {
                "uuid": str(characteristic.uuid),
                "properties": properties,
            }
            service_item["characteristics"].append(item)
            characteristics.append(item)
        service_items.append(service_item)

    writable = [
        item
        for item in characteristics
        if {"write", "write-without-response"}.intersection(item["properties"])
    ]
    notify = [item for item in characteristics if "notify" in item["properties"]]
    return {
        "services": service_items,
        "write_uuid": writable[0]["uuid"] if len(writable) == 1 else None,
        "notify_uuid": notify[0]["uuid"] if len(notify) == 1 else None,
    }


class BleSessionWorker(QObject):
    """Own one asyncio loop and one optional BleakClient in a QThread."""

    ready = Signal()
    scan_finished = Signal(object)
    connected = Signal(object)
    disconnected = Signal(str)
    gatt_finished = Signal(object)
    error = Signal(str)

    def __init__(self, scan_timeout: float = 5.0) -> None:
        super().__init__()
        self.scan_timeout = scan_timeout
        self._loop = asyncio.new_event_loop()
        self._client: Any = None
        self._devices: dict[str, Any] = {}
        self._connected_address: Optional[str] = None
        self._disconnect_requested = False
        self._closed = False

    @staticmethod
    def is_available() -> bool:
        return BleakScanner is not None and BleakClient is not None

    @Slot()
    def run(self) -> None:
        """Run the BLE asyncio loop until the GUI closes."""

        asyncio.set_event_loop(self._loop)
        self.ready.emit()
        try:
            self._loop.run_forever()
        finally:
            self._closed = True
            if not self._loop.is_closed():
                self._loop.close()

    def _submit(self, coroutine: Any) -> None:
        if self._closed or self._loop.is_closed():
            self.error.emit("BLE 调试线程已关闭")
            return
        try:
            future = asyncio.run_coroutine_threadsafe(coroutine, self._loop)
        except RuntimeError as exc:
            self.error.emit(f"BLE 操作无法启动：{exc}")
            return

        def report_exception(done: Any) -> None:
            try:
                done.result()
            except asyncio.CancelledError:
                pass
            except Exception as exc:  # pragma: no cover - defensive boundary
                self.error.emit(str(exc))

        future.add_done_callback(report_exception)

    def request_scan(self) -> None:
        self._submit(self._scan_async())

    def request_connect(self, address: str) -> None:
        self._submit(self._connect_async(address))

    def request_disconnect(self) -> None:
        self._submit(self._disconnect_async())

    def request_discover_gatt(self) -> None:
        self._submit(self._discover_gatt_async())

    def shutdown(self) -> None:
        """Disconnect and stop the worker loop during application shutdown."""

        if self._closed or self._loop.is_closed():
            return
        self._submit(self._shutdown_async())

    async def _scan_async(self) -> None:
        if not self.is_available():
            self.error.emit("未安装 bleak，无法扫描 BLE")
            return
        try:
            devices = await BleakScanner.discover(timeout=self.scan_timeout)
            self._devices = {
                str(device.address): device
                for device in devices
                if getattr(device, "address", None)
            }
            result = [
                {
                    "name": str(getattr(device, "name", None) or "未命名设备"),
                    "address": str(device.address),
                }
                for device in devices
                if getattr(device, "address", None)
            ]
            result.sort(key=lambda item: (item["name"].lower(), item["address"].lower()))
            self.scan_finished.emit(result)
        except Exception as exc:
            self.error.emit(f"BLE 扫描失败：{exc}")

    async def _connect_async(self, address: str) -> None:
        if not self.is_available():
            self.error.emit("未安装 bleak，无法连接 BLE")
            return
        try:
            if self._client is not None and getattr(self._client, "is_connected", False):
                await self._client.disconnect()
            device = self._devices.get(address)
            if device is None:
                device = await BleakScanner.find_device_by_address(address, timeout=10.0)
            if device is None:
                raise RuntimeError(f"未找到 BLE 设备：{address}")
            self._client = BleakClient(device, disconnected_callback=self._on_disconnected)
            await self._client.connect()
            if not getattr(self._client, "is_connected", False):
                raise RuntimeError("BLE 连接未建立")
            self._connected_address = address
            self.connected.emit(
                {
                    "name": str(getattr(device, "name", None) or "未命名设备"),
                    "address": address,
                }
            )
        except Exception as exc:
            self._client = None
            self._connected_address = None
            self.error.emit(f"BLE 连接失败：{exc}")

    async def _disconnect_async(self) -> None:
        address = self._connected_address or ""
        self._disconnect_requested = True
        try:
            if self._client is not None and getattr(self._client, "is_connected", False):
                await self._client.disconnect()
        except Exception as exc:
            self.error.emit(f"BLE 断开失败：{exc}")
        finally:
            self._disconnect_requested = False
            self._client = None
            self._connected_address = None
            self.disconnected.emit(address)

    async def _discover_gatt_async(self) -> None:
        if self._client is None or not getattr(self._client, "is_connected", False):
            self.error.emit("BLE 设备尚未连接")
            return
        try:
            result = analyze_gatt_services(self._client.services)
            self.gatt_finished.emit(result)
        except Exception as exc:
            self.error.emit(f"GATT 枚举失败：{exc}")

    def _on_disconnected(self, _client: Any) -> None:
        if self._disconnect_requested:
            return
        address = self._connected_address or ""
        self._client = None
        self._connected_address = None
        self.disconnected.emit(address)

    async def _shutdown_async(self) -> None:
        try:
            if self._client is not None and getattr(self._client, "is_connected", False):
                await self._client.disconnect()
        except Exception:
            pass
        self._client = None
        self._connected_address = None
        self._loop.stop()


__all__ = ["BleSessionWorker", "analyze_gatt_services", "normalize_property"]
