"""Fourth-stage Roxy OTA GUI with source/frozen path compatibility."""

from __future__ import annotations

from datetime import datetime
from pathlib import Path
from typing import Any, Optional

from PySide6.QtCore import Qt, QThread
from PySide6.QtGui import QColor, QPainter, QPixmap
from PySide6.QtWidgets import (
    QComboBox,
    QDialog,
    QDialogButtonBox,
    QFileDialog,
    QFormLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QProgressBar,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ..core.device import (
    DeviceInfo,
    SlotInfo,
    inactive_slot,
    next_version,
    slot_name,
    state_name,
)
from ..core.serial_transport import SerialTransportError, list_serial_ports
from ..firmware.ciallo import CialloPackage, FirmwareImage, load_ciallo
from ..utils.paths import find_external_package, resource_path
from ..version import APP_DISPLAY_NAME, APP_NAME, APP_VERSION
from .ble_worker import BleSessionWorker
from .connection_mode import BLE_UART_FLAG, ConnectionMode, TYPEC_UART_FLAG
from .workers import OtaWorker, OtaWorkerResult, QueryInfoWorker


class BackgroundWidget(QWidget):
    """Paint the supplied blue-purple illustration behind the controls."""

    def __init__(self, image_path: Path, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._background = QPixmap(str(image_path))

    def paintEvent(self, event: Any) -> None:  # noqa: N802 - Qt override
        painter = QPainter(self)
        if not self._background.isNull():
            painter.drawPixmap(self.rect(), self._background, self._background.rect())
            painter.fillRect(self.rect(), QColor(255, 255, 255, 170))
        else:
            painter.fillRect(self.rect(), QColor(225, 235, 255))
        super().paintEvent(event)


class MainWindow(QMainWindow):
    """显示设备信息、已校验固件包和只读升级计划。"""

    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle(APP_DISPLAY_NAME)
        self.resize(720, 760)
        self._query_thread: Optional[QThread] = None
        self._query_worker: Optional[QueryInfoWorker] = None
        self._ota_thread: Optional[QThread] = None
        self._ota_worker: Optional[OtaWorker] = None
        self._device_info: Optional[DeviceInfo] = None
        self._package: Optional[CialloPackage] = None
        self._ports_available = False
        self.connection_mode_flag = TYPEC_UART_FLAG
        self._ble_thread: Optional[QThread] = None
        self._ble_worker: Optional[BleSessionWorker] = None
        self._ble_busy = False
        self._ble_connected_address: Optional[str] = None
        self._slot_labels: dict[str, dict[str, QLabel]] = {}
        self._firmware_labels: dict[str, dict[str, QLabel]] = {}

        image_path = resource_path("e221ec38138fda53cbb4bc72ccbd3b5e.jpg")
        panel = BackgroundWidget(image_path)
        self.setCentralWidget(panel)
        layout = QVBoxLayout(panel)
        layout.setContentsMargins(14, 12, 14, 12)
        layout.setSpacing(8)

        self._build_device_section(layout)
        self._build_current_slot(layout)
        self._build_slot_sections(layout)
        self._build_firmware_section(layout)
        self._build_plan_section(layout)
        self._build_log(layout)
        self._build_menu()
        self._apply_connection_mode()
        self.setStyleSheet(
            """
            QGroupBox {
                background-color: rgba(255, 255, 255, 215);
                border: 1px solid rgba(65, 95, 160, 150);
                border-radius: 6px;
                margin-top: 8px;
                padding: 8px;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 4px;
                color: #183c76;
                font-weight: bold;
            }
            QPlainTextEdit, QLineEdit, QComboBox {
                background-color: rgba(255, 255, 255, 230);
                border: 1px solid rgba(65, 95, 160, 150);
                border-radius: 4px;
                padding: 3px;
            }
            QLabel#currentSlot {
                color: #123b83;
                font-size: 30px;
                font-weight: bold;
            }
            QLabel#planStatus {
                color: #183c76;
                font-weight: bold;
            }
            """
        )
        self._auto_load_ciallo()
        self.refresh_ports()
        self.append_log("程序已启动")

    def _build_menu(self) -> None:
        settings_action = self.menuBar().addAction("设置")
        settings_action.triggered.connect(self._show_settings)
        help_menu = self.menuBar().addMenu("帮助")
        about_action = help_menu.addAction("关于")
        about_action.triggered.connect(self._show_about)

    def _show_settings(self) -> None:
        dialog = QDialog(self)
        dialog.setWindowTitle("设置")
        dialog.setMinimumWidth(360)
        form = QFormLayout(dialog)

        mode_combo = QComboBox(dialog)
        mode_combo.addItem("Type-C", TYPEC_UART_FLAG)
        mode_combo.addItem("Bluetooth", BLE_UART_FLAG)
        mode_combo.setCurrentIndex(
            mode_combo.findData(self.connection_mode_flag)
        )
        flag_label = QLabel(str(self.connection_mode_flag), dialog)
        mode_name_label = QLabel("TYPEC-UART", dialog)
        version_label = QLabel(APP_VERSION, dialog)
        form.addRow("连接模式：", mode_combo)
        form.addRow("选择标志位：", flag_label)
        form.addRow("标志位名称：", mode_name_label)
        form.addRow("当前版本：", version_label)

        def update_flag(index: int) -> None:
            mode = ConnectionMode(int(mode_combo.itemData(index)))
            flag_label.setText(str(int(mode)))
            mode_name_label.setText(mode.flag_name)

        mode_combo.currentIndexChanged.connect(update_flag)
        update_flag(mode_combo.currentIndex())

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Ok
            | QDialogButtonBox.StandardButton.Cancel,
            parent=dialog,
        )
        buttons.accepted.connect(dialog.accept)
        buttons.rejected.connect(dialog.reject)
        form.addRow(buttons)

        if dialog.exec() == QDialog.DialogCode.Accepted:
            selected_flag = int(mode_combo.currentData())
            self.connection_mode_combo.setCurrentIndex(
                self.connection_mode_combo.findData(selected_flag)
            )

    @property
    def selected_connection_mode_flag(self) -> int:
        """Return 0 for TYPEC-UART or 1 for BLE-UART."""

        return int(self.connection_mode_flag)

    def _show_about(self) -> None:
        QMessageBox.about(
            self,
            f"关于 {APP_NAME}",
            f"{APP_NAME}\n版本 {APP_VERSION}\n\n"
            "Roxy 设备固件升级工具。",
        )

    @staticmethod
    def _slot_text(slot: int | str) -> str:
        name = slot_name(slot) if isinstance(slot, int) else slot
        return {"NONE": "无"}.get(name, name)

    @staticmethod
    def _state_text(state: int) -> str:
        name = state_name(state)
        return {
            "EMPTY": "空",
            "DOWNLOADING": "下载中",
            "PENDING": "待确认",
            "CONFIRMED": "已确认",
            "BAD": "异常",
        }.get(name, name)

    def _auto_load_ciallo(self) -> None:
        """Load only the package beside the app when it exists at startup."""

        package_path = find_external_package()
        if package_path is None:
            self.package_path_edit.setPlaceholderText("未加载固件包")
            self.append_log("未加载固件包")
            return

        self.append_log(f"发现程序目录中的固件包：{package_path}")
        try:
            package = load_ciallo(package_path)
        except Exception as exc:
            self._package = None
            self._clear_firmware_display()
            self.package_path_edit.clear()
            self.package_path_edit.setPlaceholderText("固件包无效")
            self.append_log(f"错误：固件包无效：{exc}")
            QMessageBox.warning(self, "固件包无效", str(exc))
            self.update_upgrade_plan()
            return

        self._package = package
        self.package_path_edit.setText(str(package.path))
        self._update_firmware_card("A", package.image_a)
        self._update_firmware_card("B", package.image_b)
        self.append_log(f"已加载固件包：{package.path}")
        self.update_upgrade_plan()

    def _build_device_section(self, parent_layout: QVBoxLayout) -> None:
        group = QGroupBox("设备连接")
        outer = QVBoxLayout(group)

        mode_row = QHBoxLayout()
        mode_row.addWidget(QLabel("连接模式："))
        self.connection_mode_combo = QComboBox()
        self.connection_mode_combo.addItem("Type-C", TYPEC_UART_FLAG)
        self.connection_mode_combo.addItem("Bluetooth", BLE_UART_FLAG)
        self.connection_mode_combo.currentIndexChanged.connect(
            self._connection_mode_changed
        )
        mode_row.addWidget(self.connection_mode_combo)
        self.mode_flag_label = QLabel("标志位：0（TYPEC-UART）")
        mode_row.addWidget(self.mode_flag_label)
        mode_row.addStretch(1)
        outer.addLayout(mode_row)

        typec_row = QHBoxLayout()
        typec_row.addWidget(QLabel("串口："))
        self.port_combo = QComboBox()
        self.port_combo.currentIndexChanged.connect(self._update_query_button)
        typec_row.addWidget(self.port_combo, 1)
        self.refresh_button = QPushButton("刷新串口")
        self.refresh_button.clicked.connect(self.refresh_ports)
        typec_row.addWidget(self.refresh_button)
        self.query_button = QPushButton("查询设备")
        self.query_button.clicked.connect(self._query_device)
        typec_row.addWidget(self.query_button)
        outer.addLayout(typec_row)
        self._typec_widgets = [self.port_combo, self.refresh_button, self.query_button]

        ble_row = QHBoxLayout()
        ble_row.addWidget(QLabel("BLE 设备："))
        self.ble_device_combo = QComboBox()
        self.ble_device_combo.setMinimumWidth(220)
        self.ble_device_combo.currentIndexChanged.connect(self._update_operation_controls)
        ble_row.addWidget(self.ble_device_combo, 1)
        self.ble_scan_button = QPushButton("扫描 BLE")
        self.ble_scan_button.clicked.connect(self._scan_ble)
        ble_row.addWidget(self.ble_scan_button)
        self.ble_connect_button = QPushButton("连接 BLE")
        self.ble_connect_button.clicked.connect(self._connect_ble)
        ble_row.addWidget(self.ble_connect_button)
        self.ble_discover_button = QPushButton("枚举 GATT")
        self.ble_discover_button.clicked.connect(self._discover_gatt)
        ble_row.addWidget(self.ble_discover_button)
        outer.addLayout(ble_row)
        self._ble_widgets = [
            self.ble_device_combo,
            self.ble_scan_button,
            self.ble_connect_button,
            self.ble_discover_button,
        ]

        status_row = QHBoxLayout()
        status_row.addWidget(QLabel("状态："))
        self.status_label = QLabel("未连接")
        status_row.addWidget(self.status_label)
        status_row.addStretch(1)
        outer.addLayout(status_row)
        parent_layout.addWidget(group)

    def _build_current_slot(self, parent_layout: QVBoxLayout) -> None:
        group = QGroupBox("当前运行槽位")
        row = QHBoxLayout(group)
        row.addStretch(1)
        self.current_slot_label = QLabel("无")
        self.current_slot_label.setObjectName("currentSlot")
        self.current_slot_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        row.addWidget(self.current_slot_label)
        row.addStretch(1)
        parent_layout.addWidget(group)

    def _build_slot_sections(self, parent_layout: QVBoxLayout) -> None:
        row = QHBoxLayout()
        row.addWidget(self._make_slot_group("A"), 1)
        row.addWidget(self._make_slot_group("B"), 1)
        parent_layout.addLayout(row)

    def _make_slot_group(self, slot: str) -> QGroupBox:
        group = QGroupBox(f"槽位 {slot}")
        form = QFormLayout(group)
        labels: dict[str, QLabel] = {}
        for field in ("State", "Version", "Size", "CRC32", "Boot Attempt"):
            value = QLabel("-")
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            form.addRow(
                {
                    "State": "状态：",
                    "Version": "版本：",
                    "Size": "大小：",
                    "CRC32": "CRC32：",
                    "Boot Attempt": "启动尝试：",
                }[field],
                value,
            )
            labels[field] = value
        self._slot_labels[slot] = labels
        return group

    def _build_firmware_section(self, parent_layout: QVBoxLayout) -> None:
        group = QGroupBox("固件包")
        outer = QVBoxLayout(group)
        path_row = QHBoxLayout()
        path_row.addWidget(QLabel("包路径："))
        self.package_path_edit = QLineEdit()
        self.package_path_edit.setReadOnly(True)
        self.package_path_edit.setPlaceholderText("未加载固件包")
        path_row.addWidget(self.package_path_edit, 1)
        self.browse_button = QPushButton("浏览")
        self.browse_button.clicked.connect(self._browse_ciallo)
        path_row.addWidget(self.browse_button)
        outer.addLayout(path_row)

        images_row = QHBoxLayout()
        images_row.addWidget(self._make_firmware_group("A"), 1)
        images_row.addWidget(self._make_firmware_group("B"), 1)
        outer.addLayout(images_row)
        parent_layout.addWidget(group)

    def _make_firmware_group(self, slot: str) -> QGroupBox:
        group = QGroupBox(f"应用 {slot}")
        form = QFormLayout(group)
        labels: dict[str, QLabel] = {}
        for field in ("Size", "CRC32", "MSP", "Reset Handler"):
            value = QLabel("-")
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            form.addRow(
                {
                    "Size": "大小：",
                    "CRC32": "CRC32：",
                    "MSP": "MSP：",
                    "Reset Handler": "复位处理函数：",
                }[field],
                value,
            )
            labels[field] = value
        self._firmware_labels[slot] = labels
        return group

    def _build_plan_section(self, parent_layout: QVBoxLayout) -> None:
        group = QGroupBox("升级计划")
        form = QFormLayout(group)
        self.plan_status_label = QLabel("未就绪")
        self.plan_status_label.setObjectName("planStatus")
        form.addRow("状态：", self.plan_status_label)
        self.plan_labels: dict[str, QLabel] = {}
        for field in (
            "Running Slot",
            "Target Slot",
            "New Version",
            "Firmware Size",
            "Firmware CRC32",
        ):
            value = QLabel("-")
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            form.addRow(
                {
                    "Running Slot": "运行槽位：",
                    "Target Slot": "目标槽位：",
                    "New Version": "新版本：",
                    "Firmware Size": "固件大小：",
                    "Firmware CRC32": "固件 CRC32：",
                }[field],
                value,
            )
            self.plan_labels[field] = value
        self.ota_status_label = QLabel("空闲")
        form.addRow("OTA 状态：", self.ota_status_label)
        self.ota_progress = QProgressBar()
        self.ota_progress.setRange(0, 100)
        self.ota_progress.setValue(0)
        self.ota_progress.setFormat("%p%")
        form.addRow("进度：", self.ota_progress)
        self.start_ota_button = QPushButton("开始 OTA")
        self.start_ota_button.setEnabled(False)
        self.start_ota_button.clicked.connect(self._start_ota)
        form.addRow(self.start_ota_button)
        parent_layout.addWidget(group)

    def _build_log(self, parent_layout: QVBoxLayout) -> None:
        group = QGroupBox("日志")
        inner = QVBoxLayout(group)
        self.log_edit = QPlainTextEdit()
        self.log_edit.setReadOnly(True)
        inner.addWidget(self.log_edit)
        parent_layout.addWidget(group, 1)

    def append_log(self, message: str) -> None:
        timestamp = datetime.now().strftime("%H:%M:%S")
        self.log_edit.appendPlainText(f"[{timestamp}] {message}")

    def refresh_ports(self) -> None:
        previous = self.port_combo.currentData() if self.port_combo.count() else None
        try:
            ports = list_serial_ports()
        except SerialTransportError as exc:
            self.port_combo.clear()
            self.port_combo.addItem("没有可用串口", None)
            self._ports_available = False
            self.port_combo.setEnabled(False)
            self.status_label.setText("错误")
            self.append_log(f"错误：{exc}")
            self._update_operation_controls()
            return

        self.port_combo.blockSignals(True)
        self.port_combo.clear()
        for descriptor in ports:
            device = str(getattr(descriptor, "device", descriptor))
            description = str(getattr(descriptor, "description", "") or "")
            display = f"{device} - {description}" if description else device
            self.port_combo.addItem(display, device)
        if not ports:
            self.port_combo.addItem("没有可用串口", None)
            self._ports_available = False
            self.port_combo.setEnabled(False)
        else:
            self._ports_available = True
            self.port_combo.setEnabled(True)
            if previous:
                index = self.port_combo.findData(previous)
                self.port_combo.setCurrentIndex(index if index >= 0 else 0)
            else:
                self.port_combo.setCurrentIndex(0)
        self.port_combo.blockSignals(False)
        self.append_log("串口列表已刷新")
        self._update_operation_controls()

    def _update_query_button(self, *_args: Any) -> None:
        self._update_operation_controls()

    def _set_query_busy(self, busy: bool) -> None:
        self._update_operation_controls()

    def _plan_ready(self) -> bool:
        return (
            self._device_info is not None
            and self._package is not None
            and self.plan_status_label.text() == "就绪"
        )

    def _update_operation_controls(self) -> None:
        """Keep Query/Package/OTA controls consistent with worker state."""

        ota_busy = self._ota_thread is not None
        query_busy = self._query_thread is not None
        ble_busy = self._ble_busy
        typec_mode = self.connection_mode_flag == TYPEC_UART_FLAG
        has_port = self._ports_available and self.port_combo.currentData() is not None
        has_ble_device = self.ble_device_combo.currentData() is not None

        self.connection_mode_combo.setEnabled(not ota_busy and not query_busy and not ble_busy)
        for widget in self._typec_widgets:
            widget.setEnabled(typec_mode and not ota_busy and not query_busy)

        if not typec_mode:
            self.query_button.setEnabled(False)
            self.start_ota_button.setEnabled(False)

        self.ble_device_combo.setEnabled(
            not typec_mode and not ota_busy and not query_busy and not ble_busy
        )
        self.ble_scan_button.setEnabled(
            not typec_mode and not ota_busy and not query_busy and not ble_busy
        )
        self.ble_connect_button.setEnabled(
            not typec_mode
            and not ota_busy
            and not query_busy
            and not ble_busy
            and (self._ble_connected_address is not None or has_ble_device)
        )
        self.ble_discover_button.setEnabled(
            not typec_mode
            and not ota_busy
            and not query_busy
            and not ble_busy
            and self._ble_connected_address is not None
        )

        if ota_busy:
            self.browse_button.setEnabled(False)
            self.start_ota_button.setEnabled(False)
            return

        self.port_combo.setEnabled(typec_mode and self._ports_available and not query_busy)
        self.refresh_button.setEnabled(typec_mode and not query_busy)
        self.query_button.setEnabled(typec_mode and has_port and not query_busy)
        self.browse_button.setEnabled(True)
        self.start_ota_button.setEnabled(
            typec_mode and self._plan_ready() and has_port and not query_busy
        )

    def _connection_mode_changed(self, index: int) -> None:
        if index < 0:
            return
        self.connection_mode_flag = int(self.connection_mode_combo.itemData(index))
        mode = ConnectionMode(self.connection_mode_flag)
        self.mode_flag_label.setText(
            f"标志位：{self.connection_mode_flag}（{mode.flag_name}）"
        )
        self._apply_connection_mode()
        self.append_log(
            f"连接模式：{mode.display_name}；选择标志位："
            f"{self.connection_mode_flag}（{mode.flag_name}）"
        )

    def _apply_connection_mode(self) -> None:
        typec_mode = self.connection_mode_flag == TYPEC_UART_FLAG
        if typec_mode:
            self.status_label.setText("未连接")
            self.ota_status_label.setText("空闲")
        else:
            self.status_label.setText("未连接 BLE")
            self.ota_status_label.setText("BLE 调试模式")
        self._update_operation_controls()

    def _ensure_ble_worker(self) -> Optional[BleSessionWorker]:
        if not BleSessionWorker.is_available():
            self.status_label.setText("BLE 不可用")
            self.append_log("错误：未安装 bleak，无法使用 BLE 调试")
            return None
        if self._ble_thread is not None and self._ble_worker is not None:
            return self._ble_worker

        thread = QThread(self)
        worker = BleSessionWorker()
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        worker.scan_finished.connect(self._ble_scan_finished)
        worker.connected.connect(self._ble_connected)
        worker.disconnected.connect(self._ble_disconnected)
        worker.gatt_finished.connect(self._ble_gatt_finished)
        worker.error.connect(self._ble_error)
        thread.finished.connect(worker.deleteLater)
        self._ble_thread = thread
        self._ble_worker = worker
        thread.start()
        return worker

    def _scan_ble(self) -> None:
        if self.connection_mode_flag != BLE_UART_FLAG or self._ble_busy:
            return
        worker = self._ensure_ble_worker()
        if worker is None:
            return
        self._ble_busy = True
        self.status_label.setText("正在扫描 BLE……")
        self.append_log("开始扫描 BLE 设备……")
        self._update_operation_controls()
        worker.request_scan()

    def _ble_scan_finished(self, devices: object) -> None:
        self._ble_busy = False
        self.ble_device_combo.blockSignals(True)
        self.ble_device_combo.clear()
        entries = list(devices) if isinstance(devices, list) else []
        for item in entries:
            name = str(item.get("name", "未命名设备"))
            address = str(item.get("address", ""))
            if address:
                self.ble_device_combo.addItem(f"{name} [{address}]", address)
                self.append_log(f"发现 BLE 设备：{name} [{address}]")
        if not entries:
            self.ble_device_combo.addItem("未发现 BLE 设备", None)
        self.ble_device_combo.blockSignals(False)
        self.status_label.setText("BLE 扫描完成")
        self.append_log(f"BLE 扫描完成，共发现 {len(entries)} 个设备")
        self._update_operation_controls()

    def _connect_ble(self) -> None:
        if self.connection_mode_flag != BLE_UART_FLAG or self._ble_busy:
            return
        worker = self._ensure_ble_worker()
        if worker is None:
            return
        self._ble_busy = True
        self._update_operation_controls()
        if self._ble_connected_address is not None:
            self.status_label.setText("正在断开 BLE……")
            self.append_log("正在断开 BLE 设备")
            worker.request_disconnect()
            return

        address = self.ble_device_combo.currentData()
        if not address:
            self._ble_busy = False
            self.status_label.setText("未选择 BLE 设备")
            self.append_log("错误：未选择 BLE 设备")
            self._update_operation_controls()
            return
        self.status_label.setText("正在连接 BLE……")
        self.append_log(f"正在连接 BLE 设备：{address}")
        worker.request_connect(str(address))

    def _ble_connected(self, device: object) -> None:
        self._ble_busy = False
        item = device if isinstance(device, dict) else {}
        self._ble_connected_address = str(item.get("address", "")) or None
        name = str(item.get("name", "未命名设备"))
        address = self._ble_connected_address or ""
        self.ble_connect_button.setText("断开 BLE")
        self.status_label.setText("BLE 已连接")
        self.append_log(f"BLE 已连接：{name} [{address}]")
        self._update_operation_controls()

    def _ble_disconnected(self, address: str) -> None:
        self._ble_busy = False
        self._ble_connected_address = None
        self.ble_connect_button.setText("连接 BLE")
        self.status_label.setText("BLE 未连接")
        self.append_log(f"BLE 已断开：{address}" if address else "BLE 已断开")
        self._update_operation_controls()

    def _discover_gatt(self) -> None:
        if (
            self.connection_mode_flag != BLE_UART_FLAG
            or self._ble_busy
            or self._ble_connected_address is None
        ):
            return
        worker = self._ensure_ble_worker()
        if worker is None:
            return
        self._ble_busy = True
        self.status_label.setText("正在枚举 GATT……")
        self.append_log("开始枚举 GATT 服务和特征")
        self._update_operation_controls()
        worker.request_discover_gatt()

    def _ble_gatt_finished(self, result: object) -> None:
        self._ble_busy = False
        payload = result if isinstance(result, dict) else {}
        services = payload.get("services", [])
        for service in services:
            self.append_log(f"Service: {service.get('uuid', '')}")
            for characteristic in service.get("characteristics", []):
                self.append_log(f"Characteristic: {characteristic.get('uuid', '')}")
                properties = characteristic.get("properties", [])
                property_text = ", ".join(str(item) for item in properties) or "none"
                self.append_log(f"Properties: {property_text}")

        write_uuid = payload.get("write_uuid")
        notify_uuid = payload.get("notify_uuid")
        if write_uuid:
            self.append_log(f"Possible UART Write Characteristic: {write_uuid}")
        if notify_uuid:
            self.append_log(f"Possible UART Notify Characteristic: {notify_uuid}")
        self.status_label.setText("GATT 枚举完成")
        self.append_log("GATT 枚举完成；未自动认定 CF6208 透传 UUID")
        self._update_operation_controls()

    def _ble_error(self, message: str) -> None:
        self._ble_busy = False
        self.status_label.setText("BLE 错误")
        self.append_log(f"错误：{message}")
        self._update_operation_controls()

    def _query_device(self) -> None:
        if self.connection_mode_flag != TYPEC_UART_FLAG:
            self.status_label.setText("Bluetooth 调试模式")
            self.append_log("当前为 Bluetooth 调试模式，不发送 QUERY_INFO")
            return
        port = self.port_combo.currentData()
        if not port:
            self.status_label.setText("错误")
            self.append_log("错误：未选择串口")
            return
        if self._query_thread is not None:
            return

        self.status_label.setText("查询中……")
        self.append_log(f"正在查询 {port}……")

        thread = QThread(self)
        worker = QueryInfoWorker(str(port))
        worker.moveToThread(thread)
        thread.started.connect(worker.run)
        worker.log.connect(self.append_log)
        worker.finished.connect(self._query_finished)
        worker.error.connect(self._query_failed)
        worker.finished.connect(thread.quit)
        worker.error.connect(thread.quit)
        thread.finished.connect(worker.deleteLater)
        thread.finished.connect(self._query_thread_finished)
        thread.finished.connect(thread.deleteLater)
        self._query_thread = thread
        self._query_worker = worker
        self._update_operation_controls()
        thread.start()

    def _query_finished(self, info: DeviceInfo) -> None:
        self._device_info = info
        self.current_slot_label.setText(self._slot_text(info.current_slot))
        self._update_slot_card("A", info.slot_a)
        self._update_slot_card("B", info.slot_b)
        self.status_label.setText("设备已连接")
        self.append_log(f"当前运行槽位：{self._slot_text(info.current_slot)}")
        self.append_log("设备查询完成")
        self.update_upgrade_plan()

    def _query_failed(self, message: str) -> None:
        self._device_info = None
        self._clear_device_display()
        self.status_label.setText("查询失败")
        self.append_log(f"错误：{message}")
        self.update_upgrade_plan()

    def _query_thread_finished(self) -> None:
        self._query_thread = None
        self._query_worker = None
        self._set_query_busy(False)

    def _update_slot_card(self, slot: str, info: SlotInfo) -> None:
        labels = self._slot_labels[slot]
        labels["State"].setText(self._state_text(info.state))
        labels["Version"].setText(str(info.version))
        labels["Size"].setText(f"{info.size} 字节")
        labels["CRC32"].setText(f"0x{info.crc32:08X}")
        labels["Boot Attempt"].setText(str(info.boot_attempt))

    def _clear_device_display(self) -> None:
        self.current_slot_label.setText("无")
        for labels in self._slot_labels.values():
            for value in labels.values():
                value.setText("-")

    def _browse_ciallo(self) -> None:
        if self._ota_thread is not None:
            return
        selected, _filter = QFileDialog.getOpenFileName(
            self,
            "选择固件包",
            "",
            "Ciallo 固件包 (*.ciallo);;所有文件 (*.*)",
        )
        if not selected:
            return

        try:
            package = load_ciallo(selected)
        except Exception as exc:
            self._package = None
            self._clear_firmware_display()
            self.package_path_edit.clear()
            self.append_log(f"错误：固件包无效：{exc}")
            QMessageBox.warning(self, "固件包无效", str(exc))
            self.update_upgrade_plan()
            return

        self._package = package
        self.package_path_edit.setText(str(package.path))
        self._update_firmware_card("A", package.image_a)
        self._update_firmware_card("B", package.image_b)
        self.append_log(f"已加载固件包：{package.path}")
        self.update_upgrade_plan()

    def _update_firmware_card(self, slot: str, image: FirmwareImage) -> None:
        labels = self._firmware_labels[slot]
        labels["Size"].setText(f"{image.size} 字节")
        labels["CRC32"].setText(f"0x{image.crc32:08X}")
        labels["MSP"].setText(f"0x{image.msp:08X}")
        labels["Reset Handler"].setText(f"0x{image.reset_handler:08X}")

    def _clear_firmware_display(self) -> None:
        for labels in self._firmware_labels.values():
            for value in labels.values():
                value.setText("-")

    def update_upgrade_plan(self) -> None:
        """Recompute the complete read-only plan from current GUI state."""

        self.plan_status_label.setText("未就绪")
        for value in self.plan_labels.values():
            value.setText("-")

        if self._device_info is None or self._package is None:
            self._update_operation_controls()
            return

        running = self._slot_text(self._device_info.current_slot)
        self.plan_labels["Running Slot"].setText(running)
        target = inactive_slot(self._device_info.current_slot)
        if target is None:
            self.plan_labels["Target Slot"].setText("未知")
            self._update_operation_controls()
            return

        self.plan_labels["Target Slot"].setText(target)
        try:
            version = next_version(
                self._device_info.slot_a.version,
                self._device_info.slot_b.version,
            )
        except Exception as exc:
            self.append_log(f"错误：{exc}")
            self._update_operation_controls()
            return

        image = self._package.image_for_slot(target)
        self.plan_labels["New Version"].setText(str(version))
        self.plan_labels["Firmware Size"].setText(f"{image.size} 字节")
        self.plan_labels["Firmware CRC32"].setText(f"0x{image.crc32:08X}")
        self.plan_status_label.setText("就绪")
        self._update_operation_controls()

    def _start_ota(self) -> None:
        """Start one immutable OTA worker after its final readiness check."""

        if self.connection_mode_flag != TYPEC_UART_FLAG:
            self.append_log("当前为 Bluetooth 调试模式，不发送 BEGIN、DATA 或 END")
            return
        port = self.port_combo.currentData()
        package = self._package
        if not port or package is None or not self._plan_ready():
            self.append_log("错误：OTA 尚未就绪")
            return
        if self._query_thread is not None or self._ota_thread is not None:
            return

        self.ota_progress.setValue(0)
        self.ota_status_label.setText("准备中……")
        self.status_label.setText("准备中……")
        self.append_log("正在启动 OTA 任务")
        self._ota_thread = QThread(self)
        self._ota_worker = OtaWorker(str(port), package)
        self._ota_worker.moveToThread(self._ota_thread)
        self._ota_thread.started.connect(self._ota_worker.run)
        self._ota_worker.progress.connect(self._ota_progress_changed)
        self._ota_worker.status.connect(self._ota_status_changed)
        self._ota_worker.log.connect(self.append_log)
        self._ota_worker.finished.connect(self._ota_finished)
        self._ota_worker.error.connect(self._ota_failed)
        self._ota_worker.finished.connect(self._ota_thread.quit)
        self._ota_worker.error.connect(self._ota_thread.quit)
        self._ota_thread.finished.connect(self._ota_worker.deleteLater)
        self._ota_thread.finished.connect(self._ota_thread_finished)
        self._ota_thread.finished.connect(self._ota_thread.deleteLater)
        self._update_operation_controls()
        self._ota_thread.start()

    def _ota_progress_changed(self, sent_bytes: int, total_bytes: int) -> None:
        percent = int(sent_bytes * 100 / total_bytes) if total_bytes else 0
        self.ota_progress.setValue(percent)
        self.ota_status_label.setText(
            f"上传中…… {sent_bytes} / {total_bytes} 字节"
        )

    def _ota_status_changed(self, status: str) -> None:
        status_text = {
            "Preparing...": "准备中……",
            "Sending BEGIN...": "发送 BEGIN……",
            "Uploading...": "上传中……",
            "Finishing...": "完成中……",
            "Refreshing device...": "刷新设备信息……",
        }.get(status, status)
        self.ota_status_label.setText(status_text)
        self.status_label.setText(status_text)

    def _ota_finished(self, result: OtaWorkerResult) -> None:
        self.ota_progress.setValue(100)
        self.ota_status_label.setText("成功")
        self.status_label.setText("升级完成")
        self.append_log("OTA 升级成功")
        self.append_log(
            f"目标槽位：{result.ota_result.target_slot}；"
            f"版本：{result.ota_result.version}；"
            f"大小：{result.ota_result.size} 字节；"
            f"CRC32：0x{result.ota_result.crc32:08X}"
        )
        if result.refresh_pending:
            self.append_log("设备信息刷新等待中")
            if result.refresh_error:
                self.append_log(f"错误：设备信息刷新失败：{result.refresh_error}")
            # Do not leave a stale plan marked Ready when the post-OTA query
            # could not confirm the device state.
            self._device_info = None
            self._clear_device_display()
            self.update_upgrade_plan()
            return

        if result.refreshed_info is not None:
            info = result.refreshed_info
            self._device_info = info
            self.current_slot_label.setText(self._slot_text(info.current_slot))
            self._update_slot_card("A", info.slot_a)
            self._update_slot_card("B", info.slot_b)
            self.append_log(f"当前运行槽位：{self._slot_text(info.current_slot)}")
            self.append_log("设备信息刷新完成")
            self.update_upgrade_plan()

    def _ota_failed(self, message: str) -> None:
        self.ota_status_label.setText("失败")
        self.status_label.setText("升级失败")
        self.append_log(f"错误：{message}")

    def _ota_thread_finished(self) -> None:
        self._ota_thread = None
        self._ota_worker = None
        self._update_operation_controls()

    def closeEvent(self, event: Any) -> None:  # noqa: N802 - Qt override
        if self._ota_thread is not None and self._ota_thread.isRunning():
            QMessageBox.information(
                self,
                "OTA 正在进行",
                "请等待当前 OTA 操作完成。",
            )
            event.ignore()
            return
        if self._ble_worker is not None and self._ble_thread is not None:
            self._ble_worker.shutdown()
            self._ble_thread.quit()
            self._ble_thread.wait(2000)
            self._ble_worker = None
            self._ble_thread = None
        event.accept()
