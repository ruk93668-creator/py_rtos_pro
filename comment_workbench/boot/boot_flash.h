#ifndef __BOOT_FLASH_H
#define	__BOOT_FLASH_H

#include <stdint.h>
#include <stdbool.h>


bool BootFlash_ErasePage(uint32_t address);
bool BootFlash_WritePage(uint32_t address, uint32_t *data);
bool BootFlash_VerifyPage(uint32_t address,
                          const uint32_t *data);
#endif
