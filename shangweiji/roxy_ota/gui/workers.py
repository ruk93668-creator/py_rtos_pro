"""GUI 后台任务，避免串口操作阻塞主界面。"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Optional

from PySide6.QtCore import QObject, Signal, Slot

from ..core.device import DeviceInfo, inactive_slot
from ..core.ota_updater import OtaResult, OtaUpdater, make_ota_plan
from ..core.protocol import OtaError
from ..core.serial_transport import RoxyDevice
from ..firmware.ciallo import CialloPackage


class QueryInfoWorker(QObject):
    """打开一次串口，查询 INFO，关闭串口并通过信号返回结果。"""

    finished = Signal(object)
    error = Signal(str)
    log = Signal(str)

    def __init__(self, port: str, timeout: float = 3.0) -> None:
        super().__init__()
        self.port = port
        self.timeout = timeout

    @Slot()
    def run(self) -> None:
        try:
            self.log.emit(f"正在以 38400 波特率打开 {self.port}……")
            with RoxyDevice(self.port) as device:
                self.log.emit("已发送 QUERY_INFO")
                info: DeviceInfo = device.query_info(timeout=self.timeout)
                self.log.emit("已收到 INFO")
            self.log.emit("串口已关闭")
            self.finished.emit(info)
        except Exception as exc:
            self.error.emit(str(exc))


@dataclass(frozen=True)
class OtaWorkerResult:
    """OTA result plus the optional post-upgrade device refresh."""

    ota_result: OtaResult
    refreshed_info: Optional[DeviceInfo]
    refresh_pending: bool
    refresh_error: Optional[str] = None


class OtaWorker(QObject):
    """重新查询、生成计划、上传并刷新设备，线程中不操作 QWidget。"""

    progress = Signal(int, int)
    status = Signal(str)
    log = Signal(str)
    finished = Signal(object)
    error = Signal(str)

    def __init__(
        self,
        port: str,
        package: CialloPackage,
        query_timeout: float = 3.0,
        ota_timeout: float = 10.0,
    ) -> None:
        super().__init__()
        # Keep immutable references for the complete worker lifetime.
        self.port = port
        self.package = package
        self.query_timeout = query_timeout
        self.ota_timeout = ota_timeout

    @Slot()
    def run(self) -> None:
        try:
            self.status.emit("准备中……")
            self.log.emit(f"正在以 38400 波特率打开 {self.port}……")
            with RoxyDevice(self.port) as device:
                self.log.emit("已发送 QUERY_INFO")
                fresh_info = device.query_info(timeout=self.query_timeout)
                self.log.emit("已收到 INFO")

                target_slot = inactive_slot(fresh_info.current_slot)
                if target_slot is None:
                    raise OtaError("当前运行槽位无效")
                image = self.package.image_for_slot(target_slot)
                plan = make_ota_plan(fresh_info, image)
                self.log.emit(
                    f"当前运行槽位：{plan.running_slot}；"
                    f"目标槽位：{plan.target_slot}；版本：{plan.version}"
                )

                updater = OtaUpdater(
                    device,
                    plan.image,
                    plan.target_slot,
                    plan.version,
                    timeout=self.ota_timeout,
                )
                result = updater.perform(
                    progress_callback=self._on_progress,
                    status_callback=self._emit_status,
                    log_callback=self._emit_log,
                )

                # END ACK is the OTA success criterion.  Close before waiting
                # because the MCU may reset and briefly drop the USB-UART link.
                self.status.emit("刷新设备信息……")
                device.close()

            time.sleep(0.2)
            try:
                with RoxyDevice(self.port) as refreshed_device:
                    self.log.emit("已发送 QUERY_INFO")
                    refreshed_info = refreshed_device.query_info(timeout=self.query_timeout)
                    self.log.emit("已收到 INFO")
                self.finished.emit(
                    OtaWorkerResult(result, refreshed_info, False, None)
                )
            except Exception as refresh_exc:
                self.finished.emit(
                    OtaWorkerResult(result, None, True, str(refresh_exc))
                )
        except Exception as exc:
            self.error.emit(str(exc))

    def _on_progress(self, sent_bytes: int, total_bytes: int) -> None:
        self.progress.emit(sent_bytes, total_bytes)
        # Keep the visible log concise while preserving byte-level progress in
        # the progress signal and progress bar.
        percent = int(sent_bytes * 100 / total_bytes) if total_bytes else 0
        bucket = percent // 10
        last_bucket = getattr(self, "_last_log_bucket", -1)
        if bucket > last_bucket:
            self._last_log_bucket = bucket
            self.log.emit(f"{percent}%（{sent_bytes} / {total_bytes} 字节）")

    def _emit_status(self, value: str) -> None:
        status_text = {
            "Preparing...": "准备中……",
            "Sending BEGIN...": "发送 BEGIN……",
            "Uploading...": "上传中……",
            "Finishing...": "完成中……",
        }.get(value, value)
        self.status.emit(status_text)

    def _emit_log(self, value: str) -> None:
        log_text = {
            "BEGIN ... ACK": "BEGIN 已收到 ACK",
            "Upload started": "开始上传",
            "END ... ACK": "END 已收到 ACK",
        }.get(value, value)
        self.log.emit(log_text)
