#include "boot_metadata.h"
#include "boot_flash.h"

#include <string.h>

static uint32_t BootMeta_GetAddress(BootSlot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return SLOT_A_META_ADDR;
    }

    if (slot == BOOT_SLOT_B)
    {
        return SLOT_B_META_ADDR;
    }

    return 0U;
}

bool BootMeta_IsValid(BootSlot_t slot,
                      const BootSlotMetadata_t *metadata)
{
	uint32_t slot_size;
	
	if ( metadata == NULL)									
		return false;
	if ( metadata->magic != BOOT_META_MAGIC)
		return false;
	if (metadata->slot_id != slot)
		return false;
	
	if (slot == BOOT_SLOT_A)
		slot_size = SLOT_A_SIZE;
	else if (slot == BOOT_SLOT_B)
		slot_size = SLOT_B_SIZE;
	else
		return false;
	
	if (metadata->state != (uint32_t)SLOT_STATE_EMPTY)
	{
		if ((metadata->firmware_size == 0U) ||
				(metadata->firmware_size > slot_size))
		{
			return false;
		}
	}
	return true;
}

bool BootMeta_Read(BootSlot_t slot,
                   BootSlotMetadata_t *metadata)
{
	uint32_t address;

	volatile const uint32_t *flash_data;
	uint32_t *metadata_data;
	if ( metadata	== NULL)
		return false;
	address = BootMeta_GetAddress(slot);
	if (address == 0u)
		return false;
	
	flash_data = (uint32_t *)address;
	
	metadata_data = (uint32_t *)metadata;
	
	for(uint32_t i = 0u; 
			i< (sizeof(BootSlotMetadata_t) / sizeof(uint32_t));
			i++)
	*(metadata_data + i) = *(flash_data + i);
	
	return BootMeta_IsValid(slot, metadata);
}

bool BootMeta_Write(BootSlot_t slot,
                    const BootSlotMetadata_t *metadata)
{
	uint32_t address;
	uint32_t page_data[FLASH_PAGE_BYTES / sizeof(uint32_t)];

	if (!BootMeta_IsValid(slot, metadata))
	{
		return false;
	}	
	
	address = BootMeta_GetAddress(slot);
	
	if (address == 0u)
		return false;
	
	memset(
    page_data,
    0xFF,
    sizeof(page_data));
	
	memcpy(page_data,
				 metadata,
				 sizeof(BootSlotMetadata_t));	
	
	if (!BootFlash_ErasePage(address))
		return false;
	if (!BootFlash_WritePage(address, page_data))
		return false;
	if (!BootFlash_VerifyPage(address, page_data))
		return false;
	
	return true;
}

//检查固件尝试启动次数是不是大于BOOT_MAX_ATTEMPTS，是的话标记为BAD
bool BootMeta_CheckBootAttempts(BootSlot_t slot,
                                BootSlotMetadata_t *metadata)
{
	if (metadata == NULL)
	{
		return false;
	}

	if (metadata->state != (uint32_t)SLOT_STATE_PENDING)
	{
		return true;
	}

	if (metadata->boot_attempt < BOOT_MAX_ATTEMPTS)
	{
		return true;
	}

	metadata->state =
			(uint32_t)SLOT_STATE_BAD;

	if (!BootMeta_Write(slot, metadata))
	{
		return false;
	}

	return true;	
}

bool BootMeta_RecordBootAttempt(BootSlot_t slot,
                                BootSlotMetadata_t *metadata)
{
	if (metadata == NULL)
	{
		return false;
	}

	if (metadata->state != (uint32_t)SLOT_STATE_PENDING)
	{
		return true;
	}

	metadata->boot_attempt++;

	if (!BootMeta_Write(slot, metadata))
	{
		return false;
	}

	return true;
}

BootSlot_t App_GetCurrentSlot(void)
{
#if defined(APPLICATION_A_BUILD)

	return BOOT_SLOT_A;

#elif defined(APPLICATION_B_BUILD)

	return BOOT_SLOT_B;

#else

	return BOOT_SLOT_NONE;

#endif
}

bool App_ConfirmCurrentSlot(void)
{
	BootSlot_t slot;
	BootSlotMetadata_t metadata;

	slot = App_GetCurrentSlot();

	if (slot == BOOT_SLOT_NONE)
	{
		return false;
	}

	if (!BootMeta_Read(slot, &metadata))
	{
		return false;
	}

	/* 已经是正式版本，不需要重复擦写 Flash */
	if (metadata.state == (uint32_t)SLOT_STATE_CONFIRMED)
	{
		return true;
	}

	/* 只有 PENDING 才允许转成 CONFIRMED */
	if (metadata.state != (uint32_t)SLOT_STATE_PENDING)
	{
		return false;
	}

	metadata.state =
		(uint32_t)SLOT_STATE_CONFIRMED;

	metadata.boot_attempt = 0U;

	if (!BootMeta_Write(slot, &metadata))
	{
		return false;
	}

	return true;
}
