#ifndef __BOOT_PROTOCOL_H
#define	__BOOT_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>

/* Bootloader */
#define BOOT_START_ADDR        0x08000000U
#define BOOT_END_ADDR          0x08004000U

/* Slot A */
#define SLOT_A_START_ADDR      0x08004000U
#define SLOT_A_END_ADDR        0x08011800U
#define SLOT_A_SIZE            (SLOT_A_END_ADDR - SLOT_A_START_ADDR)

/* Slot B */
#define SLOT_B_START_ADDR      0x08011800U
#define SLOT_B_END_ADDR        0x0801F000U
#define SLOT_B_SIZE            (SLOT_B_END_ADDR - SLOT_B_START_ADDR)

/* Metadata */
#define BOOT_DATA_START_ADDR   0x0801F000U
#define BOOT_DATA_END_ADDR     0x08020000U

typedef enum{
	/*
		IDLE        空闲
		RECEIVING   正在接收
		VERIFY      校验
		SUCCESS     成功
		ERROR       错误	
	*/
	OTA_STATE_IDLE = 0,
	OTA_STATE_RECEIVING,
	OTA_STATE_VERIFY,
	OTA_STATE_SUCCESS,
	OTA_STATE_ERROR	
}OtaState_t;

typedef enum
{
	OTA_CMD_START = 0x01,
	OTA_CMD_DATA  = 0x02,
	OTA_CMD_END   = 0x03,

	OTA_CMD_ACK   = 0x80,
	OTA_CMD_NACK  = 0x81

}OtaCmd_t;

#endif
