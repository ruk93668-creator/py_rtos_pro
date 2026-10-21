#ifndef GAME_ARCADE_H
#define GAME_ARCADE_H

#include <stdint.h>
#include "game_snake.h"
#ifdef ARCADE_TEST
typedef struct { int32_t x,y,velocity; uint16_t score; uint8_t dead; } ArcadeDebug;
void Arcade_Debug(unsigned game, ArcadeDebug *out);
#endif

void GamePlane_Init(void);
void GamePlane_Enter(void);
void GamePlane_Update(uint32_t dt_ms);
void GamePlane_Render(void);
void GamePlane_Exit(void);

void GameRacing_Init(void);
void GameRacing_Enter(void);
void GameRacing_Update(uint32_t dt_ms);
void GameRacing_Render(void);
void GameRacing_Exit(void);

void GameFlappy_Init(void);
void GameFlappy_Enter(void);
void GameFlappy_Update(uint32_t dt_ms);
void GameFlappy_Render(void);
void GameFlappy_Exit(void);

void GameShooter_Init(void);
void GameShooter_Enter(void);
void GameShooter_Update(uint32_t dt_ms);
void GameShooter_Render(void);
void GameShooter_Exit(void);

void GameFishing_Init(void);
void GameFishing_Enter(void);
void GameFishing_Update(uint32_t dt_ms);
void GameFishing_Render(void);
void GameFishing_Exit(void);

#endif
