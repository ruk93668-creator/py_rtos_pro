#include "boot_flash.h"
#include "boot_config.h"
#include "py32f0xx.h"
#include "py32f071_hal_flash.h"

bool BootFlash_ErasePage(uint32_t address)
{
	FLASH_EraseInitTypeDef erase = {0};
	uint32_t page_error = 0xFFFFFFFFU;
	HAL_StatusTypeDef status;
	/* 1. 检查 address 是否位于我们的安全数据区 */
	if ( (address < SLOT_A_START_ADDR) ||
       (address > (FLASH_END_ADDR - FLASH_PAGE_SIZE)))
		return false;
	/* 2. 检查 address 是否按照 256 Byte Page 对齐 */
	if ( address %FLASH_PAGE_SIZE != 0)
		return false;
	
	if (HAL_FLASH_Unlock() != HAL_OK)
	{
			return false;
	}
	
	erase.TypeErase = FLASH_TYPEERASE_PAGEERASE;
	erase.PageAddress = address;
	erase.NbPages = 1u;
	
	status = HAL_FLASH_Erase(&erase, &page_error);
	
	HAL_FLASH_Lock();

	if ((status != HAL_OK) ||
			(page_error != 0xFFFFFFFFU))
	{
			return false;
	}
	return true;	
}
//写入数据一次	256 字节
bool BootFlash_WritePage(uint32_t address, uint32_t *data)
{
	if ((((address < SLOT_A_START_ADDR) ||
					(address > (FLASH_END_ADDR - FLASH_PAGE_SIZE)))	||
			   data == NULL))
		return false;	
	if ( address % FLASH_PAGE_SIZE)
		return false;
	
	if (HAL_FLASH_Unlock() != HAL_OK)
	{
			return false;
	}
	
	HAL_StatusTypeDef status = HAL_FLASH_PageProgram(address, data);
	
	HAL_FLASH_Lock();
	
	return (status == HAL_OK);
}
//校验写入数据是否有错
bool BootFlash_VerifyPage(uint32_t address,
                          const uint32_t *data)
{
  volatile const uint32_t *flash_data;
	if ((((address < SLOT_A_START_ADDR) ||
					(address > (FLASH_END_ADDR - FLASH_PAGE_SIZE)))	||
			   data == NULL))
		return false;	
	if ( address % FLASH_PAGE_SIZE)
		return false;	
	
	flash_data = (volatile uint32_t *)address;
	
	for ( uint32_t i = 0; i < (FLASH_PAGE_SIZE / sizeof(uint32_t));i++)
		if ( *(flash_data + i) != *(data + i))
			return false;
	return true;
}
