#ifndef __OTA_PROTOCOL_H
#define __OTA_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

#define OTA_FRAME_SOF_0          0xAAU
#define OTA_FRAME_SOF_1          0x55U

#define OTA_DATA_MAX_SIZE        256U

#define OTA_BEGIN_PAYLOAD_SIZE   12U
#define OTA_DATA_OFFSET_SIZE     4U

#define OTA_INFO_PAYLOAD_SIZE    44U

#define OTA_FRAME_HEADER_SIZE       7U
#define OTA_FRAME_CRC_SIZE          2U
#define OTA_FRAME_MIN_SIZE          9U
#define OTA_FRAME_MAX_PAYLOAD_SIZE  \
        (OTA_DATA_OFFSET_SIZE + OTA_DATA_MAX_SIZE)
				
#define OTA_FRAME_MAX_SIZE       \
    (OTA_FRAME_HEADER_SIZE +     \
     OTA_FRAME_MAX_PAYLOAD_SIZE + \
     OTA_FRAME_CRC_SIZE)
/*
				OTA_FRAME_HEADER_SIZE    					OTA_FRAME_CRC_SIZE
©°©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ð©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©´
©¦ SOF     ©¦ TYPE ©¦ SEQ ©¦ LENGTH ©¦ PAYLOAD ©¦ PACKET CRC  ©¦
©¦ 2 Byte  ©¦ 1B   ©¦ 2B  ©¦ 2B     ©¦ N Byte  ©¦ 2B          ©¦
©¸©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¤©¤©¤©¤©Ø©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¼
																			|
															©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤©¤
											OTA_FRAME_MAX_PAYLOAD_SIZE
*/
typedef enum
{
    OTA_CMD_BEGIN 			= 0x01U,
    OTA_CMD_DATA  			= 0x02U,
    OTA_CMD_END   			= 0x03U,
    OTA_CMD_ABORT 			= 0x04U,
		OTA_CMD_QUERY_INFO  = 0x05U,
	
	  OTA_RSP_ACK   			= 0x80U,
    OTA_RSP_NACK  			= 0x81U,
		OTA_RSP_INFO  			= 0x82U
} OtaCommand_t;

typedef enum
{
    OTA_RESPONSE_ACK = 0,
    OTA_RESPONSE_INFO

} OtaResponseType_t;

typedef struct
{
    uint32_t current_slot;

    uint32_t slot_a_state;
    uint32_t slot_a_version;
    uint32_t slot_a_size;
    uint32_t slot_a_crc32;
    uint32_t slot_a_boot_attempt;

    uint32_t slot_b_state;
    uint32_t slot_b_version;
    uint32_t slot_b_size;
    uint32_t slot_b_crc32;
    uint32_t slot_b_boot_attempt;

} OtaDeviceInfo_t;

bool Ota_GetDeviceInfo(OtaDeviceInfo_t *info);

uint16_t Ota_CRC16_Calculate(const uint8_t *data,
                                    uint16_t length);

//bool Ota_HandleBegin(const uint8_t *payload,
//                     uint16_t length);

//bool Ota_HandleData(const uint8_t *payload,
//                    uint16_t length);

//bool Ota_HandleEnd(const uint8_t *payload,
//                   uint16_t length);

//bool Ota_HandleAbort(const uint8_t *payload,
//                     uint16_t length);

bool Ota_HandleFrame(uint8_t type,
                     const uint8_t *payload,
                     uint16_t length);
										 
bool Ota_ParseFrame(
    const uint8_t *frame,
    uint16_t frame_length,
    uint16_t *seq_out,
    OtaResponseType_t *response_out);
#endif
