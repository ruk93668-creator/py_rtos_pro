#include "ota_uart.h"

#include "ota_receiver.h"
#include "ota_protocol.h"
#include "bsp_usart.h"

#define OTA_INFO_FRAME_SIZE \
    (OTA_FRAME_HEADER_SIZE + \
     OTA_INFO_PAYLOAD_SIZE + \
     OTA_FRAME_CRC_SIZE)

static uint8_t s_ota_tx_frame[OTA_INFO_FRAME_SIZE];
		 
static UartId_t s_ota_uart_id = uart_typec;

static void OtaUart_WriteU32LE(
    uint8_t *data,
    uint32_t value)
{
    data[0] = (uint8_t)(value & 0xFFU);
    data[1] = (uint8_t)((value >> 8U) & 0xFFU);
    data[2] = (uint8_t)((value >> 16U) & 0xFFU);
    data[3] = (uint8_t)((value >> 24U) & 0xFFU);
}

static bool OtaUart_SendResponse(uint8_t type,
                                 uint16_t seq)
{
	uint8_t frame[OTA_FRAME_MIN_SIZE];
	uint16_t crc;	
	
	frame[0] = OTA_FRAME_SOF_0;
	frame[1] = OTA_FRAME_SOF_1;
	
	frame[2] = type;
	
	frame[3] = (uint8_t)(seq & 0xffu);
	frame[4] = (uint8_t)((seq >> 8u) & 0xffu);
	
	frame[5] = 0u;
	frame[6] = 0u;
	
	crc = Ota_CRC16_Calculate(
        &frame[2],
        5U);
	
	frame[7] = (uint8_t)(crc & 0xFFU);
  frame[8] = (uint8_t)((crc >> 8U) & 0xFFU);
	
	if (BSP_UART_Transmit_IT(
            s_ota_uart_id,
            frame,
            sizeof(frame)) != HAL_OK)
		return false;
						
    return true;
}



bool OtaUart_SetPort(OtaUartPort_t port)
{
    UartId_t new_id;

    switch (port)
    {
    case OTA_UART_TYPEC:
        new_id = uart_typec;
        break;

    case OTA_UART_BLE:
        new_id = uart_ble;
        break;

    default:
        return false;
    }

    if (new_id == s_ota_uart_id)
    {
        return true;
    }

    s_ota_uart_id = new_id;

    /*
     * 丢弃这个通道之前残留的数据。
     */
    BSP_UART_FlushRx(
        s_ota_uart_id);

    /*
     * 丢弃之前接了一半的OTA帧。
     */
    OtaReceiver_Init();

    return true;
}

OtaUartPort_t OtaUart_GetPort(void)
{
    if (s_ota_uart_id == uart_ble)
    {
        return OTA_UART_BLE;
    }

    return OTA_UART_TYPEC;
}

bool OtaUart_Init(void)
{
	OtaReceiver_Init();

	return true;
}

bool OtaUart_SendAck(uint16_t seq)
{
	return OtaUart_SendResponse(
			OTA_RSP_ACK,
			seq);
}

bool OtaUart_SendNack(uint16_t seq)
{
	return OtaUart_SendResponse(
			OTA_RSP_NACK,
			seq);
}

bool OtaUart_SendInfo(uint16_t seq)
{
    OtaDeviceInfo_t info;
    uint8_t *payload;
    uint16_t crc;
	
	if ( BSP_UART_GetTxStatus(s_ota_uart_id) == HAL_BUSY)
		return false;
	
	if (!Ota_GetDeviceInfo(&info))
		return false;
	
	s_ota_tx_frame[0] = OTA_FRAME_SOF_0;
	s_ota_tx_frame[1] = OTA_FRAME_SOF_1;

	s_ota_tx_frame[2] = OTA_RSP_INFO;

	s_ota_tx_frame[3] =
			(uint8_t)(seq & 0xFFU);

	s_ota_tx_frame[4] =
			(uint8_t)((seq >> 8U) & 0xFFU);

	s_ota_tx_frame[5] =
			(uint8_t)(OTA_INFO_PAYLOAD_SIZE & 0xFFU);

	s_ota_tx_frame[6] =
			(uint8_t)((OTA_INFO_PAYLOAD_SIZE >> 8U) & 0xFFU);	
	
	payload =
			&s_ota_tx_frame[OTA_FRAME_HEADER_SIZE];
			
	OtaUart_WriteU32LE(
			&payload[0],
			info.current_slot);

	OtaUart_WriteU32LE(
			&payload[4],
			info.slot_a_state);

	OtaUart_WriteU32LE(
			&payload[8],
			info.slot_a_version);

	OtaUart_WriteU32LE(
			&payload[12],
			info.slot_a_size);

	OtaUart_WriteU32LE(
			&payload[16],
			info.slot_a_crc32);

	OtaUart_WriteU32LE(
			&payload[20],
			info.slot_a_boot_attempt);


	OtaUart_WriteU32LE(
			&payload[24],
			info.slot_b_state);

	OtaUart_WriteU32LE(
			&payload[28],
			info.slot_b_version);

	OtaUart_WriteU32LE(
			&payload[32],
			info.slot_b_size);

	OtaUart_WriteU32LE(
			&payload[36],
			info.slot_b_crc32);

	OtaUart_WriteU32LE(
			&payload[40],
			info.slot_b_boot_attempt);	
			
	crc =
			Ota_CRC16_Calculate(
					&s_ota_tx_frame[2],
					(uint16_t)(
							5U +
							OTA_INFO_PAYLOAD_SIZE));	
	
	s_ota_tx_frame[
        OTA_FRAME_HEADER_SIZE +
        OTA_INFO_PAYLOAD_SIZE] =
            (uint8_t)(crc & 0xFFU);

	s_ota_tx_frame[
			OTA_FRAME_HEADER_SIZE +
			OTA_INFO_PAYLOAD_SIZE +
			1U] =
					(uint8_t)((crc >> 8U) & 0xFFU);
					
	if (BSP_UART_Transmit_IT(
            s_ota_uart_id,
            s_ota_tx_frame,
            OTA_INFO_FRAME_SIZE) != HAL_OK)
		return false;
	
	return true;
}

void OtaUart_Process(void)
{
	uint8_t byte;
	uint16_t seq;
	OtaRxResult_t result;
	OtaResponseType_t response;

	while (BSP_UART_ReadByte(
					 s_ota_uart_id,
					 &byte))
	{
		seq = 0xFFFFU;
		
		
	  result = 
				OtaReceiver_InputByte(byte, 
															&seq, 
															&response);

		if (result == OTA_RX_RESULT_OK)
		{
				if (response == OTA_RESPONSE_ACK)
					OtaUart_SendAck(seq);
				else if (response == OTA_RESPONSE_INFO)
					OtaUart_SendInfo(seq);
		}
		else if (result == OTA_RX_RESULT_ERROR)
		{
				if (seq != 0xFFFFU)
				{
						OtaUart_SendNack(seq);
				}
		}		
	}
}


