#ifndef GAME_DINO_H
#define GAME_DINO_H

#include <stdint.h>

void GameDino_Init(void);
void GameDino_Enter(void);
void GameDino_Update(uint32_t dt_ms);
void GameDino_Render(void);
void GameDino_Exit(void);
#ifdef ARCADE_TEST
typedef struct {
    int32_t foot, velocity, obstacle;
    uint16_t score;
    uint8_t dead, crouch;
} DinoDebug;
void GameDino_Debug(DinoDebug *out);
void GameDino_TestObstacle(uint8_t bird);
void GameDino_TestLanding(void);
#endif

#endif
