#include "service_enmu.h"
#include "service_mpu_6050.h"
#include "service_display.h"
#include "service_config.h"
#include "key.h"
#include "menu_icons.h"
#include "py32f0xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include <string.h>

#define INPUT_QUEUE_DEPTH 32U
static InputEvent_t s_queue[INPUT_QUEUE_DEPTH], s_frame[INPUT_QUEUE_DEPTH];
static uint8_t s_count, s_frame_count, s_frame_index;
static uint8_t s_live_state, s_frame_state, s_frame_presses;
static uint8_t s_raw, s_stable, s_long_sent, s_button_armed, s_rotary_state;
static uint32_t s_changed, s_pressed_at, s_rotary_until, s_dropped;

/* Called while the Service task holds its short input-state critical section. */
static void Push(InputCode_t code, uint8_t pressed, InputSource_t source)
{
    if (s_count == INPUT_QUEUE_DEPTH) {
        memmove(s_queue, s_queue + 1,
                (INPUT_QUEUE_DEPTH - 1U) * sizeof(s_queue[0]));
        --s_count;
        ++s_dropped;
    }
    s_queue[s_count].code = code;
    s_queue[s_count++].pressed = pressed;
    s_queue[s_count - 1U].source = source;
}

static void Pulse(InputCode_t code, InputSource_t source)
{
    Push(code, 1U, source);
    Push(code, 0U, source);
}

void InputService_Init(void)
{
    s_count = s_frame_count = s_frame_index = 0U;
    s_live_state = s_frame_state = s_frame_presses = 0U;
    s_raw = s_stable = KEY_IsPressed() ? 1U : 0U;
    s_button_armed = (uint8_t)!s_stable;
    s_long_sent = s_rotary_state = 0U;
    s_changed = s_pressed_at = s_rotary_until = HAL_GetTick();
    s_dropped = 0U;
}

void InputService_Process(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t raw = KEY_IsPressed() ? 1U : 0U;
    uint8_t state = MotionService_GetState();
    int8_t turns = KEY_TakeRotation();
    uint8_t changed;
    unsigned i;
    MotionCode_t motion;

    taskENTER_CRITICAL();
    if (raw != s_raw) { s_raw = raw; s_changed = now; }
    if (raw != s_stable && (uint32_t)(now - s_changed) >= SERVICE_BUTTON_DEBOUNCE_MS) {
        s_stable = raw;
        if (raw) { s_pressed_at = now; s_long_sent = 0U; }
        else {
            if (s_button_armed && !s_long_sent)
                Pulse(INPUT_A, INPUT_SOURCE_BUTTON);
            s_button_armed = 1U;
        }
    }
    if (s_stable && s_button_armed && !s_long_sent &&
        (uint32_t)(now - s_pressed_at) >= SERVICE_BUTTON_BACK_MS) {
        Pulse(INPUT_BACK, INPUT_SOURCE_BUTTON);
        s_long_sent = 1U;
    }
    if (turns != 0) {
        s_rotary_state = (uint8_t)(1U << (turns > 0 ? INPUT_DOWN : INPUT_UP));
        s_rotary_until = now + SERVICE_ROTARY_HOLD_MS;
        while (turns > 0) {
            Pulse(INPUT_DOWN, INPUT_SOURCE_ROTARY);
            --turns;
        }
        while (turns < 0) {
            Pulse(INPUT_UP, INPUT_SOURCE_ROTARY);
            ++turns;
        }
    } else if ((int32_t)(now - s_rotary_until) >= 0) s_rotary_state = 0U;

    state |= s_rotary_state;
    changed = state ^ s_live_state;
    for (i = 0U; i < 4U; ++i) {
        if ((changed & (1U << i)) && !(s_rotary_state & (1U << i)))
            Push((InputCode_t)i, (uint8_t)((state >> i) & 1U),
                 INPUT_SOURCE_MOTION);
    }
    s_live_state = state;
    while (MotionService_ConsumeEvent(&motion)) {
        if (motion == MOTION_JUMP) Pulse(INPUT_A, INPUT_SOURCE_MOTION);
        if (motion == MOTION_SHAKE) Pulse(INPUT_PAUSE, INPUT_SOURCE_MOTION);
    }
    taskEXIT_CRITICAL();
}

void InputService_BeginFrame(void)
{
    unsigned i;
    taskENTER_CRITICAL();
    memcpy(s_frame, s_queue, s_count * sizeof(s_queue[0]));
    s_frame_count = s_count;
    s_count = 0U;
    s_frame_index = 0U;
    s_frame_state = s_live_state;
    taskEXIT_CRITICAL();
    s_frame_presses = 0U;
    for (i = 0U; i < s_frame_count; ++i)
        if (s_frame[i].pressed) s_frame_presses |= (uint8_t)(1U << s_frame[i].code);
}

void InputService_ClearFrame(void)
{ s_frame_count = s_frame_index = s_frame_presses = 0U; }

uint8_t InputService_GetEvent(InputEvent_t *event)
{
    if (!event || s_frame_index == s_frame_count) return 0U;
    *event = s_frame[s_frame_index++];
    return 1U;
}

uint8_t InputService_IsDown(InputCode_t code)
{
    return (uint8_t)((unsigned)code < INPUT_COUNT &&
                    (s_frame_state & (1U << code)) != 0U);
}

uint8_t InputService_IsRotaryDown(InputCode_t code)
{
    uint8_t value;
    if ((unsigned)code >= INPUT_COUNT) return 0U;
    taskENTER_CRITICAL();
    value = (uint8_t)((s_rotary_state & (1U << code)) != 0U);
    taskEXIT_CRITICAL();
    return value;
}

uint8_t InputService_ConsumePress(InputCode_t code)
{
    uint8_t found;
    if ((unsigned)code >= INPUT_COUNT) return 0U;
    found = (uint8_t)((s_frame_presses & (1U << code)) != 0U);
    s_frame_presses &= (uint8_t)~(1U << code);
    return found;
}

uint8_t InputService_ConsumeBack(void)
{
    uint8_t back = InputService_ConsumePress(INPUT_BACK);
    return back | InputService_ConsumePress(INPUT_B);
}

uint32_t InputService_GetDroppedCount(void)
{
    uint32_t dropped;
    taskENTER_CRITICAL();
    dropped = s_dropped;
    taskEXIT_CRITICAL();
    return dropped;
}

static const char *const s_names[GAME_ID_COUNT] = {
    "DINO", "SNAKE", "PLANE", "RACING", "FLAPPY", "SHOOTER", "FISHING"
};
#define MENU_ITEM_SETTINGS ((uint8_t)GAME_ID_COUNT)
#define MENU_ITEM_COUNT    ((uint8_t)(GAME_ID_COUNT + 1U))

static uint8_t s_selected_item;
static uint8_t s_start_pending;
static uint8_t s_settings_pending;
static uint32_t s_move_ready_ms, s_start_ready_ms, s_animation_ms;
static int32_t s_offset, s_velocity;
static uint8_t s_remainder_ms;

#define MENU_MOVE_STEP_MS 10U
#define MENU_SPACING_Q8   (64 * 256)
#define MENU_MAX_OFFSET_Q8 (96 * 256)
#define MENU_SPRING_DIVISOR 32
#define MENU_DRAG 12

static int32_t MenuAbs(int32_t value) { return value < 0 ? -value : value; }

static uint8_t MenuNeighbour(int slot)
{
    int item = (int)s_selected_item + slot;
    while (item < 0) item += MENU_ITEM_COUNT;
    while (item >= MENU_ITEM_COUNT) item -= MENU_ITEM_COUNT;
    return (uint8_t)item;
}

static uint8_t MenuVisibleItem(void)
{
    int slot = (int)((MenuAbs(s_offset) + MENU_SPACING_Q8 / 2) / MENU_SPACING_Q8);
    return MenuNeighbour(s_offset < 0 ? slot : -slot);
}

static void MenuAnimate(uint32_t now_ms)
{
    uint32_t elapsed = now_ms - s_animation_ms;
    s_animation_ms = now_ms;
    if (elapsed > 100U) elapsed = 100U;
    elapsed += s_remainder_ms;
    for (; elapsed >= MENU_MOVE_STEP_MS; elapsed -= MENU_MOVE_STEP_MS) {
        s_velocity += (-s_offset - MENU_DRAG * s_velocity) / MENU_SPRING_DIVISOR;
        s_offset += s_velocity;
        if (MenuAbs(s_offset) < 128 && MenuAbs(s_velocity) < 32)
            s_offset = s_velocity = 0;
    }
    s_remainder_ms = (uint8_t)elapsed;
}

static void MenuDrawIcon(GameId_t id, int16_t center, uint8_t pose)
{
    int x = center - MENU_ICON_WIDTH / 2;
    int first = x < 0 ? -x : 0;
    int last = x + MENU_ICON_WIDTH > 128 ? 128 - x : MENU_ICON_WIDTH;
    int row, column;
    if (first >= last) return;
    for (row = 0; row < MENU_ICON_HEIGHT; ++row) {
        uint32_t bits = s_menu_icons[id][pose][row];
        for (column = first; column < last; ++column)
            if (bits & (1UL << (31 - column)))
                DisplayService_Pixel((int16_t)(x + column), (int16_t)(8 + row));
    }
}

/* The Settings item is deliberately drawn here instead of adding a pseudo
   game to game_id.h.  This keeps the game registration table unchanged. */
static void MenuDrawSettingsIcon(int16_t center)
{
    int16_t left = (int16_t)(center - 15);
    DisplayService_Rect(left, 10, 30, 12);
    DisplayService_Rect((int16_t)(center - 5), 13, 10, 6);
    DisplayService_Line((int16_t)(center - 10), 16,
                        (int16_t)(center + 10), 16);
}

static void MenuDrawItem(uint8_t item, int16_t center, uint8_t pose)
{
    if (item == MENU_ITEM_SETTINGS) {
        (void)pose;
        MenuDrawSettingsIcon(center);
    } else if (item < (uint8_t)GAME_ID_COUNT) {
        MenuDrawIcon((GameId_t)item, center, pose);
    }
}

void MenuService_Init(void)
{
    s_selected_item = (uint8_t)GAME_ID_DINO;
    s_start_pending = 0U;
    s_settings_pending = 0U;
    s_move_ready_ms = HAL_GetTick();
    s_start_ready_ms = s_move_ready_ms + SERVICE_MENU_START_GUARD_MS;
    s_animation_ms = s_move_ready_ms;
    s_offset = s_velocity = 0;
    s_remainder_ms = 0U;
}

void MenuService_HandleInput(void)
{
    InputEvent_t event;
    uint32_t now = HAL_GetTick();
    MenuAnimate(now);
    while (InputService_GetEvent(&event)) {
        if (!event.pressed || s_start_pending || s_settings_pending) continue;
        if ((event.code == INPUT_UP || event.code == INPUT_DOWN) &&
            (int32_t)(now - s_move_ready_ms) < 0) continue;
        if (event.code == INPUT_UP || event.code == INPUT_DOWN) {
            int direction = event.code == INPUT_DOWN ? 1 : -1;
            int32_t next_offset = s_offset + direction * MENU_SPACING_Q8;
            if (MenuAbs(next_offset) > MENU_MAX_OFFSET_Q8) continue;
            s_selected_item = MenuNeighbour(direction);
            s_offset = next_offset;
            s_move_ready_ms = now + SERVICE_MENU_DAMPING_MS;
        } else if (event.code == INPUT_A) {
            if ((int32_t)(now - s_start_ready_ms) >= 0) {
                if (s_selected_item == MENU_ITEM_SETTINGS)
                    s_settings_pending = 1U;
                else
                    s_start_pending = 1U;
            }
            break;
        }
    }
}

void MenuService_Render(void)
{
    int slot;
    int16_t offset_px;
    uint8_t pose;
    uint8_t visible;
    uint32_t now = HAL_GetTick();
    MenuAnimate(now);
    if (!DisplayService_BeginFrame()) return;
    offset_px = (int16_t)(s_offset / 256);
    pose = (uint8_t)((now / MENU_ICON_FRAME_MS) & 1U);
    for (slot = -2; slot <= 2; ++slot)
        MenuDrawItem(MenuNeighbour(slot),
                     (int16_t)(64 + slot * 64 + offset_px), pose);
    visible = MenuVisibleItem();
    if (visible == MENU_ITEM_SETTINGS)
        DisplayService_Text(2, 25, "SETTINGS");
    else
        DisplayService_Text(2, 25, s_names[visible]);
    DisplayService_Number(110, 0, (uint16_t)(visible + 1U), 1U);
    DisplayService_Text(116, 0, "-8");
    DisplayService_Present();
}

uint8_t MenuService_ConsumeStart(GameId_t *game_id)
{
    if (!game_id || !s_start_pending || s_offset || s_velocity ||
        s_selected_item >= (uint8_t)GAME_ID_COUNT) return 0U;
    *game_id = (GameId_t)s_selected_item;
    s_start_pending = 0U;
    return 1U;
}

uint8_t MenuService_ConsumeSettings(void)
{
    if (!s_settings_pending || s_offset || s_velocity ||
        s_selected_item != MENU_ITEM_SETTINGS) return 0U;
    s_settings_pending = 0U;
    return 1U;
}

#ifdef ARCADE_TEST
void MenuService_Debug(MenuDebug *debug)
{
    if (!debug) return;
    debug->selected = (GameId_t)s_selected_item;
    debug->visible = (GameId_t)MenuVisibleItem();
    debug->offset = s_offset;
    debug->velocity = s_velocity;
    debug->start_pending = s_start_pending;
}
#endif
