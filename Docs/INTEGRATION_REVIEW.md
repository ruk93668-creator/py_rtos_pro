# 底层修复与工程接入说明

这份说明以当前工程源代码为依据。你已授权原文件小幅度修改，`proposed_low_level.patch` 中的五项修改已应用到 BSP、Driver 和 core；`MDK-ARM/yxj.uvprojx` 也已补齐 App / Service 源文件登记。业务代码仍在原有 App / Service 分层内，HAL、启动文件、FreeRTOS 配置和硬件映射未改动。

## 修改范围

| 原文件 | 已实施修改 | 原因 |
| --- | --- | --- |
| `BSP/bsp_iic.h` | 声明同步 `BSP_IIC_ReadRegs()` | 当前只有单寄存器读、多寄存器写和裸接收，MPU 需要从指定寄存器连续读取。 |
| `BSP/bsp_iic.c` | 新增一段 `HAL_I2C_Mem_Read()` 包装 | 沿用当前总线配置、设备地址左移、8 位寄存器地址和 50 ms 超时。 |
| `Driver/mpu_6050.c` | `MPU_Read_Len()` 的 `BSP_IIC_WriteRegs()` 改为 `BSP_IIC_ReadRegs()` | 现有函数名虽然是读取，实际却把未初始化缓冲区送去写，三类测量 getter 都受影响。 |
| `BSP/bsp_gpio.c` | PA3 / PA5 的 A / B 通道中断改为上升沿和下降沿 | 现有解码器以四次完整 AB 转移累计一格，单下降沿配置会漏掉所需状态。 |
| `core/Src/main.c` | 增加现有 BSP / Service / App 初始化、填充原三个任务、检查任务创建结果 | 当前没有设备初始化，三个空任务无限运行，最高优先级任务持续占用处理器。 |

硬件映射、设备地址、I2C 同步模型、初始化寄存器参数、时钟、HAL、启动汇编、中断转发和 FreeRTOS 配置均沿用本工程。补丁未启用残留的 `Driver/driver_main.c`；该文件和 `Driver/README_DRIVER.md` 描述的异步 API 在当前 BSP / Driver 中不存在，不能当作本次实现的依据。

## 初始化和任务接入

初始化顺序：`HAL_Init()` → 原 `APP_SystemClockConfig()` → `bsp_init()` → `ServiceMain_Init()` → `AppMain_Init()` → 创建原三个任务 → `vTaskStartScheduler()`。

`ServiceMain_Init()` 属于新增的 Service 实现，负责 KEY / OLED / MPU 的上层初始化组织。`bsp_init()` 失败、任务创建失败或调度器意外返回时，进入原有 `APP_ErrorHandler()`。

| 原任务 | 原优先级（保留） | 采用栈深度 | 周期 | 调用 |
| --- | --- | --- | --- | --- |
| Task1 | 1 | 256 words，即 1024 字节 | 10 ms | `AppMain_Process(dt_ms)`；从 `HAL_GetTick()` 的无符号差值传递实际经过时间。 |
| Task2 | 2 | 192 words，即 768 字节 | 5 ms | `ServiceMain_Process()`，处理输入服务。 |
| Task3 | 3 | 192 words，即 768 字节 | 20 ms | `SensorService_Process()`，更新运动传感器服务。 |

每个任务都使用 `vTaskDelayUntil()` 让出处理器。此处是调度目标周期，同步 I2C 调用的耗时及故障超时可能使任务超期，不能把这些数值当成硬实时保证。栈深度仍需在实际固件运行负载下验证余量。

应用层源文件为 `app_main.c`、`game_arcade.c`、`game_dino.c`、`game_manager.c`、`game_util.c`；当前第二项菜单为 `SNAKE`，第三项 `PLANE` 使用拨轮控制。`MDK-ARM/yxj.uvprojx` 已登记这些 App 文件以及 `service_display.c`、`service_main.c`，原 `service_enmu.c`、`service_mpu_6050.c` 也在工程内。

## 底层问题的代码依据

- `Driver/mpu_6050.c:81` 定义 `MPU_Read_Len()`，第 87 行调用写接口；`BSP/bsp_iic.c:101` 的写接口转调 `HAL_I2C_Mem_Write()`。补丁只补充与现有单寄存器读相同风格的连续读接口，并修正这一处调用。
- `BSP/bsp_gpio.c:27` 和第 50 行配置 `GPIO_MODE_IT_FALLING`；`Driver/key.c:138` 对跨两位跳变清零，第 189 / 198 行要求累计达到 ±4。双边沿配置使中断输入符合现有状态表的前提。PA4 按键保持输入模式，由 Service 做消抖与点击识别。
- `core/Src/main.c:62` 起创建三个任务，原第 75 / 87 / 99 行对应空循环。保留任务名称和优先级，仅填充业务调用与周期等待。

## 验证状态和复核方法

应用层与 Service 层已在当前工程中完成，且没有调用参考工程的异步 I2C API。`Tools/run_tests.ps1` 使用真实 App/Service/OLED 绘图代码和主机替身，覆盖按键消抖、短按/长按、旋钮队列、MPU 数据失效与重试、体感滞回、七项菜单、七个游戏的关键操作/重开，以及 OLED 边界绘制和刷新限频；运行结果为 `ALL HOST TESTS PASSED`。`Tools/check_upper_armcc.ps1` 用当前 MDK 的 ARMCC 5.06 参数逐个编译 9 个 App/Service 源文件，结果为 `PASS ARMCC compile-only`。

实际工程的 uVision/ARMCC 构建已重新编译并链接：`Code=24564`、`RO-data=900`、`RW-data=240`、`ZI-data=13344`，生成 `yxj.axf`，0 错误、0 警告。授权前的隔离副本报告仍保留在 `Tests/out/proposed_firmware_build.json`，其中 `protected_files_unchanged` 为 `true`，完整编译暴露出原有 `Driver/key.c` 文件末尾缺换行的 1 个警告；本次没有改这个文件。主机测试、隔离链接和报告都不能替代实板烧录与电气/时序验收。

已完成：按原始字节生成统一补丁，保留原文件编码及换行；App / Service 源文件登记已用实际 uVision 构建验证并解决 `DisplayService_*` 未定义符号；五文件补丁已经应用，构建结果写入 `Tests/out/current_build.log`。补丁文件保留用于审阅和追溯；它当前不能再次套用到已修改的工作树。

这些检查证明源码可以编译链接和应用层行为通过，但不代表固件已经烧录或板上功能通过。仍需在实际硬件上完成电气、时序和交互验收。

本机已有 `D:\32\UV4\UV4.exe` 和 `D:\32\ARM\ARMCC\bin\armcc.exe`。原 MDK 目标为 `Project`，使用 ARMCC 5.06 update 7、C99、PY32F071xB，128 KB Flash / 16 KB SRAM。现有旧构建日志不是本次验证结果。

可复核的隔离构建入口是 `python Tools/check_proposed_firmware.py`。脚本复制所需固件目录到新建 TEMP 目录，只向该副本应用提案，再隐藏运行原 uVision 构建；不烧录设备。原工程中所有受保护目录的文件摘要会在运行前后对比，结果写入 `Tests/out/proposed_firmware_build.json`，副本及构建日志保留在报告列出的 TEMP 位置。

板上复核应覆盖：OLED 菜单显示与刷新、编码器正反转单格/快转、按键短按/长按、MPU 静止和姿态变化数据、初始化失败时界面可用性、各游戏进入/退出和暂停恢复。传感器方向和旋转方向可能因装配方向不同，需要按实际硬件确认。

## 本次实际修改

已应用上述五个文件中的最小修改：两个底层逻辑问题的修复，以及保持现有三任务组织方式的启动和周期调用。随后在 App / Service 层增加了菜单上电启动保护、第二项 `SNAKE` 游戏和第三项飞机的拨轮专用控制。`MDK-ARM/yxj.uvprojx` 的源文件登记只改变工程编译清单，不改变运行时架构。没有改动 HAL、启动汇编、中断转发、FreeRTOS 配置或 `Driver/key.c`。

## 当前受保护文件 SHA-256

以下摘要对应本次修改和构建完成后的工程状态；`Docs/protected_files.sha256.json` 同步记录了受保护目录（含工程生成物）的完整摘要。

| 文件 | SHA-256 |
| --- | --- |
| `BSP/bsp_iic.h` | `bbea2a9190ddf739d8a46096acb06f06ee444c310801d773bbb405e3f91ea7b8` |
| `BSP/bsp_iic.c` | `f7d15295f2d4a96e93cf8bb4ef4a0cb37a894b1e2433963e51a5a6498f63ff26` |
| `BSP/bsp_gpio.c` | `2b03e293bc9adfaaff7d23a32830cd230812b6e21e8cf48f7084b6e73df258fc8` |
| `Driver/mpu_6050.c` | `a5f7209fad20435247395d0c408787b1fa72f9d1cf4a4d0250a05bc19e2a30a1` |
| `core/Src/main.c` | `f88b208d99af047a845c652a795ec3a392f1aeaf8c01ad0ea6fe3f6ba095f289` |
| `MDK-ARM/yxj.uvprojx` | `d26589a6d641b3ebf7676f848a43ec25bca15d73fd1478706de661bf7cfa7c9d` |
