# Bootloader / OTA 源码注释工作副本

## 推荐阅读顺序

1. D:/PY32/yxj/py_rtos_pro/boot/boot_config.h
2. D:/PY32/yxj/py_rtos_pro/boot/boot_select.h
3. D:/PY32/yxj/py_rtos_pro/boot/boot_select.c
4. D:/PY32/yxj/py_rtos_pro/boot/boot_metadata.h
5. D:/PY32/yxj/py_rtos_pro/boot/boot_metadata.c
6. D:/PY32/yxj/py_rtos_pro/boot/boot_crc.h
7. D:/PY32/yxj/py_rtos_pro/boot/boot_crc.c
8. D:/PY32/yxj/py_rtos_pro/boot/boot_flash.h
9. D:/PY32/yxj/py_rtos_pro/boot/boot_flash.c
10. D:/PY32/yxj/py_rtos_pro/boot/boot_jump.h
11. D:/PY32/yxj/py_rtos_pro/boot/boot_jump.c
12. D:/PY32/yxj/py_rtos_pro/boot/app_update.h
13. D:/PY32/yxj/py_rtos_pro/boot/app_update.c
14. D:/PY32/yxj/py_rtos_pro/boot/boot_mian.c
15. D:/PY32/yxj/py_rtos_pro/boot/boot_it.c
16. D:/PY32/yxj/py_rtos_pro/boot/boot_protocol.h
17. D:/PY32/yxj/py_rtos_pro/boot/boot_protocol.c
18. D:/PY32/yxj/py_rtos_pro/Service/ota_protocol.h
19. D:/PY32/yxj/py_rtos_pro/Service/ota_protocol.c
20. D:/PY32/yxj/py_rtos_pro/Service/ota_receiver.h
21. D:/PY32/yxj/py_rtos_pro/Service/ota_receiver.c
22. D:/PY32/yxj/py_rtos_pro/Driver/ota_uart.h
23. D:/PY32/yxj/py_rtos_pro/Driver/ota_uart.c
24. D:/PY32/yxj/py_rtos_pro/BSP/bsp_usart.h
25. D:/PY32/yxj/py_rtos_pro/BSP/bsp_usart.c
26. D:/PY32/yxj/py_rtos_pro/BSP/bsp_init.h
27. D:/PY32/yxj/py_rtos_pro/BSP/bsp_init.c
28. D:/PY32/yxj/py_rtos_pro/BSP/bsp_gpio.h
29. D:/PY32/yxj/py_rtos_pro/BSP/bsp_gpio.c
30. D:/PY32/yxj/py_rtos_pro/BSP/bsp_iic.h
31. D:/PY32/yxj/py_rtos_pro/Driver/ble.h
32. D:/PY32/yxj/py_rtos_pro/Driver/ble.c
33. D:/PY32/yxj/py_rtos_pro/App/app_settings.h
34. D:/PY32/yxj/py_rtos_pro/App/app_settings.c
35. D:/PY32/yxj/py_rtos_pro/App/app_main.h
36. D:/PY32/yxj/py_rtos_pro/App/app_main.c
37. D:/PY32/yxj/py_rtos_pro/App/game_id.h
38. D:/PY32/yxj/py_rtos_pro/App/game_manager.h
39. D:/PY32/yxj/py_rtos_pro/Service/service_enmu.h
40. D:/PY32/yxj/py_rtos_pro/Service/service_display.h
41. D:/PY32/yxj/py_rtos_pro/Service/service_main.h
42. D:/PY32/yxj/py_rtos_pro/core/Inc/main.h
43. D:/PY32/yxj/py_rtos_pro/core/Src/main.c

## 文件关系

- D:/PY32/yxj/py_rtos_pro/boot/boot_config.h ↔ 仅头文件，提供 Boot、Slot、Metadata 地址和相关配置。
- D:/PY32/yxj/py_rtos_pro/boot/boot_select.h ↔ boot/boot_select.c
- D:/PY32/yxj/py_rtos_pro/boot/boot_metadata.h ↔ boot/boot_metadata.c
- D:/PY32/yxj/py_rtos_pro/boot/boot_crc.h ↔ boot/boot_crc.c
- D:/PY32/yxj/py_rtos_pro/boot/boot_flash.h ↔ boot/boot_flash.c
- D:/PY32/yxj/py_rtos_pro/boot/boot_jump.h ↔ boot/boot_jump.c
- D:/PY32/yxj/py_rtos_pro/boot/app_update.h ↔ boot/app_update.c
- D:/PY32/yxj/py_rtos_pro/boot/boot_protocol.h ↔ boot/boot_protocol.c
- D:/PY32/yxj/py_rtos_pro/Service/ota_protocol.h ↔ Service/ota_protocol.c
- D:/PY32/yxj/py_rtos_pro/Service/ota_receiver.h ↔ Service/ota_receiver.c
- D:/PY32/yxj/py_rtos_pro/Driver/ota_uart.h ↔ Driver/ota_uart.c
- D:/PY32/yxj/py_rtos_pro/BSP/bsp_usart.h ↔ BSP/bsp_usart.c
- D:/PY32/yxj/py_rtos_pro/BSP/bsp_init.h ↔ BSP/bsp_init.c
- D:/PY32/yxj/py_rtos_pro/BSP/bsp_gpio.h ↔ BSP/bsp_gpio.c
- D:/PY32/yxj/py_rtos_pro/BSP/bsp_iic.h ↔ 仅头文件，供 BSP 初始化代码声明 I2C 接口。
- D:/PY32/yxj/py_rtos_pro/Driver/ble.h ↔ Driver/ble.c
- D:/PY32/yxj/py_rtos_pro/App/app_settings.h ↔ App/app_settings.c
- D:/PY32/yxj/py_rtos_pro/App/app_main.h ↔ App/app_main.c
- D:/PY32/yxj/py_rtos_pro/App/game_id.h ↔ App/game_manager.h / Service/service_enmu.h 使用的游戏标识定义。
- D:/PY32/yxj/py_rtos_pro/App/game_manager.h ↔ 仅头文件，声明游戏管理接口。
- D:/PY32/yxj/py_rtos_pro/Service/service_enmu.h ↔ 仅头文件，声明输入和菜单服务接口。
- D:/PY32/yxj/py_rtos_pro/Service/service_display.h ↔ 仅头文件，声明显示服务接口。
- D:/PY32/yxj/py_rtos_pro/Service/service_main.h ↔ 仅头文件，声明 Service 初始化和处理接口。
- D:/PY32/yxj/py_rtos_pro/core/Inc/main.h ↔ core/Src/main.c
- D:/PY32/yxj/py_rtos_pro/boot/boot_it.c ↔ 无对应头文件。

## 大致调用关系

- boot/boot_mian.c 读取 Metadata，结合 boot/boot_select.c、boot/boot_crc.c 和 boot/boot_jump.c 选择并跳转到 Slot A 或 Slot B。
- boot/app_update.c 通过 boot/boot_config.h、boot/boot_metadata.c、boot/boot_flash.c 和 boot/boot_crc.c 完成目标槽擦除、写入、校验及状态更新。
- Service/ota_receiver.c 接收字节并组帧，然后调用 Service/ota_protocol.c 解析和处理 OTA 帧。
- Service/ota_protocol.c 处理 BEGIN、DATA、END、ABORT、QUERY_INFO，并调用 boot/app_update.c 与 boot/boot_metadata.c。
- Driver/ota_uart.c 通过 BSP/bsp_usart.c 接收和发送串口数据，并把接收字节交给 Service/ota_receiver.c。
- BSP/bsp_init.c 初始化 BSP 串口；core/Src/main.c 初始化应用服务并创建 OTA 处理任务，任务调用 Driver/ota_uart.c 的处理接口。
- App/app_settings.c 使用 Driver/ota_uart.c、Driver/ble.c 和 boot/boot_metadata.c 管理 Type-C/BLE 端口标志及当前版本显示。
- App/app_main.c 管理设置页面入口；core/Src/main.c 负责系统启动、任务创建和当前 Slot 确认。
- boot/boot_protocol.h 与 boot/boot_protocol.c 保留为工程中的独立 Boot 协议定义，阅读时与 Service/ota_protocol.h/.c 对照。
