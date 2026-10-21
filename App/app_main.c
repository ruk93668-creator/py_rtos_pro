#include "app_main.h"
#include "app_settings.h"
#include "game_manager.h"
#include "service_enmu.h"

typedef enum
{
    APP_MODE_MENU = 0,
    APP_MODE_GAME,
    APP_MODE_SETTINGS
} AppMode_t;

static AppMode_t s_mode;

void AppMain_Init(void)
{
    MenuService_Init();
    AppSettings_Init();
    GameManager_Init();
    s_mode = APP_MODE_MENU;
}

void AppMain_Process(uint32_t dt_ms)
{
    GameId_t game_id;
    InputService_BeginFrame();

    if (s_mode == APP_MODE_MENU)
    {
        MenuService_HandleInput();
        if (MenuService_ConsumeSettings() != 0U)
        {
            AppSettings_ClearDone();
            s_mode = APP_MODE_SETTINGS;
            AppSettings_Render();
        }
        else if (MenuService_ConsumeStart(&game_id) != 0U)
        {
            if (GameManager_Start(game_id) != 0U)
            {
                s_mode = APP_MODE_GAME;
                GameManager_Render();
            }
        }
        else
        {
            MenuService_Render();
        }
    }
    else if (s_mode == APP_MODE_SETTINGS)
    {
        AppSettings_Process();
        if (AppSettings_IsDone() != 0U)
        {
            AppSettings_ClearDone();
            s_mode = APP_MODE_MENU;
            MenuService_Render();
        }
        else
        {
            AppSettings_Render();
        }
    }
    else
    {
        GameManager_Update(dt_ms);
        if (GameManager_IsRunning() == 0U)
        {
            s_mode = APP_MODE_MENU;
            MenuService_Render();
        }
        else
        {
            GameManager_Render();
        }
    }
}
