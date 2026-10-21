#ifndef __OTA_RECEIVER_H
#define __OTA_RECEIVER_H

#include "ota_protocol.h"

#include <stdint.h>
#include <stddef.h>

typedef enum
{
    OTA_RX_RESULT_NONE = 0,
    OTA_RX_RESULT_OK,
    OTA_RX_RESULT_ERROR

} OtaRxResult_t;

void OtaReceiver_Init(void);

OtaRxResult_t OtaReceiver_InputByte(
    uint8_t byte,
    uint16_t *seq_out,
    OtaResponseType_t *response_out);

void OtaReceiver_Input(const uint8_t *data,
                       uint16_t length);

#endif
