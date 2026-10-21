#include "ota_protocol.h"

#include "app_update.h"
#include "boot_config.h"
#include "boot_metadata.h"

#include <string.h>
static bool s_seq_active = false;
static uint16_t s_expected_seq = 0U;

static void Ota_ResetSequence(void)
{
    s_seq_active = false;
    s_expected_seq = 0U;
}

static uint16_t Ota_ReadU16LE(const uint8_t *data)
{
    return ((uint16_t)data[0]) |
           ((uint16_t)data[1] << 8U);
}

static uint32_t Ota_ReadU32LE(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static bool Ota_HandleBegin(const uint8_t *payload,
                     uint16_t length)
{
	uint32_t version;
	uint32_t firmware_size;
	uint32_t firmware_crc32;
	
	if (payload == NULL)
		return false;
	if (length != OTA_BEGIN_PAYLOAD_SIZE)
		return false;
	
	version =
			Ota_ReadU32LE(&payload[0]);

	firmware_size =
			Ota_ReadU32LE(&payload[4]);

	firmware_crc32 =
			Ota_ReadU32LE(&payload[8]);	
	
	return App_UpdateBegin(
        version,
        firmware_size,
        firmware_crc32);
}

static bool Ota_HandleData(const uint8_t *payload,
                    uint16_t length)
{
	uint32_t offset;
  uint16_t data_length;
	
	if (payload == NULL)
		return false;
	if (length <= OTA_DATA_OFFSET_SIZE)
		return false;	
	
	if (length >
     (OTA_DATA_OFFSET_SIZE + OTA_DATA_MAX_SIZE))
        return false;
	offset =
        Ota_ReadU32LE(&payload[0]);
	data_length =
        length - OTA_DATA_OFFSET_SIZE;
	
	return App_UpdateWrite(
        offset,
        &payload[OTA_DATA_OFFSET_SIZE],
        data_length);
}

static bool Ota_HandleEnd(const uint8_t *payload,
                   uint16_t length)
{
	(void)payload;
	
	if (length != 0U)
	{
		return false;
	}

	return App_UpdateFinish();
}

static bool Ota_HandleAbort(const uint8_t *payload,
								 uint16_t length)
{
	(void)payload;
	
	if (length != 0U)
	{
		return false;
	}

	return App_UpdateAbort();
}

bool Ota_GetDeviceInfo(OtaDeviceInfo_t *info)
{
	BootSlotMetadata_t meta_a;
	BootSlotMetadata_t meta_b;
	
	if ( info == NULL)
		return false;
	
	memset(info, 0, sizeof(OtaDeviceInfo_t));
	
	info->current_slot =
        (uint32_t)App_GetCurrentSlot();
	
	if (BootMeta_Read(BOOT_SLOT_A, &meta_a))
	{
		info->slot_a_state =
				meta_a.state;

		info->slot_a_version =
				meta_a.version;

		info->slot_a_size =
				meta_a.firmware_size;

		info->slot_a_crc32 =
				meta_a.firmware_crc32;

		info->slot_a_boot_attempt =
				meta_a.boot_attempt;
	}
	else
	{
		info->slot_a_state =
				SLOT_STATE_EMPTY;
	}
	
	if (BootMeta_Read(BOOT_SLOT_B, &meta_b))
	{
		info->slot_b_state =
				meta_b.state;

		info->slot_b_version =
				meta_b.version;

		info->slot_b_size =
				meta_b.firmware_size;

		info->slot_b_crc32 =
				meta_b.firmware_crc32;

		info->slot_b_boot_attempt =
				meta_b.boot_attempt;
	}
	else
	{
		info->slot_b_state =
				SLOT_STATE_EMPTY;
	}

	return true;
}

bool Ota_HandleFrame(uint8_t type,
                     const uint8_t *payload,
                     uint16_t length)
{
	switch (type)
	{
	case OTA_CMD_BEGIN:
		return Ota_HandleBegin(
				payload,
				length);

	case OTA_CMD_DATA:
		return Ota_HandleData(
				payload,
				length);

	case OTA_CMD_END:
		return Ota_HandleEnd(
				payload,
				length);

	case OTA_CMD_ABORT:
		return Ota_HandleAbort(
				payload,
				length);
	default:
		return false;
	}
}


//Frame
/*检查“一帧 UART 数据”有没有传坏*/
uint16_t Ota_CRC16_Calculate(const uint8_t *data,
                                    uint16_t length)
{
	uint16_t crc = 0xFFFFU;
	uint16_t i;
	uint8_t bit;

	for (i = 0U; i < length; i++)
	{
		crc ^= ((uint16_t)data[i] << 8U);

		for (bit = 0U; bit < 8U; bit++)
		{
			if ((crc & 0x8000U) != 0U)
			{
				crc =
						(uint16_t)((crc << 1U) ^ 0x1021U);
			}
			else
			{
				crc <<= 1U;
			}
		}
	}

	return crc;
}

bool Ota_ParseFrame(
    const uint8_t *frame,
    uint16_t frame_length,
    uint16_t *seq_out,
    OtaResponseType_t *response_out)
{
	uint8_t type;

	uint16_t seq;
	uint16_t payload_length;

	uint16_t received_crc;
	uint16_t calculated_crc;

	uint32_t expected_length;

	const uint8_t *payload;
	
	if (response_out != NULL)
		*response_out = OTA_RESPONSE_ACK;
	if ( seq_out != NULL)
		*seq_out = 0xffffu;
	
	
	if (frame == NULL)
	{
			return false;
	}

	if (frame_length < OTA_FRAME_MIN_SIZE)
	{
			return false;
	}
	
	if ((frame[0] != OTA_FRAME_SOF_0) ||
      (frame[1] != OTA_FRAME_SOF_1))
		return false;

	/* 3. 解析固定字段 */
	type = frame[2];

	seq =
			Ota_ReadU16LE(&frame[3]);
	if (seq_out != NULL)
		*seq_out = seq;


	payload_length =
			Ota_ReadU16LE(&frame[5]);
	
	if (payload_length >
			OTA_FRAME_MAX_PAYLOAD_SIZE)
		return false;
	
	expected_length = OTA_FRAME_HEADER_SIZE +
										payload_length				+
										OTA_FRAME_CRC_SIZE;
	if (frame_length != expected_length)
		return false;
	
	
	
	received_crc =
        Ota_ReadU16LE(
            &frame[
                OTA_FRAME_HEADER_SIZE +
                payload_length]);
		/*
     * 8. 计算 CRC16
     *
     * 不包含：
     * AA 55
     * CRC16本身
     *
     * 包含：
     * TYPE + SEQ + LENGTH + PAYLOAD
     */
	calculated_crc =
        Ota_CRC16_Calculate(
            &frame[2],
            (uint16_t)(
                5U + payload_length));
	
	if (received_crc != calculated_crc)
		return false;
		
	payload = &frame[OTA_FRAME_HEADER_SIZE];
	
	if (type == OTA_CMD_QUERY_INFO)
	{
		if (seq != 0U)
			return false;

		if (payload_length != 0U)
			return false;

		if (response_out != NULL)
			*response_out = OTA_RESPONSE_INFO;

		return true;
	}
	
	if (type == OTA_CMD_BEGIN)
	{
		if (seq != 0u)
			return false;
		
		if (!Ota_HandleFrame(
							type,
							payload,
							payload_length))
			return false;	
		
    s_seq_active = true;
    s_expected_seq = 1U;

    return true;
	}
	
	if (!s_seq_active)
		return false;
	
	if ( seq != s_expected_seq)
		return false;
	
	if (!Ota_HandleFrame(
        type,
        payload,
        payload_length))
		return false;

	if ((type == OTA_CMD_END) ||
			(type == OTA_CMD_ABORT))
	{
		Ota_ResetSequence();
	}
	else
	{
		/*
		* DATA 成功以后等待下一帧
		*/
		s_expected_seq++;
	}	
	
	return true;
}
