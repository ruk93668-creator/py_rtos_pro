# PY32F071 FreeRTOS A/B OTA project

本工程包含 PY32F071 + FreeRTOS 固件、软件 A/B 分区 OTA、OLED 游戏界面，以及 `shangweiji/` 下的 Roxy OTA 上位机。

## 工程内容

- `MDK-ARM/yxj.uvprojx`：Keil 工程，包含 `Application_A`、`Application_B` 和 `Bootloader` 目标。
- `boot/`：启动选择、Metadata、回滚和固件更新逻辑。
- `App/`、`Service/`、`Driver/`、`BSP/`：应用、服务和硬件驱动源码。
- `roxy_ota_send.py`：命令行 OTA 工具。
- `shangweiji/`：PySide6 Roxy OTA GUI 及其测试。
- `info.ciallo`：可移植固件包输出（由 `python roxy_ota_send.py --ciallo` 生成，不作为构建输出提交）。

## 离线测试

```powershell
powershell -ExecutionPolicy Bypass -File Tools/run_tests.ps1
powershell -ExecutionPolicy Bypass -File Tools/check_upper_armcc.ps1
```

构建生成的对象文件、Keil 列表文件、Python 缓存和 GUI 打包目录已通过 `.gitignore` 排除。
