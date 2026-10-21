#include "game_snake.h"
#include "game_util.h"
#include "service_enmu.h"
#include "service_display.h"
#include <string.h>

/* 16x3 board, relative rotary turns, 140ms/cell. Each cell uses one byte;
   the game uses only static storage. */
static struct {
    uint8_t cells[SNAKE_CAPACITY], length, food, dead, won;
    SnakeDirection direction;
    int8_t turns[2];
    uint8_t turn_count;
    uint16_t elapsed, score;
} s_snake;

static uint8_t Occupies(uint8_t cell, unsigned count)
{
    unsigned i;
    for (i = 0; i < count; ++i) if (s_snake.cells[i] == cell) return 1;
    return 0;
}
static void PlaceFood(void)
{
    unsigned cell;
    uint16_t rank;
    if (s_snake.length == SNAKE_CAPACITY) { s_snake.won = 1; return; }
    /* Select among every empty cell, including when only one remains. */
    rank = GameUtil_Random((uint16_t)(SNAKE_CAPACITY - s_snake.length));
    for (cell = 0; cell < SNAKE_CAPACITY; ++cell) {
        if (Occupies((uint8_t)cell, s_snake.length)) continue;
        if (rank == 0) { s_snake.food = (uint8_t)cell; return; }
        --rank;
    }
}
static void Reset(void)
{
    memset(&s_snake, 0, sizeof(s_snake));
    s_snake.length = 3;
    s_snake.cells[0] = 24; s_snake.cells[1] = 23; s_snake.cells[2] = 22;
    s_snake.direction = SNAKE_RIGHT;
    s_snake.food = 28;
}
static void Step(void)
{
    int x, y;
    uint8_t next, grows;
    unsigned i, checked;
    /* A turn is committed with a move: two fast detents cannot reverse
       into the neck before the head has advanced one cell. */
    if (s_snake.turn_count) {
        s_snake.direction = (SnakeDirection)(((int)s_snake.direction + s_snake.turns[0] + 4) % 4);
        s_snake.turns[0] = s_snake.turns[1];
        --s_snake.turn_count;
    }
    x = s_snake.cells[0] % SNAKE_COLS;
    y = s_snake.cells[0] / SNAKE_COLS;
    if (s_snake.direction == SNAKE_RIGHT) ++x;
    else if (s_snake.direction == SNAKE_LEFT) --x;
    else if (s_snake.direction == SNAKE_DOWN) ++y;
    else --y;
    if (x < 0 || x >= (int)SNAKE_COLS || y < 0 || y >= (int)SNAKE_ROWS) {
        s_snake.dead = 1; return;
    }
    next = (uint8_t)(y * (int)SNAKE_COLS + x);
    grows = (uint8_t)(next == s_snake.food);
    checked = s_snake.length - (grows ? 0U : 1U);
    /* The current tail cell is free on a non-growing move. */
    if (Occupies(next, checked)) { s_snake.dead = 1; return; }
    if (grows) ++s_snake.length;
    for (i = s_snake.length - 1U; i > 0; --i) s_snake.cells[i] = s_snake.cells[i - 1U];
    s_snake.cells[0] = next;
    if (grows) { ++s_snake.score; PlaceFood(); }
}
void GameSnake_Init(void) { Reset(); }
void GameSnake_Enter(void) { Reset(); }
void GameSnake_Exit(void) {}
void GameSnake_Update(uint32_t dt_ms)
{
    InputEvent_t event;
    uint8_t restart = 0;
    while (InputService_GetEvent(&event)) {
        if (!event.pressed) continue;
        if (event.source == INPUT_SOURCE_BUTTON && event.code == INPUT_A) restart = 1;
        if (event.source == INPUT_SOURCE_ROTARY && s_snake.turn_count < 2U &&
            (event.code == INPUT_UP || event.code == INPUT_DOWN))
            s_snake.turns[s_snake.turn_count++] = event.code == INPUT_UP ? -1 : 1;
    }
    if (s_snake.dead || s_snake.won) { if (restart) Reset(); return; }
    if (dt_ms > 100U) dt_ms = 100U;
    s_snake.elapsed = (uint16_t)(s_snake.elapsed + dt_ms);
    if (s_snake.elapsed >= SNAKE_MOVE_MS) {
        s_snake.elapsed = (uint16_t)(s_snake.elapsed - SNAKE_MOVE_MS);
        Step();
    }
}

static void DrawSnakeSegment(int16_t x, int16_t y, unsigned index)
{
    static const uint16_t body[7] = {
        0x1CU,0x3EU,0x7FU,0x6BU,0x7FU,0x3EU,0x1CU
    };
    static const uint16_t tail[7] = {
        0x08U,0x1CU,0x3EU,0x3EU,0x1CU,0x08U,0x00U
    };
    static const uint16_t head[4][7] = {
        /* right: two eye holes and a pointed nose */
        {0x18U,0x3CU,0x6EU,0x7FU,0x6EU,0x3CU,0x18U},
        /* down */
        {0x18U,0x3CU,0x66U,0x7EU,0x7EU,0x7EU,0x3CU},
        /* left */
        {0x18U,0x3CU,0x3BU,0x7FU,0x3BU,0x3CU,0x18U},
        /* up */
        {0x3CU,0x7EU,0x7EU,0x7EU,0x66U,0x3CU,0x18U}
    };
    const uint16_t *sprite = body;

    if (index == 0U) sprite = head[s_snake.direction];
    else if (index + 1U == s_snake.length) sprite = tail;
    GameUtil_DrawSprite((int16_t)(x + 1), (int16_t)(y + 1), sprite, 7U, 7U);
}

static void DrawFood(int16_t x, int16_t y)
{
    static const uint16_t apple[7] = {
        0x10U,0x18U,0x3CU,0x7EU,0x7EU,0x3CU,0x18U
    };
    GameUtil_DrawSprite(x, y, apple, 7U, 7U);
}

void GameSnake_Render(void)
{
    unsigned i;
    int16_t x, y;
    if (!DisplayService_BeginFrame()) return;
    DisplayService_Text(0, 0, "SNAKE");
    DisplayService_Number(96, 0, s_snake.score, 5);
    if (s_snake.dead || s_snake.won) {
        DisplayService_Text(34, 11, s_snake.won ? "YOU WIN!" : "GAME OVER");
        DisplayService_Text(28, 23, "A RESTART");
    } else {
        for (i = 0; i < s_snake.length; ++i) {
            x = (int16_t)((s_snake.cells[i] % SNAKE_COLS) * 8U);
            y = (int16_t)(8U + (s_snake.cells[i] / SNAKE_COLS) * 8U);
            DrawSnakeSegment(x, y, i);
        }
        DrawFood((int16_t)((s_snake.food % SNAKE_COLS) * 8U + 1U),
                 (int16_t)(9U + (s_snake.food / SNAKE_COLS) * 8U));
    }
    DisplayService_Present();
}
#ifdef ARCADE_TEST
void GameSnake_Debug(SnakeDebug *out)
{
    memcpy(out->cells, s_snake.cells, sizeof(out->cells));
    out->length = s_snake.length; out->direction = s_snake.direction;
    out->food = s_snake.food; out->score = s_snake.score;
    out->dead = s_snake.dead; out->won = s_snake.won; out->queued = s_snake.turn_count;
}
void GameSnake_TestSetup(const uint8_t *cells, uint8_t length, SnakeDirection direction, uint8_t food)
{
    Reset();
    if (!length || length > SNAKE_CAPACITY) return;
    memcpy(s_snake.cells, cells, length);
    s_snake.length = length; s_snake.direction = direction; s_snake.food = food;
}
void GameSnake_TestPlaceFood(void) { PlaceFood(); }
#endif
