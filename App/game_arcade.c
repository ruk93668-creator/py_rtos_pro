#include "game_arcade.h"
#include "game_snake.h"
#include "game_util.h"
#include "service_display.h"
#include "service_enmu.h"
#include <string.h>

/* All positions/velocities are Q8. Fixed 10ms integration preserves subpixels. */
#define Q 256
#define A 1U
#define UP 2U
#define DOWN 4U
typedef struct { uint16_t accumulator; uint8_t pending; } GameClock;
typedef struct { int32_t x, y; uint8_t active; } Object;
static int16_t Pixel(int32_t value) { return (int16_t)(value / Q); }
static int32_t Move(int32_t speed) { return speed * Q / 100; }
static uint8_t Hit(int32_t x, int32_t y, int w, int h, int32_t bx, int32_t by, int bw, int bh)
{
    return (uint8_t)(x < bx + bw*Q && x + w*Q > bx && y < by + bh*Q && y + h*Q > by);
}
static void Score(uint16_t *score) { if (*score < 65535U) ++*score; }
static void Advance(GameClock *clock, uint32_t dt, void (*step)(uint8_t))
{
    if (GameUtil_WasPressed(INPUT_A)) clock->pending |= A;
    if (GameUtil_WasPressed(INPUT_UP)) clock->pending |= UP;
    if (GameUtil_WasPressed(INPUT_DOWN)) clock->pending |= DOWN;
    if (dt > 100U) dt = 100U;
    clock->accumulator = (uint16_t)(clock->accumulator + dt);
    while (clock->accumulator >= 10U) {
        uint8_t pressed = clock->pending;
        clock->pending = 0;
        clock->accumulator -= 10U;
        step(pressed);
    }
}
static void Header(const char *name, uint16_t score)
{
    DisplayService_Text(0,0,name);
    DisplayService_Number(96,0,score,5);
}
static void GameOver(void)
{
    DisplayService_Text(34,11,"GAME OVER");
    DisplayService_Text(28,23,"A RESTART");
}
static void Jump(int32_t *foot, int32_t *velocity, uint8_t press)
{
    if (press && *foot == 30*Q) *velocity = -70*Q;
    if (*foot < 30*Q || *velocity < 0) {
        *velocity += Move(220);
        *foot += *velocity / 100;
        if (*foot >= 30*Q) { *foot = 30*Q; *velocity = 0; }
    }
}

/* PLANE: four bullets/four enemies, automatic fire and vertical steering. */
static struct {
    GameClock clock;
    int32_t y;
    Object bullets[4], enemies[4];
    uint16_t fire_ms, anim_ms, score;
    uint8_t dead;
} plane;
static void PlaneReset(void)
{
    unsigned i; memset(&plane,0,sizeof(plane)); plane.y=16*Q;
    for(i=0;i<4;++i) {
        plane.enemies[i].x=(100+(int32_t)i*50)*Q;
        plane.enemies[i].y=(9+GameUtil_Random(17))*Q;
    }
}
static void Fire(Object *bullets, int32_t y)
{
    unsigned i;
    for(i=0;i<4;++i) if (!bullets[i].active) {
        bullets[i].active=1; bullets[i].x=24*Q; bullets[i].y=y; break;
    }
}
static void Respawn(Object *enemies, unsigned count, unsigned index, uint8_t airborne)
{
    unsigned i; int32_t farthest=128*Q;
    for(i=0;i<count;++i) if (enemies[i].x>farthest) farthest=enemies[i].x;
    enemies[index].x=farthest+(40+GameUtil_Random(25))*Q;
    enemies[index].y=(airborne?(9+GameUtil_Random(17)):24)*Q;
}
static void DrawPlane(int16_t x, int16_t y)
{
    static const uint16_t sprite[2][5] = {
        {0x040U,0x1F0U,0x3B7U,0x1F0U,0x048U},
        {0x040U,0x1F0U,0x3B7U,0x1F0U,0x024U}
    };
    GameUtil_DrawSprite(x, y, sprite[(plane.anim_ms / 80U) & 1U], 10U, 5U);
}
static void DrawPlaneEnemy(int16_t x, int16_t y)
{
    static const uint16_t sprite[2][6] = {
        {0x0CU,0x1EU,0x3FU,0x35U,0x3FU,0x12U},
        {0x0CU,0x1EU,0x3FU,0x2DU,0x3FU,0x24U}
    };
    GameUtil_DrawSprite(x, y, sprite[(plane.anim_ms / 120U) & 1U], 6U, 6U);
}
static void PlaneStep(uint8_t press)
{
    unsigned i,j;
    if(plane.dead) { if(press&A) PlaneReset(); return; }
    plane.anim_ms = (uint16_t)(plane.anim_ms + 10U);
    /* PLANE steering is rotary-only in this target.  A persistent motion
       state must not integrate every frame and pin the aircraft at an edge. */
    (void)press;
    if (InputService_IsRotaryDown(INPUT_UP) && !InputService_IsRotaryDown(INPUT_DOWN))
        plane.y-=Move(80);
    else if (InputService_IsRotaryDown(INPUT_DOWN) && !InputService_IsRotaryDown(INPUT_UP))
        plane.y+=Move(80);
    if(plane.y<8*Q) plane.y=8*Q;
    if(plane.y>26*Q) plane.y=26*Q;
    plane.fire_ms+=10;
    if(plane.fire_ms>=125 || (press&A)) { plane.fire_ms=0; Fire(plane.bullets,plane.y+2*Q); }
    for(i=0;i<4;++i) {
        if(plane.bullets[i].active) {
            plane.bullets[i].x+=Move(140);
            if(plane.bullets[i].x>128*Q) plane.bullets[i].active=0;
        }
        plane.enemies[i].x-=Move(60);
        if(plane.enemies[i].x<-6*Q) Respawn(plane.enemies,4,i,1);
        for(j=0;j<4;++j) if(plane.bullets[j].active &&
            Hit(plane.bullets[j].x,plane.bullets[j].y,3,1,plane.enemies[i].x,plane.enemies[i].y,6,6)) {
            plane.bullets[j].active=0; Score(&plane.score); Respawn(plane.enemies,4,i,1);
        }
        if(Hit(12*Q,plane.y,10,5,plane.enemies[i].x,plane.enemies[i].y,6,6)) plane.dead=1;
    }
}
void GamePlane_Init(void) { PlaneReset(); }
void GamePlane_Enter(void) { PlaneReset(); }
void GamePlane_Update(uint32_t dt) { Advance(&plane.clock,dt,PlaneStep); }
void GamePlane_Render(void)
{
    unsigned i;
    if(!DisplayService_BeginFrame()) return;
    Header("PLANE",plane.score);
    if(plane.dead) GameOver();
    else {
        DrawPlane(12,Pixel(plane.y));
        for(i=0;i<4;++i) {
            DrawPlaneEnemy(Pixel(plane.enemies[i].x),Pixel(plane.enemies[i].y));
            if(plane.bullets[i].active) DisplayService_FillRect(Pixel(plane.bullets[i].x),Pixel(plane.bullets[i].y),3,1);
        }
    }
    DisplayService_Present();
}
void GamePlane_Exit(void) {}

/* RACING: one event moves one lane; enemies cannot form an impassable row. */
static struct {
    GameClock clock;
    int32_t x[2];
    uint8_t lane, enemy_lane[2], passed[2], dead;
    uint16_t anim_ms, score;
} racing;
static void RacingReset(void)
{
    memset(&racing,0,sizeof(racing)); racing.lane=1;
    racing.x[0]=120*Q; racing.x[1]=210*Q; racing.enemy_lane[1]=2;
}
static void RacingStep(uint8_t press)
{
    unsigned i;
    if(racing.dead) { if(press&A) RacingReset(); return; }
    racing.anim_ms = (uint16_t)(racing.anim_ms + 10U);
    if((press&UP) && racing.lane>0) --racing.lane;
    if((press&DOWN) && racing.lane<2) ++racing.lane;
    for(i=0;i<2;++i) {
        racing.x[i]-=Move(100);
        if(!racing.passed[i] && racing.x[i]+9*Q<18*Q) { Score(&racing.score); racing.passed[i]=1; }
        if(racing.x[i]<-9*Q) {
            racing.x[i]=racing.x[1-i]+(105+GameUtil_Random(35))*Q;
            racing.enemy_lane[i]=(uint8_t)GameUtil_Random(3); racing.passed[i]=0;
        }
        if(racing.lane==racing.enemy_lane[i] && racing.x[i]<28*Q && racing.x[i]+9*Q>18*Q) racing.dead=1;
    }
}
static void DrawRaceCar(int16_t x, int16_t y, uint8_t player)
{
    static const uint16_t ours[2][5] = {
        {0x060U,0x1F8U,0x3FFU,0x2DBU,0x249U},
        {0x060U,0x1F8U,0x3FFU,0x36DU,0x249U}
    };
    static const uint16_t rival[2][5] = {
        {0x030U,0x0F8U,0x1FFU,0x155U,0x1A3U},
        {0x030U,0x0F8U,0x1FFU,0x1ABU,0x145U}
    };
    if (player)
        GameUtil_DrawSprite(x, y, ours[(racing.anim_ms / 100U) & 1U], 10U, 5U);
    else
        GameUtil_DrawSprite(x, y, rival[(racing.anim_ms / 100U) & 1U], 9U, 5U);
}
void GameRacing_Init(void) { RacingReset(); }
void GameRacing_Enter(void) { RacingReset(); }
void GameRacing_Update(uint32_t dt) { Advance(&racing.clock,dt,RacingStep); }
void GameRacing_Render(void)
{
    unsigned i;
    if(!DisplayService_BeginFrame()) return;
    Header("RACING",racing.score);
    if(racing.dead) GameOver();
    else {
        for(i=0;i<8;++i) {
            DisplayService_Line((int16_t)(i * 16), 7, (int16_t)(i * 16 + 7), 7);
            DisplayService_Line((int16_t)(i * 16), 15, (int16_t)(i * 16 + 7), 15);
            DisplayService_Line((int16_t)(i * 16), 23, (int16_t)(i * 16 + 7), 23);
        }
        DrawRaceCar(18,(int16_t)(9+8*racing.lane),1U);
        for(i=0;i<2;++i)
            DrawRaceCar(Pixel(racing.x[i]),(int16_t)(9+8*racing.enemy_lane[i]),0U);
    }
    DisplayService_Present();
}
void GameRacing_Exit(void) {}

/* FLAPPY: 12px gap, detailed 8x6 bird, signed Q8 gravity/flap impulse. */
#define FLAPPY_BIRD_WIDTH 8
#define FLAPPY_BIRD_HEIGHT 6
#define FLAPPY_PIPE_WIDTH 8
static struct {
    GameClock clock;
    int32_t y, velocity, pipe;
    uint16_t anim_ms, score;
    uint8_t gap, passed, dead;
} flappy;
static void FlappyReset(void)
{
    memset(&flappy,0,sizeof(flappy)); flappy.y=16*Q; flappy.pipe=120*Q; flappy.gap=10;
}
static void FlappyStep(uint8_t press)
{
    if(flappy.dead) { if(press&A) FlappyReset(); return; }
    flappy.anim_ms = (uint16_t)(flappy.anim_ms + 10U);
    /* Either encoder direction is a deliberate flap; motion-up also arrives as UP. */
    if(press&A || (press&(UP|DOWN))) flappy.velocity=-40*Q;
    flappy.velocity+=Move(150);
    flappy.y+=flappy.velocity/100;
    flappy.pipe-=Move(30);
    if(!flappy.passed && flappy.pipe+8*Q<18*Q) { Score(&flappy.score); flappy.passed=1; }
    if(flappy.pipe<-8*Q) {
        flappy.pipe=128*Q; flappy.gap=(uint8_t)(9+GameUtil_Random(5)); flappy.passed=0;
    }
    if(flappy.y<8*Q || flappy.y+FLAPPY_BIRD_HEIGHT*Q>32*Q ||
       (flappy.pipe<(18+FLAPPY_BIRD_WIDTH)*Q && flappy.pipe+FLAPPY_PIPE_WIDTH*Q>18*Q &&
        (flappy.y<flappy.gap*Q || flappy.y+FLAPPY_BIRD_HEIGHT*Q>(flappy.gap+12)*Q))) flappy.dead=1;
}
static void DrawFlappyBird(int16_t x, int16_t y)
{
    static const uint16_t sprite[2][FLAPPY_BIRD_HEIGHT] = {
        {0x18U,0x7EU,0xFFU,0xDBU,0x3CU,0x18U},
        {0x18U,0x7EU,0xFFU,0xDBU,0x18U,0x3CU}
    };
    GameUtil_DrawSprite(x, y, sprite[(flappy.anim_ms / 80U) & 1U],
                        FLAPPY_BIRD_WIDTH, FLAPPY_BIRD_HEIGHT);
}
static void DrawFlappyPipe(int16_t x, int16_t gap)
{
    int16_t top = (int16_t)(gap - 8);
    int16_t bottom = (int16_t)(32 - gap - 12);
    DisplayService_FillRect((int16_t)(x - 1), 7, 10, 2);
    DisplayService_FillRect(x, 8, FLAPPY_PIPE_WIDTH, top);
    DisplayService_FillRect(x, (int16_t)(gap + 12), FLAPPY_PIPE_WIDTH, bottom);
    DisplayService_FillRect((int16_t)(x - 1), (int16_t)(gap + 11), 10, 2);
}
void GameFlappy_Init(void) { FlappyReset(); }
void GameFlappy_Enter(void) { FlappyReset(); }
void GameFlappy_Update(uint32_t dt) { Advance(&flappy.clock,dt,FlappyStep); }
void GameFlappy_Render(void)
{
    if(!DisplayService_BeginFrame()) return;
    Header("FLAPPY",flappy.score);
    if(flappy.dead) GameOver();
    else {
        DrawFlappyBird(18,Pixel(flappy.y));
        DrawFlappyPipe(Pixel(flappy.pipe),(int16_t)flappy.gap);
    }
    DisplayService_Present();
}
void GameFlappy_Exit(void) {}

/* SHOOTER: A fires, UP jumps over enemies; DOWN crouches. */
static struct {
    GameClock clock;
    int32_t foot,velocity;
    Object bullets[4],enemies[3];
    uint16_t anim_ms, score;
    uint8_t dead,crouch;
} shooter;
static void ShooterReset(void)
{
    unsigned i; memset(&shooter,0,sizeof(shooter)); shooter.foot=30*Q;
    for(i=0;i<3;++i) { shooter.enemies[i].x=(110+(int32_t)i*65)*Q; shooter.enemies[i].y=24*Q; }
}
static void ShooterStep(uint8_t press)
{
    unsigned i,j; int h;
    if(shooter.dead) { if(press&A) ShooterReset(); return; }
    shooter.anim_ms = (uint16_t)(shooter.anim_ms + 10U);
    Jump(&shooter.foot,&shooter.velocity,(uint8_t)(press&UP));
    shooter.crouch=(uint8_t)(InputService_IsDown(INPUT_DOWN) && shooter.foot==30*Q);
    h=shooter.crouch?5:9;
    if(press&A) Fire(shooter.bullets,shooter.foot-(h/2)*Q);
    for(i=0;i<4;++i) if(shooter.bullets[i].active) {
        shooter.bullets[i].x+=Move(150);
        if(shooter.bullets[i].x>128*Q) shooter.bullets[i].active=0;
    }
    for(i=0;i<3;++i) {
        shooter.enemies[i].x-=Move(65);
        if(shooter.enemies[i].x<-6*Q) Respawn(shooter.enemies,3,i,0);
        for(j=0;j<4;++j) if(shooter.bullets[j].active &&
            Hit(shooter.bullets[j].x,shooter.bullets[j].y,3,1,shooter.enemies[i].x,shooter.enemies[i].y,6,6)) {
            shooter.bullets[j].active=0; Score(&shooter.score); Respawn(shooter.enemies,3,i,0);
        }
        if(Hit(16*Q,shooter.foot-h*Q,8,h,shooter.enemies[i].x,shooter.enemies[i].y,6,6)) shooter.dead=1;
    }
}
static void DrawShooterPlayer(int16_t x, int16_t top)
{
    static const uint16_t standing[2][9] = {
        {0x18U,0x3CU,0x2CU,0x3EU,0x7CU,0x5EU,0x3CU,0x24U,0x42U},
        {0x18U,0x3CU,0x2CU,0x3EU,0x7CU,0x5EU,0x3CU,0x42U,0x24U}
    };
    static const uint16_t crouching[2][5] = {
        {0x18U,0x3CU,0x2CU,0x7EU,0x5AU},
        {0x18U,0x3CU,0x2CU,0x7EU,0x66U}
    };
    if (shooter.crouch)
        GameUtil_DrawSprite(x, top, crouching[(shooter.anim_ms / 100U) & 1U], 8U, 5U);
    else
        GameUtil_DrawSprite(x, top, standing[(shooter.anim_ms / 100U) & 1U], 8U, 9U);
}
static void DrawShooterEnemy(int16_t x, int16_t y)
{
    static const uint16_t robot[2][6] = {
        {0x0CU,0x1EU,0x3FU,0x2DU,0x3FU,0x12U},
        {0x0CU,0x1EU,0x3FU,0x33U,0x3FU,0x24U}
    };
    GameUtil_DrawSprite(x, y, robot[(shooter.anim_ms / 120U) & 1U], 6U, 6U);
}
void GameShooter_Init(void) { ShooterReset(); }
void GameShooter_Enter(void) { ShooterReset(); }
void GameShooter_Update(uint32_t dt) { Advance(&shooter.clock,dt,ShooterStep); }
void GameShooter_Render(void)
{
    unsigned i; int h=shooter.crouch?5:9;
    if(!DisplayService_BeginFrame()) return;
    Header("SHOOTER",shooter.score);
    if(shooter.dead) GameOver();
    else {
        DisplayService_Line(0,30,127,30);
        DrawShooterPlayer(16,(int16_t)(Pixel(shooter.foot)-h));
        for(i=0;i<3;++i) DrawShooterEnemy(Pixel(shooter.enemies[i].x),24);
        for(i=0;i<4;++i) if(shooter.bullets[i].active)
            DisplayService_FillRect(Pixel(shooter.bullets[i].x),Pixel(shooter.bullets[i].y),3,1);
    }
    DisplayService_Present();
}
void GameShooter_Exit(void) {}

/* FISHING: cast -> wait -> hook -> balance line tension -> catch or lose. */
enum { CAST, WAIT, BITE, REEL, CAUGHT, LOST };
static struct {
    GameClock clock;
    uint32_t elapsed;
    int32_t progress,tension;
    uint16_t wait_ms,score;
    uint8_t state;
} fishing;
static void FishingReset(void) { memset(&fishing,0,sizeof(fishing)); }
static void FishingStep(uint8_t press)
{
    if(fishing.state==CAST) {
        if(press&A) { fishing.state=WAIT; fishing.elapsed=0; fishing.wait_ms=(uint16_t)(800+GameUtil_Random(1200)); }
        return;
    }
    fishing.elapsed+=10;
    if(fishing.state==WAIT) {
        if(press&A) fishing.state=LOST;
        else if(fishing.elapsed>=fishing.wait_ms) { fishing.state=BITE; fishing.elapsed=0; }
    } else if(fishing.state==BITE) {
        if(press&A) { fishing.state=REEL; fishing.elapsed=0; fishing.progress=0; fishing.tension=30*Q; }
        else if(fishing.elapsed>450) fishing.state=LOST;
    } else if(fishing.state==REEL) {
        uint8_t reel=(uint8_t)(InputService_IsDown(INPUT_UP) || (press&A));
        if(reel && !InputService_IsDown(INPUT_DOWN)) { fishing.progress+=Move(45); fishing.tension+=Move(35); }
        else { fishing.progress-=Move(2); fishing.tension-=Move(45); }
        if(fishing.elapsed%1000<250) fishing.tension+=Move(18);
        if(fishing.progress<0) fishing.progress=0;
        if(fishing.tension<0) fishing.tension=0;
        if(fishing.tension>=100*Q || fishing.elapsed>12000) fishing.state=LOST;
        else if(fishing.progress>=100*Q) { Score(&fishing.score); fishing.state=CAUGHT; }
    } else if(press&A) { fishing.state=CAST; fishing.elapsed=0; }
}
static void DrawFishingScene(void)
{
    static const uint16_t angler[10] = {
        0x18U,0x3CU,0x24U,0x3CU,0x18U,0x7EU,0x3CU,0x24U,0x24U,0x42U
    };
    static const uint16_t fish[5] = {
        0x18U,0x7EU,0xDBU,0x7EU,0x18U
    };
    static const uint16_t bobber[3] = {0x02U,0x07U,0x02U};
    int16_t fish_x = (int16_t)(14U + (fishing.elapsed / 120U) % 18U);

    GameUtil_DrawSprite(3, 17, angler, 8U, 10U);
    DisplayService_Line(10,18,26,9);
    DisplayService_Line(26,9,26,21);
    GameUtil_DrawSprite(24,21,bobber,3U,3U);
    DisplayService_Line(0,27,43,27);
    DisplayService_Line(0,30,6,30);
    DisplayService_Line(12,30,19,30);
    DisplayService_Line(26,30,34,30);
    GameUtil_DrawSprite(fish_x, 22, fish, 9U, 5U);
}
void GameFishing_Init(void) { FishingReset(); }
void GameFishing_Enter(void) { FishingReset(); }
void GameFishing_Update(uint32_t dt) { Advance(&fishing.clock,dt,FishingStep); }
void GameFishing_Render(void)
{
    int16_t tension, progress;
    if(!DisplayService_BeginFrame()) return;
    Header("FISH",fishing.score);
    DrawFishingScene();
    if(fishing.state==CAST) {
        DisplayService_Text(50,10,"A CAST");
        DisplayService_Text(50,19,"READY");
    } else if(fishing.state==WAIT) {
        DisplayService_Text(50,10,"WAIT...");
        DisplayService_Text(50,19,"WATCH");
    } else if(fishing.state==BITE) {
        DisplayService_Text(50,10,"BITE!");
        DisplayService_Text(50,19,"PRESS A");
    }
    else if(fishing.state==REEL) {
        tension = Pixel(fishing.tension);
        progress = Pixel(fishing.progress);
        if (tension > 66) tension = 66;
        if (progress > 66) progress = 66;
        DisplayService_Text(48,8,"REEL");
        DisplayService_Text(48,15,"T");
        DisplayService_Rect(56,15,70,5);
        DisplayService_FillRect(57,16,tension,3);
        DisplayService_Text(48,24,"P");
        DisplayService_Rect(56,24,70,5);
        DisplayService_FillRect(57,25,progress,3);
    } else {
        DisplayService_Text(50,10,fishing.state==CAUGHT?"CAUGHT":"LOST");
        DisplayService_Text(50,19,"A CAST");
    }
    DisplayService_Present();
}
void GameFishing_Exit(void) {}
#ifdef ARCADE_TEST
void Arcade_Debug(unsigned game, ArcadeDebug *out)
{
    SnakeDebug snake_debug;
    memset(out,0,sizeof(*out));
    if(game==0) {
        GameSnake_Debug(&snake_debug);
        out->x = snake_debug.cells[0] % SNAKE_COLS;
        out->y = snake_debug.cells[0] / SNAKE_COLS;
        out->velocity = snake_debug.length;
        out->dead = snake_debug.dead;
        out->score = snake_debug.score;
    }
    if(game==1) { out->y=plane.y; out->x=plane.enemies[0].x; out->dead=plane.dead; out->score=plane.score; }
    if(game==2) { out->y=racing.lane; out->x=racing.x[0]; out->dead=racing.dead; out->score=racing.score; }
    if(game==3) { out->y=flappy.y; out->velocity=flappy.velocity; out->x=flappy.pipe; out->dead=flappy.dead; out->score=flappy.score; }
    if(game==4) { out->y=shooter.foot; out->velocity=shooter.velocity; out->dead=shooter.dead; out->score=shooter.score; }
    if(game==5) { out->y=fishing.progress; out->x=fishing.tension; out->dead=fishing.state; out->score=fishing.score; }
}
#endif
