#ifndef SERVICE_ENMU_H
#define SERVICE_ENMU_H

#include <stdint.h>
#include "game_id.h"

typedef enum {
    INPUT_UP = 0, INPUT_DOWN, INPUT_LEFT, INPUT_RIGHT,
    INPUT_A, INPUT_B, INPUT_PAUSE, INPUT_BACK, INPUT_COUNT,
    INPUT_NONE = 0xFF
} InputCode_t;

/* Keep the origin of an event so games can distinguish a rotary detent from
   a held motion direction while sharing one frame queue. */
typedef enum {
    INPUT_SOURCE_BUTTON = 0,
    INPUT_SOURCE_ROTARY,
    INPUT_SOURCE_MOTION
} InputSource_t;

typedef struct {
    InputCode_t code;
    uint8_t pressed;
    InputSource_t source;
} InputEvent_t;

/* Init before starting the scheduler; Process in Service task only. */
void InputService_Init(void);
void InputService_Process(void);
/* App task takes a consistent snapshot once per application frame. */
void InputService_BeginFrame(void);
void InputService_ClearFrame(void);
uint8_t InputService_ConsumePress(InputCode_t code);
uint32_t InputService_GetDroppedCount(void);
uint8_t InputService_GetEvent(InputEvent_t *event);
uint8_t InputService_IsDown(InputCode_t code);
uint8_t InputService_IsRotaryDown(InputCode_t code);
uint8_t InputService_ConsumeBack(void);

/* Menu state is owned exclusively by the App task. */
void MenuService_Init(void);
void MenuService_HandleInput(void);
void MenuService_Render(void);
uint8_t MenuService_ConsumeStart(GameId_t *game_id);
/* Returns one when the independent Settings item was activated. */
uint8_t MenuService_ConsumeSettings(void);

#ifdef ARCADE_TEST
typedef struct {
    GameId_t selected;
    GameId_t visible;
    int32_t offset;
    int32_t velocity;
    uint8_t start_pending;
} MenuDebug;
void MenuService_Debug(MenuDebug *debug);
#endif

#endif
