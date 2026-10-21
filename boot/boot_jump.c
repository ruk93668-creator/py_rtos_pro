#include "boot_jump.h"
#include "py32f0xx.h"

typedef void (*AppEntry_t)(void);
//检查Flash槽位
bool Boot_IsSlotValid(uint32_t slot_start, uint32_t slot_end)
{
	uint32_t app_msp;
	uint32_t app_reset;

	app_msp = *(volatile uint32_t *)slot_start;
	app_reset = *(volatile uint32_t *)(slot_start + 4U);

	if ( (app_msp >= SRAM_START_ADDR 				 && 
				app_msp <= SRAM_END_ADDR) 				 &&
			((app_reset & ~1U) >= slot_start && 
			 (app_reset & ~1U) < slot_end) &&
			((app_reset & 1u) == 1))//确认用Thumb指令状态
		return true;
	return false;
}

void Boot_JumpToSlot(uint32_t slot_start)
{
	uint32_t app_msp;
	uint32_t app_reset;	
	AppEntry_t app_entry;
	
	app_msp = *(volatile uint32_t*)slot_start;
	app_reset = *(volatile uint32_t*)(slot_start + 4U);
	
	app_entry = (AppEntry_t)app_reset;
	
	__disable_irq();

	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL  = 0U;
	
  SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk |
              SCB_ICSR_PENDSVCLR_Msk;
	
	SCB->VTOR = slot_start;/* 中断向量表切换到 APP */
	__DSB();
	__ISB();//确保切换成功
	
	__set_MSP(app_msp);/* APP 使用自己的初始主栈 */
	
	__enable_irq();
	app_entry();/* 进入 APP Reset_Handler */
	while (1)
	{
	}
}

