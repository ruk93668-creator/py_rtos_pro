#include "game_manager.h"
#include "game_dino.h"
#include "game_snake.h"
#include "game_arcade.h"
#include "service_enmu.h"
#include "service_display.h"

typedef struct
{
    GameId_t id;
    GameOps_t ops;
} GameRegistration_t;

static const GameRegistration_t s_games[GAME_ID_COUNT] =
{
    {GAME_ID_DINO, {GameDino_Init, GameDino_Enter, GameDino_Update, GameDino_Render, GameDino_Exit}},
    {GAME_ID_SNAKE, {GameSnake_Init, GameSnake_Enter, GameSnake_Update, GameSnake_Render, GameSnake_Exit}},
    {GAME_ID_PLANE, {GamePlane_Init, GamePlane_Enter, GamePlane_Update, GamePlane_Render, GamePlane_Exit}},
    {GAME_ID_RACING, {GameRacing_Init, GameRacing_Enter, GameRacing_Update, GameRacing_Render, GameRacing_Exit}},
    {GAME_ID_FLAPPY, {GameFlappy_Init, GameFlappy_Enter, GameFlappy_Update, GameFlappy_Render, GameFlappy_Exit}},
    {GAME_ID_SHOOTER, {GameShooter_Init, GameShooter_Enter, GameShooter_Update, GameShooter_Render, GameShooter_Exit}},
    {GAME_ID_FISHING, {GameFishing_Init, GameFishing_Enter, GameFishing_Update, GameFishing_Render, GameFishing_Exit}}
};

static GameId_t s_current_id;
static uint8_t s_running;
static uint8_t s_exit_pending;
static uint8_t s_paused;

void GameManager_Init(void)
{
    uint8_t index;

    s_current_id = GAME_ID_DINO;
    s_running = 0U;
    s_exit_pending = 0U;
    s_paused = 0U;
    for (index = 0U; index < GAME_ID_COUNT; index++)
    {
        s_games[index].ops.Init();
    }
}

uint8_t GameManager_Start(GameId_t game_id)
{
    if ((unsigned)game_id >= GAME_ID_COUNT)
    {
        return 0U;
    }
    if (s_running != 0U)
    {
        s_games[s_current_id].ops.Exit();
    }
    s_current_id = game_id;
    InputService_ClearFrame();
    s_paused = 0;
    s_games[s_current_id].ops.Enter();
    s_running = 1U;
    s_exit_pending = 0U;
    return 1U;
}

void GameManager_Update(uint32_t dt_ms)
{
    if (s_running == 0U)
    {
        return;
    }
    if (InputService_ConsumeBack() != 0U)
    {
        s_games[s_current_id].ops.Exit();
        s_running = 0U;
        s_exit_pending = 1U;
        return;
    }
    if (InputService_ConsumePress(INPUT_PAUSE)) s_paused ^= 1U;
    if (!s_paused) s_games[s_current_id].ops.Update(dt_ms);
}

void GameManager_Render(void)
{
    if (s_running != 0U)
    {
        if (s_paused) {
            if (!DisplayService_BeginFrame()) return;
            DisplayService_Text(40, 5, "PAUSED");
            DisplayService_Text(4, 18, "SHAKE TO RESUME");
            DisplayService_Present();
            return;
        }
        s_games[s_current_id].ops.Render();
    }
}

uint8_t GameManager_IsRunning(void)
{
    return s_running;
}

uint8_t GameManager_ConsumeExit(void)
{
    uint8_t value;

    value = s_exit_pending;
    s_exit_pending = 0U;
    return value;
}
