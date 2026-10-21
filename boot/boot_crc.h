#ifndef __BOOT_CRC_H
#define __BOOT_CRC_H

#include <stdint.h>
#include <stdbool.h>

bool Boot_CRC32_Verify(uint32_t address,
                       uint32_t size,
                       uint32_t expected_crc);
uint32_t Boot_CRC32_Calculate(uint32_t address,
                              uint32_t size);
#endif
