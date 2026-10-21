#ifndef __BOOT_CONFIG_H
#define __BOOT_CONFIG_H

/* Flash ×Ü·¶Î§ */
#define FLASH_START_ADDR        0x08000000U
#define FLASH_END_ADDR          0x08020000U

/* Flash Page */
#define FLASH_PAGE_BYTES        0x00000100U

/* Bootloader */
#define BOOT_START_ADDR         0x08000000U
#define BOOT_END_ADDR           0x08004000U
#define BOOT_SIZE               (BOOT_END_ADDR - BOOT_START_ADDR)

/* Slot A */
#define SLOT_A_START_ADDR       0x08004000U
#define SLOT_A_END_ADDR         0x08011800U
#define SLOT_A_SIZE             (SLOT_A_END_ADDR - SLOT_A_START_ADDR)

/* Slot B */
#define SLOT_B_START_ADDR       0x08011800U
#define SLOT_B_END_ADDR         0x0801F000U
#define SLOT_B_SIZE             (SLOT_B_END_ADDR - SLOT_B_START_ADDR)

/* Metadata */
#define META_START_ADDR         0x0801F000U
#define META_END_ADDR           0x08020000U
#define META_SIZE               (META_END_ADDR - META_START_ADDR)

//
#define BOOT_MAX_ATTEMPTS    		3U
#endif
