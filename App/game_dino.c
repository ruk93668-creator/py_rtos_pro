#include "game_dino.h"
#include "game_util.h"
#include "service_display.h"
#include "service_enmu.h"
#include <string.h>
#define GROUND 30
#define PLAYER_X 18
#define STEP_MS 5U
#define COUNT 3
typedef enum { PLAYER_RUN, PLAYER_JUMP, PLAYER_FALL, PLAYER_CROUCH, PLAYER_DEAD } PlayerState;
static struct {
    int32_t foot, velocity, obstacle[COUNT];
    uint16_t score, elapsed, accumulator;
    uint8_t height[COUNT], bird[COUNT], crouch, jump_pending;
    PlayerState state;
} s_dino;
static int16_t PlayerHeight(void) { return s_dino.crouch ? 5 : 9; }
static int16_t PlayerY(void) { return (int16_t)(s_dino.foot / 256) - PlayerHeight(); }
static void Reset(void)
{
    unsigned i;
    memset(&s_dino, 0, sizeof(s_dino));
    s_dino.foot = GROUND * 256;
    for (i = 0; i < COUNT; ++i) {
        s_dino.obstacle[i] = (120 + (int32_t)i * 85) * 256;
        s_dino.height[i] = (uint8_t)(6 + i);
        s_dino.bird[i] = (uint8_t)(i == 1);
    }
}
void GameDino_Init(void) { Reset(); }
void GameDino_Enter(void) { Reset(); }
static void Step(void)
{
    unsigned i, j;
    int32_t farthest;
    int16_t x, y, py;
    int32_t speed = 75 + s_dino.score / 4;
    if (speed > 110) speed = 110;
    if (s_dino.jump_pending && s_dino.foot == GROUND * 256) {
        s_dino.velocity = -72 * 256;
    }
    s_dino.jump_pending = 0;
    if (s_dino.foot < GROUND * 256 || s_dino.velocity < 0) {
        s_dino.velocity += 240 * 256 * (int32_t)STEP_MS / 1000;
        s_dino.foot += s_dino.velocity * (int32_t)STEP_MS / 1000;
        if (s_dino.foot >= GROUND * 256) { s_dino.foot = GROUND * 256; s_dino.velocity = 0; }
    }
    /* Resolve the grounded pose after integration, including the landing step. */
    s_dino.crouch = (uint8_t)(InputService_IsDown(INPUT_DOWN) && s_dino.foot == GROUND * 256);
    s_dino.state = s_dino.foot < GROUND * 256 ?
        (s_dino.velocity < 0 ? PLAYER_JUMP : PLAYER_FALL) :
        (s_dino.crouch ? PLAYER_CROUCH : PLAYER_RUN);
    s_dino.elapsed += STEP_MS;
    if (s_dino.elapsed >= 1000U) {
        s_dino.elapsed -= 1000U;
        if (s_dino.score < 65535U) ++s_dino.score;
    }
    py = PlayerY();
    for (i = 0; i < COUNT; ++i) {
        s_dino.obstacle[i] -= speed * 256 * (int32_t)STEP_MS / 1000;
        if (s_dino.obstacle[i] < -8 * 256) {
            farthest = 128 * 256;
            for (j = 0; j < COUNT; ++j)
                if (s_dino.obstacle[j] > farthest) farthest = s_dino.obstacle[j];
            s_dino.obstacle[i] = farthest + (100 + GameUtil_Random(45)) * 256;
            s_dino.height[i] = (uint8_t)(5 + GameUtil_Random(4));
            s_dino.bird[i] = (uint8_t)(GameUtil_Random(4) == 0);
        }
        x = (int16_t)(s_dino.obstacle[i] / 256);
        y = s_dino.bird[i] ? 20 : GROUND - s_dino.height[i];
        if (PLAYER_X < x + 6 && PLAYER_X + (s_dino.crouch ? 10 : 8) > x &&
            py < y + (s_dino.bird[i] ? 4 : s_dino.height[i]) && py + PlayerHeight() > y)
            s_dino.state = PLAYER_DEAD;
    }
}
void GameDino_Update(uint32_t dt_ms)
{
    uint8_t jump = GameUtil_WasPressed(INPUT_A);
    if (s_dino.state == PLAYER_DEAD) { if (jump) Reset(); return; }
    if (jump) s_dino.jump_pending = 1;
    if (dt_ms > 100U) dt_ms = 100U; /* Bound catch-up work after a debugger halt. */
    s_dino.accumulator = (uint16_t)(s_dino.accumulator + dt_ms);
    while (s_dino.accumulator >= STEP_MS && s_dino.state != PLAYER_DEAD) {
        s_dino.accumulator -= STEP_MS;
        Step();
    }
}
/* Hand-drawn row masks; artwork stays inside the collision boxes. */
static void DrawPlayer(void)
{
    static const uint16_t run[2][9] = {
        {0x1CU,0x3EU,0x37U,0x7FU,0xF9U,0x7EU,0x3CU,0x28U,0x42U},
        {0x1CU,0x3EU,0x37U,0x7FU,0xF9U,0x7EU,0x3CU,0x14U,0x44U}
    };
    static const uint16_t jump[9] = {0x1CU,0x3EU,0x37U,0x7FU,0xF9U,0x7EU,0x3CU,0x24U,0x18U};
    static const uint16_t fall[9] = {0x1CU,0x3EU,0x37U,0x7FU,0xF9U,0x7EU,0x3CU,0x24U,0x66U};
    static const uint16_t crouch[2][5] = {
        {0x007U,0x1FFU,0x3FBU,0x1FEU,0x120U},
        {0x007U,0x1FFU,0x3FBU,0x1FEU,0x048U}
    };
    const uint16_t *pose = run[(s_dino.elapsed / 100U) & 1U];
    int16_t top = PlayerY();
    uint8_t width = s_dino.crouch ? 10U : 8U;
    uint8_t height = (uint8_t)PlayerHeight();
    if (s_dino.state == PLAYER_JUMP) pose = jump;
    else if (s_dino.state == PLAYER_FALL) pose = fall;
    if (s_dino.crouch) pose = crouch[(s_dino.elapsed / 100U) & 1U];
    GameUtil_DrawSprite(PLAYER_X, top, pose, width, height);
}

static void DrawCactus(int16_t x, int16_t y, uint8_t height)
{
    static const uint16_t masks[4][8] = {
        {0x0CU,0x0CU,0x3CU,0x0CU,0x0CU,0x000U,0x000U,0x000U},
        {0x0CU,0x0CU,0x3CU,0x0CU,0x0FU,0x0CU,0x000U,0x000U},
        {0x0CU,0x0CU,0x3CU,0x0CU,0x0CU,0x0FU,0x0CU,0x000U},
        {0x0CU,0x0CU,0x3CU,0x0CU,0x0CU,0x0FU,0x0CU,0x0CU}
    };
    uint8_t index = height < 5U ? 0U : (height > 8U ? 3U : (uint8_t)(height - 5U));
    GameUtil_DrawSprite(x, y, masks[index], 6U, height);
}

static void DrawBird(int16_t x, int16_t y)
{
    static const uint16_t wings[2][4] = {
        {0x0CU,0x1EU,0x3FU,0x12U},
        {0x0CU,0x1EU,0x3FU,0x0CU}
    };
    GameUtil_DrawSprite(x, y, wings[(s_dino.elapsed / 100U) & 1U], 6U, 4U);
}
void GameDino_Render(void)
{
    unsigned i;
    if (!DisplayService_BeginFrame()) return;
    DisplayService_Text(0, 0, "DINO");
    DisplayService_Number(96, 0, s_dino.score, 5);
    if (s_dino.state == PLAYER_DEAD) {
        DisplayService_Text(34, 11, "GAME OVER");
        DisplayService_Text(28, 23, "A RESTART");
    } else {
        DisplayService_Line(0, GROUND, 127, GROUND);
        DrawPlayer();
        for (i = 0; i < COUNT; ++i) {
            if (s_dino.bird[i])
                DrawBird((int16_t)(s_dino.obstacle[i] / 256), 20);
            else
                DrawCactus((int16_t)(s_dino.obstacle[i] / 256),
                           (int16_t)(GROUND - s_dino.height[i]), s_dino.height[i]);
        }
    }
    DisplayService_Present();
}
void GameDino_Exit(void) {}
#ifdef ARCADE_TEST
void GameDino_Debug(DinoDebug *out)
{
    out->foot = s_dino.foot; out->velocity = s_dino.velocity;
    out->obstacle = s_dino.obstacle[0]; out->dead = (uint8_t)(s_dino.state == PLAYER_DEAD);
    out->crouch = s_dino.crouch; out->score = s_dino.score;
}
void GameDino_TestObstacle(uint8_t bird)
{
    s_dino.obstacle[0] = PLAYER_X * 256;
    s_dino.bird[0] = bird;
}
void GameDino_TestLanding(void)
{
    s_dino.foot = GROUND * 256 - 1;
    s_dino.velocity = 60 * 256;
    s_dino.crouch = 0;
    s_dino.state = PLAYER_FALL;
}
#endif
