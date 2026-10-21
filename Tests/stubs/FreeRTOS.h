#ifndef TEST_FREERTOS_H
#define TEST_FREERTOS_H
#include <stdint.h>
void Test_EnterCritical(void);
void Test_ExitCritical(void);
#define taskENTER_CRITICAL() Test_EnterCritical()
#define taskEXIT_CRITICAL() Test_ExitCritical()
#endif
