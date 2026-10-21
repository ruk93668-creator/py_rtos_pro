#ifndef __BOOT_METADATA_H
#define __BOOT_METADATA_H

#include <stdint.h>
#include <stdbool.h>

#include "boot_config.h"
#include "boot_select.h"

#define BOOT_META_MAGIC 0X4D455441U

#define SLOT_A_META_ADDR         META_START_ADDR
#define SLOT_B_META_ADDR         (META_START_ADDR + FLASH_PAGE_BYTES)

typedef struct{
	uint32_t magic;
	uint32_t slot_id;
	uint32_t version;
	uint32_t firmware_size;
	uint32_t firmware_crc32;
	uint32_t state;
	uint32_t boot_attempt;
	uint32_t reserved;
}BootSlotMetadata_t;

bool BootMeta_Read(BootSlot_t slot,
                   BootSlotMetadata_t *metadata);

bool BootMeta_Write(BootSlot_t slot,
                    const BootSlotMetadata_t *metadata);

bool BootMeta_IsValid(BootSlot_t slot,
                      const BootSlotMetadata_t *metadata);

bool BootMeta_CheckBootAttempts(BootSlot_t slot,
                                BootSlotMetadata_t *metadata);

bool BootMeta_RecordBootAttempt(BootSlot_t slot,
                                BootSlotMetadata_t *metadata);

bool App_ConfirmCurrentSlot(void);

BootSlot_t App_GetCurrentSlot(void);
#endif
