# OTA 功能阶段工作报告

日期：2026 年 9 月 9 日

本阶段围绕 PY32F071 项目的 OTA 升级需求，完成了 Bootloader 基础模块的搭建，包括双目标工程配置、Flash 分区、应用入口检查与跳转代码、Flash 页擦写及读回校验，为后续接入固件传输和升级流程提供基础。当前处于 Bootloader 基础开发与测试阶段，完整 OTA 升级流程尚未接通。

**已完成工作**

1. **配置 Bootloader 与 Application 两个编译目标。** 使用 `BOOTLOADER_BUILD` 和 `APPLICATION_BUILD` 区分构建，分别组织引导程序与原有应用代码。Bootloader 保留启动、必要 HAL 模块和 boot 目录代码，应用侧继续使用原有 App / Service / Driver / BSP / FreeRTOS 结构。
2. **规划 Flash 存储分区并配置程序区域。** Bootloader 使用前 16 KB，Application 从 `0x08004000` 开始，尾部预留 4 KB 数据区。Application 的系统初始化依据编译宏设置 `0x4000` 向量表偏移，与应用起始位置对应。
3. **编写应用检查和跳转接口。** `Boot_IsAppValid()` 检查初始栈指针范围、复位入口地址范围及 Thumb 标志；`Boot_JumpToApp()` 编写了关闭中断、停止 SysTick、切换向量表、设置 MSP 和调用应用复位入口的流程。这属于入口格式与范围检查，尚不包含完整固件 CRC 或签名校验。
4. **实现 Flash 基础操作接口。** 提供单页擦除、256 字节页写入及逐字读回比较功能，并加入地址范围、页对齐、写入和校验数据指针检查，以及 Flash 解锁、加锁和 HAL 操作结果处理。目前擦写仅允许在尾部 4 KB 数据区内执行，尚未开放应用固件区写入。
5. **编写 Flash 测试程序与 Bootloader 时基。** 测试入口按擦除、写入、校验顺序执行，并使用 `g_erase_ok`、`g_write_ok`、`g_verify_ok` 保留结果供调试器观察。增加 Bootloader 的 `SysTick_Handler()`，调用 `HAL_IncTick()`。当前测试在写入后主动修改参考数据的第 11 个字，用于验证不一致检测；写入成功时，该用例预期得到校验失败结果。

| 区域 | 地址范围（含首尾） | 容量 |
| --- | --- | --- |
| Bootloader | `0x08000000`～`0x08003FFF` | 16 KB |
| Application | `0x08004000`～`0x0801EFFF` | 108 KB |
| 预留数据区 | `0x0801F000`～`0x0801FFFF` | 4 KB |

**验证与当前进度**

现有本地构建日志显示，Bootloader 已完成编译和链接，结果为 **0 错误、0 警告**，`Code=2924`、`RO-data=256`、`RW-data=20`、`ZI-data=1916`。这是已有构建记录，本次报告整理没有重新构建、烧录或执行 Flash 擦写。

当前主函数仍为 Flash 测试入口，尚未调用 `Boot_JumpToApp()`；现有链接映射也显示跳转函数因未被引用而被移除。因此，当前成果可表述为“完成 Flash 基础接口及应用跳转代码编写，Bootloader 测试程序编译通过”，还不能表述为“已完成 OTA 升级或应用跳转实板验证”。现有主机游戏测试也未覆盖新增 boot 模块。

**下一阶段计划**

1. 记录 Flash 正常写入校验、数据不一致、越界地址及非页对齐等用例的实际结果。
2. 将应用有效性判断和跳转接入 Bootloader 主流程，完善中断及外设状态清理，验证应用跳转后 FreeRTOS 与外设运行。
3. 接入固件传输通道、分包接收和超时重试，补充固件长度、版本、完整性校验及升级状态管理。
4. 在现有分区基础上设计应用区更新及掉电恢复策略，完成从接收固件到更新、校验、启动应用的完整流程。
5. 分离两个目标的输出目录和文件名。目前二者共用 `Objects/yxj.axf`，需避免切换目标构建时覆盖产物。

**代码与记录依据**

- [Flash 页操作](D:/PY32/yxj/py_rtos_pro/boot/boot_flash.c)
- [应用检查及跳转](D:/PY32/yxj/py_rtos_pro/boot/boot_jump.c)
- [Flash 测试入口](D:/PY32/yxj/py_rtos_pro/boot/boot_mian.c)
- [Bootloader SysTick](D:/PY32/yxj/py_rtos_pro/boot/boot_it.c)
- [向量表偏移配置](D:/PY32/yxj/py_rtos_pro/core/Src/system_py32f071.c:54)
- [双目标工程配置](D:/PY32/yxj/py_rtos_pro/MDK-ARM/yxj.uvprojx)
- [现有构建日志](D:/PY32/yxj/py_rtos_pro/MDK-ARM/Objects/yxj.build_log.htm)及[链接映射](D:/PY32/yxj/py_rtos_pro/MDK-ARM/Listings/yxj.map)（后续构建会更新）
