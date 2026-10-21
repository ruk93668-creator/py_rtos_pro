#include "app_settings.h"

#include "ota_uart.h"
#include "ble.h"
#include "boot_metadata.h"
#include "service_display.h"
#include "service_enmu.h"

static AppUartPort_t s_uart_port;
static uint8_t s_done;

void AppSettings_Init(void)
{
    /* Keep the existing Type-C OTA path as the power-on default. */
    s_uart_port = APP_UART_TYPEC;
	
		(void)OtaUart_SetPort(
        OTA_UART_TYPEC);
    s_done = 0U;
}

void AppSettings_SetUart(AppUartPort_t port)
{
    if (port == APP_UART_TYPEC)
    {
        if (OtaUart_SetPort(OTA_UART_TYPEC))
        {
						BLE_Sleep();
            s_uart_port = APP_UART_TYPEC;
        }
    }
    else if (port == APP_UART_BLE)
    {
				BLE_Wakeup();
        if (OtaUart_SetPort(OTA_UART_BLE))
        {
            s_uart_port = APP_UART_BLE;
        }
    }
}

AppUartPort_t AppSettings_GetUart(void)
{
    return s_uart_port;
}

uint8_t AppSettings_GetUartFlag(void)
{
    return (uint8_t)s_uart_port;
}

uint8_t AppSettings_GetCurrentVersion(uint32_t *version)
{
    BootSlotMetadata_t metadata;
    BootSlot_t slot;

    if (version == (uint32_t *)0)
        return 0U;

    slot = App_GetCurrentSlot();
    if (slot == BOOT_SLOT_NONE || !BootMeta_Read(slot, &metadata)) {
        *version = 0U;
        return 0U;
    }

    *version = metadata.version;
    return 1U;
}

void AppSettings_Process(void)
{
    InputEvent_t event;

    while (InputService_GetEvent(&event)) {
        if (!event.pressed)
            continue;

        if (event.code == INPUT_UP) {
            AppSettings_SetUart(APP_UART_TYPEC);
        } else if (event.code == INPUT_DOWN) {
            AppSettings_SetUart(APP_UART_BLE);
        } else if (event.code == INPUT_A ||
                   event.code == INPUT_B ||
                   event.code == INPUT_BACK) {
            s_done = 1U;
            break;
        }
    }
}

void AppSettings_Render(void)
{
    uint32_t version;
    const char *port_name = (s_uart_port == APP_UART_TYPEC) ?
                            "TYPEC" : "BLE";

    if (!DisplayService_BeginFrame())
        return;

    DisplayService_Text(0, 0, "SETTINGS-ciallo");
    DisplayService_Text(0, 8, "UART:");
    DisplayService_Text(36, 8, port_name);

    DisplayService_Text(0, 16, "FLAG:");
    DisplayService_Number(36, 16, (uint16_t)s_uart_port, 1U);
    DisplayService_Text(54, 16, "VER:");
    if (AppSettings_GetCurrentVersion(&version))
        DisplayService_Number(78, 16, (uint16_t)(version % 100000U), 5U);
    else
        DisplayService_Text(78, 16, "ERR");

    DisplayService_Text(0, 24, "A=OK B=BACK");
    DisplayService_Present();
}

uint8_t AppSettings_IsDone(void)
{
    return s_done;
}

void AppSettings_ClearDone(void)
{
    s_done = 0U;
}
