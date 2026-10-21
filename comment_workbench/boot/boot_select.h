#ifndef __BOOT_SELECT_H
#define __BOOT_SELECT_H

#include <stdint.h>
#include <stdbool.h>

typedef enum
{
    BOOT_SLOT_NONE = 0,
    BOOT_SLOT_A,
    BOOT_SLOT_B

} BootSlot_t;

typedef enum
{
    SLOT_STATE_EMPTY = 0,
    SLOT_STATE_DOWNLOADING,
    SLOT_STATE_PENDING,
    SLOT_STATE_CONFIRMED,
    SLOT_STATE_BAD
/*
SLOT_STATE_EMPTY：空槽位。这个 Slot 里没有有效固件，或者还没使用。
SLOT_STATE_DOWNLOADING：正在下载/写入固件。这个状态下如果断电，通常认为该 Slot 不可靠。
SLOT_STATE_PENDING：固件已经完整写入，CRC 等校验也通过了，但还没有证明“真的能正常启动运行”。可以理解成“待确认”。
SLOT_STATE_CONFIRMED：已经确认可正常启动、运行，是可靠固件。这个状态通常就是你的“保底版本”。
SLOT_STATE_BAD：坏槽位。比如 CRC 错误、启动多次失败、固件损坏等。
*/
} SlotState_t;

BootSlot_t Boot_SelectSlot(bool slot_a_valid,
                           uint32_t slot_a_version,
                           bool slot_b_valid,
                           uint32_t slot_b_version);//选择启动版本选择算法
bool Boot_IsStateBootable(uint32_t state);
#endif
