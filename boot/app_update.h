#ifndef __APP_UPDATE_H
#define __APP_UPDATE_H

#include <stdint.h>
#include <stdbool.h>

bool App_UpdateBegin(uint32_t version,
                     uint32_t firmware_size,
                     uint32_t firmware_crc32);

bool App_UpdateWrite(uint32_t offset,
                     const uint8_t *data,
                     uint32_t length);
										 
bool App_UpdateFinish(void);
										 
bool App_UpdateAbort(void);
#endif
