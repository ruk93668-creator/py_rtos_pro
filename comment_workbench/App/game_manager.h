#ifndef GAME_MANAGER_H
#define GAME_MANAGER_H

#include <stdint.h>
#include "game_id.h"

typedef struct
{
    void (*Init)(void);
    void (*Enter)(void);
    void (*Update)(uint32_t dt_ms);
    void (*Render)(void);
    void (*Exit)(void);
} GameOps_t;

void GameManager_Init(void);
uint8_t GameManager_Start(GameId_t game_id);
void GameManager_Update(uint32_t dt_ms);
void GameManager_Render(void);
uint8_t GameManager_IsRunning(void);
uint8_t GameManager_ConsumeExit(void);

#endif
