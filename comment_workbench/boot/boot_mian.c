#include "py32f0xx.h"
#include "py32f0xx_hal.h"

#include "boot_metadata.h"
#include "boot_jump.h"
#include "boot_select.h"
#include "boot_crc.h"

int main(void)
{
	BootSlotMetadata_t meta_a = {0};
	BootSlotMetadata_t meta_b = {0};

	
	
	bool meta_a_valid;
	bool meta_b_valid;

	bool slot_a_valid;
	bool slot_b_valid;

	bool slot_a_crc_valid = false;
	bool slot_b_crc_valid = false;

	bool slot_a_bootable;
	bool slot_b_bootable;

	BootSlot_t boot_slot;

	HAL_Init();

	/* 1. 读取 A/B Metadata */
	meta_a_valid =
		BootMeta_Read(BOOT_SLOT_A, &meta_a);

	meta_b_valid =
		BootMeta_Read(BOOT_SLOT_B, &meta_b);


	/* 2. 检查 PENDING 启动次数 */
	if (meta_a_valid)
	{
		if (!BootMeta_CheckBootAttempts(
						BOOT_SLOT_A,
						&meta_a))
		{
				meta_a_valid = false;
		}
	}

	if (meta_b_valid)
	{
		if (!BootMeta_CheckBootAttempts(
						BOOT_SLOT_B,
						&meta_b))
		{
				meta_b_valid = false;
		}
	}


	/* 3. 检查 APP 向量表 */
	slot_a_valid =
		Boot_IsSlotValid(
				SLOT_A_START_ADDR,
				SLOT_A_END_ADDR);

	slot_b_valid =
		Boot_IsSlotValid(
				SLOT_B_START_ADDR,
				SLOT_B_END_ADDR);


	/* 4. CRC32 校验 */
	if (meta_a_valid &&
		slot_a_valid &&
		Boot_IsStateBootable(meta_a.state))
	{
		slot_a_crc_valid =
				Boot_CRC32_Verify(
						SLOT_A_START_ADDR,
						meta_a.firmware_size,
						meta_a.firmware_crc32);
	}

	if (meta_b_valid &&
		slot_b_valid &&
		Boot_IsStateBootable(meta_b.state))
	{
		slot_b_crc_valid =
				Boot_CRC32_Verify(
						SLOT_B_START_ADDR,
						meta_b.firmware_size,
						meta_b.firmware_crc32);
	}


	/* 5. 综合判断是否允许启动 */
	slot_a_bootable =
		meta_a_valid &&
		slot_a_valid &&
		slot_a_crc_valid &&
		Boot_IsStateBootable(meta_a.state);

	slot_b_bootable =
		meta_b_valid &&
		slot_b_valid &&
		slot_b_crc_valid &&
		Boot_IsStateBootable(meta_b.state);


	/* 6. 选择版本最高的可启动固件 */
	boot_slot =
		Boot_SelectSlot(
				slot_a_bootable,
				meta_a.version,
				slot_b_bootable,
				meta_b.version);


	/* 7. 记录试启动次数并跳转 */
	if (boot_slot == BOOT_SLOT_A)
	{
		if (!BootMeta_RecordBootAttempt(
						BOOT_SLOT_A,
						&meta_a))
		{
				while (1)
				{
				}
		}

		Boot_JumpToSlot(SLOT_A_START_ADDR);
	}
	else if (boot_slot == BOOT_SLOT_B)
	{
		if (!BootMeta_RecordBootAttempt(
						BOOT_SLOT_B,
						&meta_b))
		{
				while (1)
				{
				}
		}

		Boot_JumpToSlot(SLOT_B_START_ADDR);
	}

	/* 没有任何可启动固件 */
	while (1)
	{
	}
}
