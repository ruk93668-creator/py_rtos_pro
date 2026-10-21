#include "boot_crc.h"

uint32_t Boot_CRC32_Calculate(uint32_t address,
                              uint32_t size)
{
	uint32_t crc, i, bit;
	
	volatile const uint8_t *data;
	
	crc = 0xFFFFFFFFU;
	
	data = (volatile const uint8_t*)address;
	
	for( i = 0u;i < size;i++)
	{
		crc ^= data[i];
		for(bit = 0u;bit < 8u;bit++)
		{
			if ( (crc & 1u) != 0u)
			{
				crc = (crc >> 1u) ^ 0xEDB88320U;
			}else{
				crc >>= 1u;
			}
		}
	}
	return crc ^ 0xFFFFFFFFU;
}

bool Boot_CRC32_Verify(uint32_t address,
                       uint32_t size,
                       uint32_t expected_crc) 
{
	uint32_t actual_crc;
	
	actual_crc = Boot_CRC32_Calculate(
							 address, size);
	return (actual_crc == expected_crc);
}
