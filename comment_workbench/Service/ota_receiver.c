#include "ota_receiver.h"
#include "ota_protocol.h"

#include <stdbool.h>

typedef enum
{
	OTA_RX_WAIT_SOF0 = 0,
	OTA_RX_WAIT_SOF1,
	OTA_RX_RECEIVING

} OtaRxState_t;

static OtaRxState_t s_rx_state;

static uint8_t s_frame_buffer[OTA_FRAME_MAX_SIZE];

static uint16_t s_frame_length;

static uint16_t s_expected_length;
/*
		s_rx_state
		→ 当前收到帧的哪个阶段

		s_frame_buffer
		→ 暂存一整帧

		s_frame_length
		→ 当前已经收了多少 Byte

		s_expected_length
		→ 根据 LENGTH 算出来，这一整帧应该有多长
*/

static uint16_t Ota_ReadU16LE(const uint8_t *data)
{
    return ((uint16_t)data[0]) |
           ((uint16_t)data[1] << 8U);
}

void OtaReceiver_Init(void)
{
	s_rx_state = OTA_RX_WAIT_SOF0;
	
	s_frame_length = 0u;
	s_expected_length = 0u;
}

OtaRxResult_t OtaReceiver_InputByte(
    uint8_t byte,
    uint16_t *seq_out,
    OtaResponseType_t *response_out)
{
	uint16_t payload_length;
	uint16_t seq = 0xFFFFU;
	
	OtaResponseType_t response =
        OTA_RESPONSE_ACK;
	
	bool parse_ok;

	if (seq_out != NULL)
		*seq_out = 0xFFFFU;
	
	if (response_out != NULL)
		*response_out = OTA_RESPONSE_ACK;

	
	switch((uint8_t)s_rx_state)
	{
		case OTA_RX_WAIT_SOF0:
		{
			if (byte == OTA_FRAME_SOF_0)
			{
				s_frame_buffer[0] = byte;
				s_frame_length = 1u;
				s_rx_state = OTA_RX_WAIT_SOF1;
			}
			
			return OTA_RX_RESULT_NONE;
		}
		case OTA_RX_WAIT_SOF1:
		{
			if (byte == OTA_FRAME_SOF_1)
			{
				s_frame_buffer[1] = byte;
				s_frame_length = 2u;
				
				s_rx_state = OTA_RX_RECEIVING;
			}else if ( byte == OTA_FRAME_SOF_0)
			{
				s_frame_buffer[0] = byte;
				s_frame_length = 1U;				
			}else{
				s_frame_length = 0U;
				s_rx_state = OTA_RX_WAIT_SOF0;				
			}
			
			return OTA_RX_RESULT_NONE;
		}
		case OTA_RX_RECEIVING:
		{
			if ( s_frame_length >= OTA_FRAME_MAX_SIZE)
			{
				OtaReceiver_Init();
				return OTA_RX_RESULT_NONE;
			}
			
			s_frame_buffer[s_frame_length] = byte;
			s_frame_length++;
			
			if ( s_frame_length == OTA_FRAME_HEADER_SIZE)
			{
				payload_length = 
						Ota_ReadU16LE(
							&s_frame_buffer[5]);
				
				if ( payload_length > 
						 OTA_FRAME_MAX_PAYLOAD_SIZE)
				{
					OtaReceiver_Init();
					return OTA_RX_RESULT_ERROR;
				}
				
				s_expected_length = 
				OTA_FRAME_HEADER_SIZE +
				payload_length				+
				OTA_FRAME_CRC_SIZE;
				if (s_expected_length >
						OTA_FRAME_MAX_SIZE)
				{
					OtaReceiver_Init();
					return OTA_RX_RESULT_ERROR;
				}
			}
			
			if ((s_expected_length != 0U) &&
					(s_expected_length == s_frame_length))
			{
					parse_ok =
						Ota_ParseFrame(
								s_frame_buffer,
								s_frame_length,
								&seq,
								&response);

					if (seq_out != NULL)
					{
							*seq_out = seq;
					}
					
					if ( response_out != NULL)
						*response_out = response;
					
					OtaReceiver_Init();

					if (parse_ok)
					{
							return OTA_RX_RESULT_OK;
					}
					return OTA_RX_RESULT_ERROR;
			}
			return OTA_RX_RESULT_NONE;
		}
    default:
			OtaReceiver_Init();
			return OTA_RX_RESULT_ERROR;
	}
}

void OtaReceiver_Input(const uint8_t *data,
                       uint16_t length)
{
	uint16_t i;

	if (data == NULL)
	{
		return;
	}

	for (i = 0U; i < length; i++)
	{
		OtaReceiver_InputByte(data[i], NULL, NULL);
	}
}
