#ifndef __BOOT_JUMP_H
#define	__BOOT_JUMP_H

#include <stdint.h>
#include <stdbool.h>

#include "boot_config.h"

#define SRAM_START_ADDR    0x20000000U
#define SRAM_END_ADDR      0x20004000U

bool Boot_IsSlotValid(uint32_t slot_start,
                      uint32_t slot_end);

void Boot_JumpToSlot(uint32_t slot_start);


#endif
