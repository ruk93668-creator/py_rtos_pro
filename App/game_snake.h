#ifndef GAME_SNAKE_H
#define GAME_SNAKE_H
#include <stdint.h>

#define SNAKE_COLS 16U
#define SNAKE_ROWS 3U
#define SNAKE_CAPACITY (SNAKE_COLS * SNAKE_ROWS)
#define SNAKE_MOVE_MS 140U
typedef enum { SNAKE_RIGHT, SNAKE_DOWN, SNAKE_LEFT, SNAKE_UP } SnakeDirection;

void GameSnake_Init(void);
void GameSnake_Enter(void);
void GameSnake_Update(uint32_t dt_ms);
void GameSnake_Render(void);
void GameSnake_Exit(void);
#ifdef ARCADE_TEST
typedef struct {
    uint8_t cells[SNAKE_CAPACITY], length, food, dead, won, queued;
    SnakeDirection direction;
    uint16_t score;
} SnakeDebug;
void GameSnake_Debug(SnakeDebug *out);
void GameSnake_TestSetup(const uint8_t *cells, uint8_t length, SnakeDirection direction, uint8_t food);
void GameSnake_TestPlaceFood(void);
#endif
#endif
