"""连接模式及其对外可见标志位。"""

from __future__ import annotations

from enum import IntEnum


class ConnectionMode(IntEnum):
    """GUI 当前选择的连接通道。数值可直接作为设备侧选择标志位。"""

    TYPEC_UART = 0
    BLE_UART = 1

    @property
    def display_name(self) -> str:
        return {
            ConnectionMode.TYPEC_UART: "Type-C",
            ConnectionMode.BLE_UART: "Bluetooth",
        }[self]

    @property
    def flag_name(self) -> str:
        return {
            ConnectionMode.TYPEC_UART: "TYPEC-UART",
            ConnectionMode.BLE_UART: "BLE-UART",
        }[self]


TYPEC_UART_FLAG = int(ConnectionMode.TYPEC_UART)
BLE_UART_FLAG = int(ConnectionMode.BLE_UART)

__all__ = [
    "BLE_UART_FLAG",
    "ConnectionMode",
    "TYPEC_UART_FLAG",
]
