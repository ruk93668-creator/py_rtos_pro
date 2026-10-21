#include "app_update.h"

#include "boot_config.h"
#include "boot_metadata.h"
#include "boot_flash.h"
#include "boot_crc.h"

#include <string.h>

static bool s_update_active = false;

static BootSlot_t s_update_slot = BOOT_SLOT_NONE;

static uint32_t s_next_offset = 0U;

static uint32_t s_firmware_size = 0U;
/*
	s_update_active
	→ 当前是否正在升级

	s_update_slot
	→ 当前正在写 A 还是 B

	s_next_offset
	→ 下一包必须从哪里开始

	s_firmware_size
	→ 本次完整固件大小
*/

static BootSlot_t App_GetUpdateSlot(void)
{
    BootSlot_t current_slot;

    current_slot = App_GetCurrentSlot();

    if (current_slot == BOOT_SLOT_A)
    {
        return BOOT_SLOT_B;
    }

    if (current_slot == BOOT_SLOT_B)
    {
        return BOOT_SLOT_A;
    }

    return BOOT_SLOT_NONE;
}


static uint32_t App_GetSlotStart(BootSlot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return SLOT_A_START_ADDR;
    }

    if (slot == BOOT_SLOT_B)
    {
        return SLOT_B_START_ADDR;
    }

    return 0U;
}


static uint32_t App_GetSlotSize(BootSlot_t slot)
{
    if (slot == BOOT_SLOT_A)
    {
        return SLOT_A_SIZE;
    }

    if (slot == BOOT_SLOT_B)
    {
        return SLOT_B_SIZE;
    }

    return 0U;
}

static void App_UpdateResetSession(void)
{
    s_update_active = false;
    s_update_slot = BOOT_SLOT_NONE;
    s_next_offset = 0U;
    s_firmware_size = 0U;
}

bool App_UpdateAbort(void)
{
	BootSlotMetadata_t metadata;

	if (!s_update_active)
	{
		return false;
	}

	/*
	* 不把目标 Slot 改回 EMPTY / BAD。
	*
	* 保持 DOWNLOADING：
	* 表示这个 Slot 里的固件是不完整的，
	* Bootloader 绝对不能启动它。
	*/
	if (!BootMeta_Read(s_update_slot, &metadata))
	{
		App_UpdateResetSession();
		return false;
	}

	if (metadata.state !=
		(uint32_t)SLOT_STATE_DOWNLOADING)
	{
		App_UpdateResetSession();
		return false;
	}

	/*
	* Flash Metadata 本身已经是 DOWNLOADING，
	* 所以这里不需要再次擦写 Metadata。
	*
	* 只结束当前 RAM 中的 OTA 会话。
	*/
	App_UpdateResetSession();

	return true;
}

bool App_UpdateBegin(uint32_t version,
                     uint32_t firmware_size,
                     uint32_t firmware_crc32)
{
	BootSlot_t update_slot;
	BootSlotMetadata_t metadata;
	uint32_t slot_size, slot_start, address;
	
	update_slot = App_GetUpdateSlot();
	
	if (update_slot == BOOT_SLOT_NONE)
	{
		return false;
	}	
	
	slot_size = App_GetSlotSize(update_slot);
	slot_start = App_GetSlotStart(update_slot);

	if ((firmware_size == 0U) ||
		(firmware_size > slot_size))
	{
		return false;
	}	
	
	/* 先将目标槽标记为正在下载 */
	metadata.magic = BOOT_META_MAGIC;
	metadata.slot_id = (uint32_t)update_slot;
	metadata.version = version;
	metadata.firmware_size = firmware_size;
	metadata.firmware_crc32 = firmware_crc32;
	metadata.state = (uint32_t)SLOT_STATE_DOWNLOADING;
	metadata.boot_attempt = 0U;
	metadata.reserved = 0xFFFFFFFFU;	
	
	if (!BootMeta_Write(update_slot, &metadata))
	{
		return false;
	}
	
	for (address = slot_start;
		 address < (slot_start + slot_size);
		 address += FLASH_PAGE_BYTES)
	{
		if (!BootFlash_ErasePage(address))
		{
				return false;
		}
	}
	/* OTA 会话开始 */
	s_update_active = true;
	s_update_slot = update_slot;
	s_next_offset = 0U;
	s_firmware_size = firmware_size;
	return true;	
}

bool App_UpdateWrite(uint32_t offset,
                     const uint8_t *data,
                     uint32_t length)
{
	/*
	offset = 这包数据在固件里的偏移量
	data   = 收到的固件数据
	length = 本次数据长度	
	*/
	if (!s_update_active)
	{
		return false;
	}
	if (offset != s_next_offset)
	{
		return false;
	}	
	BootSlot_t update_slot;
	BootSlotMetadata_t metadata;

	uint32_t slot_start;
	uint32_t write_address;

	uint32_t page_data[
		FLASH_PAGE_BYTES / sizeof(uint32_t)
	];	

	if (data == NULL || 
		length == 0u || 
		length > FLASH_PAGE_BYTES)
	return false;

	update_slot = s_update_slot;
	if (update_slot == BOOT_SLOT_NONE)
	return false;

	if (!BootMeta_Read(update_slot, &metadata))
	return false;

	if (metadata.state != (uint32_t)SLOT_STATE_DOWNLOADING)
	return false;

	if ( offset % FLASH_PAGE_BYTES || 
		 offset >= metadata.firmware_size)
	return false;

	if (length >
		(metadata.firmware_size - offset))
	 return false;

	if (length != FLASH_PAGE_BYTES &&
	 (length + offset) != metadata.firmware_size)
	return false;

	slot_start = App_GetSlotStart(update_slot);

	write_address =
		slot_start + offset;
	/*
	* 9. 先把整页填成 0xFF
	*
	* 最后一页可能只有 128 Byte，
	* 剩余部分保持擦除状态 0xFF。
	*/
	memset(page_data,
			 0xFF,
			 sizeof(page_data));

	memcpy(page_data,
			 data,
			 length);

	if (!BootFlash_WritePage(write_address, page_data))
	{
		App_UpdateAbort();
		return false;
	}
		
	if (!BootFlash_VerifyPage(write_address, page_data))
	{
		App_UpdateAbort();
		return false;
	}
	
	s_next_offset += length;
	return true;
} 
	
bool App_UpdateFinish(void)
{
	if (!s_update_active)
	{
		return false;
	}
	if (s_next_offset != s_firmware_size)
	{
		return false;
	}		
	
	BootSlot_t update_slot;
	BootSlotMetadata_t metadata;

	uint32_t slot_start;

	/* 1. 找到 inactive Slot */
	update_slot = s_update_slot;

	if (update_slot == BOOT_SLOT_NONE)
	{
		return false;
	}

	/* 2. 读取目标 Slot Metadata */
	if (!BootMeta_Read(update_slot, &metadata))
	{
		return false;
	}

	/* 3. 必须仍处于 DOWNLOADING */
	if (metadata.state !=
		(uint32_t)SLOT_STATE_DOWNLOADING)
	{
		return false;
	}

	/* 4. 获取目标 Slot 起始地址 */
	slot_start = App_GetSlotStart(update_slot);

	if (slot_start == 0U)
	{
		return false;
	}

	/*  对刚写入的完整固件计算 CRC32 */
	/*  CRC 不一致，升级失败 */
	if (!Boot_CRC32_Verify(slot_start, 
												 metadata.firmware_size,
												 metadata.firmware_crc32))
	{
		return false;
	}

	/* 7. 固件完整，允许 Bootloader 试启动 */
	metadata.state =
		(uint32_t)SLOT_STATE_PENDING;

	metadata.boot_attempt = 0U;

	/* 8. 写回 Metadata */
	if (!BootMeta_Write(
				update_slot,
				&metadata))
	{
		return false;
	}
	//清除会话
	App_UpdateResetSession();
	return true;
}


