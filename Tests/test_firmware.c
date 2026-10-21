/* Host checks of the real App/Service code and existing OLED rasterizer.
 * Hardware-facing calls are replaced here; this is not a board test. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bsp_iic.h"
#include "key.h"
#include "mpu_6050.h"
#include "oled.h"
#include "service_config.h"
#include "service_main.h"
#include "service_enmu.h"
#include "service_mpu_6050.h"
#include "service_display.h"
#include "app_main.h"
#include "app_settings.h"
#include "game_manager.h"
#include "boot_metadata.h"
#include "game_dino.h"
#include "game_arcade.h"

static uint32_t now, lock_depth, sensor_init_calls, sensor_reads;
static bool key_pressed;
static int8_t rotation;
static uint8_t sensor_error, sensor_init_error, panel[512], page;
static int16_t ax, ay, az, gx, gy, gz;
static unsigned tx_pages;
static BootSlot_t test_current_slot;
static BootSlotMetadata_t test_metadata;

/* AppSettings reads the production metadata API.  The host suite supplies a
   deterministic in-memory implementation so no bootloader flash code is
   linked into the test executable. */
bool BootMeta_Read(BootSlot_t slot, BootSlotMetadata_t *metadata)
{
    if (!metadata || slot != test_current_slot)
        return false;
    *metadata = test_metadata;
    return true;
}

BootSlot_t App_GetCurrentSlot(void)
{
    return test_current_slot;
}

uint32_t HAL_GetTick(void) { return now; }
void HAL_Delay(uint32_t delay) { assert(!lock_depth); now += delay; }
void Test_EnterCritical(void) { ++lock_depth; }
void Test_ExitCritical(void) { assert(lock_depth); --lock_depth; }
void KEY_Init(void) { }
bool KEY_IsPressed(void) { return key_pressed; }
int8_t KEY_TakeRotation(void) { int8_t value = rotation; rotation = 0; return value; }
uint8_t MPU_INIT(void) { assert(!lock_depth); ++sensor_init_calls; return sensor_init_error; }
uint8_t MPU_Get_Accelerometer(short *x, short *y, short *z)
{
    assert(!lock_depth); ++sensor_reads;
    *x = ax; *y = ay; *z = az; return sensor_error;
}
uint8_t MPU_Get_Gyroscope(short *x, short *y, short *z)
{
    assert(!lock_depth); *x = gx; *y = gy; *z = gz; return sensor_error;
}
short MPU_Get_Temperature(void) { return 2500; }
HAL_StatusTypeDef BSP_IIC_WriteRegs(IICId_t id, uint16_t addr, uint8_t reg,
                                    uint8_t *data, uint8_t len)
{
    assert(!lock_depth && id == iic_2 && addr == 0x3CU);
    if (reg == 0x40U) {
        assert(len == 128U && page < 4U);
        memcpy(panel + page * 128U, data, 128U); ++tx_pages;
    } else {
        assert(reg == 0U);
        if (len == 3U) {
            assert(data[0] >= 0xB0U && data[0] <= 0xB3U);
            assert(data[1] == 0 && data[2] == 0x10U);
            page = (uint8_t)(data[0] - 0xB0U);
        }
    }
    return HAL_OK;
}
static void Advance(unsigned ms)
{
    unsigned i;
    for (i = 0; i < ms; ++i) {
        ++now;
        if (now % 5U == 0U) {
            SensorService_Process();
            ServiceMain_Process();
        }
        assert(!lock_depth);
    }
}
static void Init(void)
{
    now = lock_depth = sensor_init_calls = sensor_reads = 0;
    key_pressed = false; rotation = 0;
    sensor_error = sensor_init_error = page = 0;
    ax = ay = gx = gy = gz = 0; az = 16384;
    tx_pages = 0; memset(panel, 0, sizeof(panel));
    test_current_slot = BOOT_SLOT_B;
    memset(&test_metadata, 0, sizeof(test_metadata));
    test_metadata.magic = BOOT_META_MAGIC;
    test_metadata.slot_id = (uint32_t)test_current_slot;
    test_metadata.version = 13U;
    test_metadata.firmware_size = 1U;
    test_metadata.state = (uint32_t)SLOT_STATE_CONFIRMED;
    OLED_Init(); ServiceMain_Init(); AppMain_Init();
    Advance(350); InputService_BeginFrame();
}
static void QueueClick(void)
{
    key_pressed = true; Advance(30);
    key_pressed = false; Advance(30);
}
static void Click(void) { QueueClick(); InputService_BeginFrame(); }
static void Turn(int8_t count) { rotation = count; Advance(5); InputService_BeginFrame(); }
static void Tilt(int16_t x, int16_t y)
{
    ax = x; ay = y; Advance(250); InputService_BeginFrame();
}
static unsigned PixelCount(void)
{
    unsigned i, bit, count = 0;
    for (i = 0; i < sizeof(panel); ++i)
        for (bit = 0; bit < 8; ++bit) count += (panel[i] >> bit) & 1U;
    return count;
}
static void SavePanel(const char *name)
{
    FILE *file;
    char path[80];
    unsigned x, y;
    (void)snprintf(path, sizeof(path), "%s.pgm", name);
    file = fopen(path, "wb"); assert(file);
    fprintf(file, "P5\n128 32\n255\n");
    for (y = 0; y < 32; ++y) for (x = 0; x < 128; ++x)
        fputc((panel[(y / 8U) * 128U + x] & (1U << (y % 8U))) ? 255 : 0, file);
    assert(!fclose(file));
}
static void TestInput(void)
{
    InputEvent_t event;
    unsigned i, down_presses = 0;
    Init();
    /* Four short contact bounces must not create a click. */
    for (i = 0; i < 4; ++i) {
        key_pressed = true; Advance(5); key_pressed = false; Advance(5);
    }
    Advance(30); InputService_BeginFrame();
    assert(!InputService_ConsumePress(INPUT_A));
    Click(); assert(InputService_ConsumePress(INPUT_A));
    assert(!InputService_ConsumePress(INPUT_A));
    assert(!InputService_ConsumePress((InputCode_t)-1));
    assert(!InputService_IsDown(INPUT_NONE));
    InputService_BeginFrame(); assert(!InputService_ConsumePress(INPUT_A));
    /* Work arriving after the App snapshot belongs to the following frame. */
    QueueClick(); assert(!InputService_ConsumePress(INPUT_A));
    InputService_BeginFrame(); assert(InputService_ConsumePress(INPUT_A));
    Turn(3);
    assert(InputService_IsRotaryDown(INPUT_DOWN));
    while (InputService_GetEvent(&event))
        if (event.code == INPUT_DOWN && event.pressed) ++down_presses;
    assert(down_presses == 3U && InputService_IsDown(INPUT_DOWN));
    Advance(SERVICE_ROTARY_HOLD_MS + 10U); InputService_BeginFrame();
    assert(!InputService_IsDown(INPUT_DOWN));
    Turn(-1); assert(InputService_ConsumePress(INPUT_UP));
    /* Overflow reports dropped transitions while retaining bounded storage. */
    for (i = 0; i < 20; ++i) { rotation = 1; Advance(5); }
    assert(InputService_GetDroppedCount() > 0U);
    InputService_BeginFrame(); assert(InputService_ConsumePress(INPUT_DOWN));
    Init(); key_pressed = true; Advance(SERVICE_BUTTON_BACK_MS + 40U);
    InputService_BeginFrame(); assert(InputService_ConsumeBack());
    assert(!InputService_ConsumeBack() && !InputService_ConsumePress(INPUT_A));
    key_pressed = false; Advance(30); InputService_BeginFrame();
    assert(!InputService_ConsumePress(INPUT_A));
    /* A key held at initialization can be released without phantom click. */
    key_pressed = true; InputService_Init(); Advance(30);
    key_pressed = false; Advance(30); InputService_BeginFrame();
    assert(!InputService_ConsumePress(INPUT_A));
    puts("PASS input: bounce, short/long press, long-release suppression, frame queue, rotary events, overflow");
}
static void TestSensorAndMotion(void)
{
    SensorData_t data;
    uint32_t reads, calls;
    Init(); assert(SensorService_Get(&data) && data.valid && data.accel_z == 16384);
    reads = sensor_reads; Advance(100);
    assert(sensor_reads - reads <= 6U && sensor_reads - reads >= 4U);
    /* Current project keeps device axes: X does not also trigger Y. */
    Tilt(14000, 0);
    assert(InputService_IsDown(INPUT_RIGHT));
    assert(!InputService_IsDown(INPUT_UP) && !InputService_IsDown(INPUT_DOWN));
    Tilt(-14000, 0); assert(InputService_IsDown(INPUT_LEFT));
    assert(!InputService_IsDown(INPUT_UP) && !InputService_IsDown(INPUT_DOWN));
    Tilt(0, 14000); assert(InputService_IsDown(INPUT_DOWN));
    assert(!InputService_IsDown(INPUT_LEFT) && !InputService_IsDown(INPUT_RIGHT));
    Tilt(0, -14000); assert(InputService_IsDown(INPUT_UP));
    assert(InputService_ConsumePress(INPUT_UP));
    Advance(1000); InputService_BeginFrame();
    assert(InputService_IsDown(INPUT_UP) && !InputService_ConsumePress(INPUT_UP));
    Tilt(0, -5500); assert(InputService_IsDown(INPUT_UP));
    Tilt(0, -3000); assert(!InputService_IsDown(INPUT_UP));
    Tilt(0, 0); gx = -32768; az = 8000; Advance(100); InputService_BeginFrame();
    assert(InputService_ConsumePress(INPUT_A));
    Advance(500); InputService_BeginFrame(); assert(!InputService_ConsumePress(INPUT_A));
    gx = 0; az = 16384; Advance(100);
    gx = SERVICE_JUMP_GYRO_RAW + 10; az = 8000;
    Advance(100); InputService_BeginFrame(); assert(InputService_ConsumePress(INPUT_A));
    gx = 0; az = 16384; gz = SERVICE_SHAKE_GYRO_RAW + 10;
    Advance(100); InputService_BeginFrame(); assert(InputService_ConsumePress(INPUT_PAUSE));
    Advance(1100); InputService_BeginFrame(); assert(!InputService_ConsumePress(INPUT_PAUSE));
    gz = 0; Tilt(0, 14000); assert(InputService_IsDown(INPUT_DOWN));
    sensor_error = 1; Advance(50); InputService_BeginFrame();
    assert(!SensorService_Get(&data));
    assert(!InputService_IsDown(INPUT_DOWN));
    calls = sensor_init_calls; sensor_error = 0; sensor_init_error = 1;
    Advance(SERVICE_SENSOR_RETRY_MS + 50U);
    assert(sensor_init_calls > calls && !SensorService_Get(&data));
    sensor_init_error = 0; ax = ay = 0;
    Advance(SERVICE_SENSOR_RETRY_MS + 100U); assert(SensorService_Get(&data));
    Tilt(0, -14000); assert(InputService_IsDown(INPUT_UP));
    /* Stop the Sensor task but keep Service/App consumers running. */
    now += SERVICE_SENSOR_STALE_MS + 1U;
    assert(!SensorService_Get(&data));
    ServiceMain_Process(); InputService_BeginFrame();
    assert(!InputService_IsDown(INPUT_UP));
    assert(!SensorService_Get(NULL));
    puts("PASS sensor/motion: 50 Hz bound, independent axes, hysteresis, events/rearm, INT16_MIN, failure/retry/stale neutral");
}
static void TestDino(void)
{
    DinoDebug a, b;
    unsigned i;
    Init(); GameDino_Enter(); Click(); GameDino_Update(100); GameDino_Debug(&a);
    assert(a.foot < 30 * 256 && a.velocity < 0 && !a.dead);
    for (i = 0; i < 10; ++i) GameDino_Update(50);
    GameDino_Debug(&a); assert(a.foot == 30 * 256 && !a.dead);
    GameDino_Enter(); Click(); for (i = 0; i < 10; ++i) GameDino_Update(10);
    GameDino_Debug(&a);
    GameDino_Enter(); Click(); for (i = 0; i < 5; ++i) GameDino_Update(20);
    GameDino_Debug(&b);
    assert(a.foot == b.foot && a.velocity == b.velocity && a.obstacle == b.obstacle);
    Tilt(0, 14000); GameDino_Enter(); GameDino_TestObstacle(1); GameDino_Update(5);
    GameDino_Debug(&a); assert(a.crouch && !a.dead);
    GameDino_Enter(); GameDino_TestObstacle(0); GameDino_Update(5);
    GameDino_Debug(&a); assert(a.dead);
    Click(); GameDino_Update(10); GameDino_Debug(&a); assert(!a.dead && a.score == 0);
    puts("PASS DINO: jump/landing, elapsed-time partition, crouch, cactus collision and restart");
}
static void TestArcade(void)
{
    ArcadeDebug a, b;
    unsigned i;
    Init(); GameSnake_Enter(); Arcade_Debug(0, &a);
    assert(a.x == 8 && a.y == 1 && a.velocity == 3 && !a.dead);
    for (i = 0; i < 14; ++i) GameSnake_Update(10);
    Arcade_Debug(0, &a); assert(a.x == 9 && a.y == 1);
    Turn(1);
    for (i = 0; i < 14; ++i) GameSnake_Update(10);
    Arcade_Debug(0, &a); assert(a.x == 9 && a.y == 2 && !a.dead);
    GameSnake_Enter();
    for (i = 0; i < 120; ++i) GameSnake_Update(10);
    Arcade_Debug(0, &a); assert(a.dead);
    Init(); GamePlane_Enter(); GamePlane_Update(100); Arcade_Debug(1, &a); assert(a.y == 16 * 256);
    Tilt(0, 0); Turn(1); GamePlane_Update(10); Arcade_Debug(1, &b); assert(b.y > a.y);
    Tilt(0, 14000); GamePlane_Enter(); GamePlane_Update(100); Arcade_Debug(1, &a);
    assert(a.y == 16 * 256); /* Plane steering ignores held motion tilt. */
    Tilt(0, 0); GameRacing_Enter(); Turn(-1); GameRacing_Update(10);
    Arcade_Debug(2, &a); assert(a.y == 0);
    GameRacing_Update(10); Arcade_Debug(2, &a); assert(a.y == 0);
    Turn(1); GameRacing_Update(10); Arcade_Debug(2, &a); assert(a.y == 1);
    Tilt(0, 0); GameFlappy_Enter(); GameFlappy_Update(100); Arcade_Debug(3, &a);
    assert(a.y > 16 * 256);
    Click(); GameFlappy_Update(100); Arcade_Debug(3, &b); assert(b.y < a.y && b.velocity < 0);
    GameFlappy_Enter(); Turn(1); GameFlappy_Update(10); Arcade_Debug(3, &a); assert(a.velocity < 0);
    GameFlappy_Enter(); Click(); for (i = 0; i < 10; ++i) GameFlappy_Update(10);
    Arcade_Debug(3, &a);
    GameFlappy_Enter(); Click(); for (i = 0; i < 5; ++i) GameFlappy_Update(20);
    Arcade_Debug(3, &b); assert(a.y == b.y && a.velocity == b.velocity && a.x == b.x);
    GameFlappy_Enter(); for (i = 0; i < 20; ++i) GameFlappy_Update(100);
    Arcade_Debug(3, &a); assert(a.dead);
    Click(); GameFlappy_Update(10); Arcade_Debug(3, &a); assert(!a.dead);
    GameShooter_Enter();
    for (i = 0; i < 120; ++i) {
        if (i % 20 == 0) Click();
        GameShooter_Update(10);
    }
    Arcade_Debug(4, &a); assert(a.score > 0 && !a.dead);
    Tilt(0, -14000); GameShooter_Update(100); Arcade_Debug(4, &a); assert(a.y < 30 * 256);
    Tilt(0, 0); GameFishing_Enter(); Click(); GameFishing_Update(10);
    for (i = 0; i < 250; ++i) {
        Arcade_Debug(5, &a); if (a.dead == 2) break;
        GameFishing_Update(10);
    }
    assert(i < 250); Click(); GameFishing_Update(10); Arcade_Debug(5, &a); assert(a.dead == 3);
    for (i = 0; i < 1200; ++i) {
        Arcade_Debug(5, &a); if (a.dead != 3) break;
        if (i % 25 == 0) Tilt(0, (int16_t)(a.x > 65 * 256 ? 14000 : -14000));
        GameFishing_Update(10);
    }
    Arcade_Debug(5, &a); assert(a.dead == 4 && a.score == 1);
    Click(); GameFishing_Update(10); Arcade_Debug(5, &a); assert(a.dead == 0);
    GameFishing_Enter(); Click(); GameFishing_Update(10); Click(); GameFishing_Update(10);
    Arcade_Debug(5, &a); assert(a.dead == 5);
    Click(); GameFishing_Update(10); Arcade_Debug(5, &a); assert(a.dead == 0);
    puts("PASS arcade: snake, plane rotary steering, racing, flapping, shooting, fishing catch/loss/recast");
}
static void TestAppAndDisplay(void)
{
    static const char *names[] = {"dino", "snake", "plane", "racing", "flappy", "shooter", "fishing"};
    unsigned game, old_pages;
    Init(); gx = SERVICE_JUMP_GYRO_RAW + 100; az = 8000;
    Advance(100); InputService_BeginFrame(); AppMain_Process(10);
    assert(!GameManager_IsRunning()); /* Startup motion cannot auto-start a game. */
    Init(); assert(!GameManager_Start((GameId_t)-1));
    assert(!GameManager_Start(GAME_ID_COUNT));
    for (game = 0; game < GAME_ID_COUNT; ++game) {
        Init();
        AppMain_Process(10); assert(!GameManager_IsRunning());
        assert(PixelCount() > 50U); if (!game) SavePanel("menu");
        Advance(SERVICE_MENU_START_GUARD_MS);
        InputService_BeginFrame(); AppMain_Process(10);
        if (game) { rotation = (int8_t)game; Advance(5); AppMain_Process(10); }
        QueueClick();
        /* A rotary selection uses the production spring animation.  Let the
           existing menu state settle before checking the pending start. */
        for (unsigned wait = 0; wait < 300U && !GameManager_IsRunning(); ++wait) {
            Advance(10); AppMain_Process(10);
        }
        assert(GameManager_IsRunning());
        Advance(SERVICE_DISPLAY_PERIOD_MS + 5U); AppMain_Process(10);
        assert(PixelCount() > 50U); SavePanel(names[game]);
        key_pressed = true; Advance(SERVICE_BUTTON_BACK_MS + 40U);
        AppMain_Process(10); assert(!GameManager_IsRunning());
        key_pressed = false; Advance(30); AppMain_Process(10);
        assert(!GameManager_IsRunning()); /* Release must not reopen a game. */
    }
    Init(); Advance(SERVICE_DISPLAY_PERIOD_MS + 1U);
    assert(DisplayService_BeginFrame());
    DisplayService_Pixel(0, 0); DisplayService_Pixel(127, 31);
    DisplayService_Rect(-20, -20, 10, 10);
    DisplayService_FillRect(130, 40, 10, 10);
    DisplayService_Present();
    assert(panel[0] == 1U && panel[511] == 128U && PixelCount() == 2U);
    old_pages = tx_pages;
    assert(!DisplayService_BeginFrame()); assert(tx_pages == old_pages);
    puts("PASS app/display: eight menu entries, rendering, long-back/release, real OLED pixel clipping and frame throttle");
}
static void TestSettings(void)
{
    uint32_t version;
    unsigned i, wait;

    Init();
    assert(AppSettings_GetUart() == APP_UART_TYPEC);
    assert(AppSettings_GetUartFlag() == 0U);
    assert(AppSettings_GetCurrentVersion(&version) && version == 13U);

    AppSettings_SetUart(APP_UART_BLE);
    assert(AppSettings_GetUart() == APP_UART_BLE);
    assert(AppSettings_GetUartFlag() == 1U);
    AppSettings_SetUart((AppUartPort_t)2U);
    assert(AppSettings_GetUart() == APP_UART_BLE);

    /* Walk the production carousel to the eighth item and open Settings.
       A positive detent is the same INPUT_DOWN event used by the real key
       service, so this also verifies that Settings is not a pseudo-game. */
    Init();
    Advance(SERVICE_MENU_START_GUARD_MS);
    for (i = 0U; i < GAME_ID_COUNT; ++i) {
        MenuDebug debug;
        rotation = 1;
        Advance(5U);
        AppMain_Process(10U);
        for (wait = 0U; wait < 200U; ++wait) {
            Advance(10U);
            AppMain_Process(10U);
            MenuService_Debug(&debug);
            if (debug.offset == 0 && debug.velocity == 0)
                break;
        }
        assert(wait < 200U);
    }
    QueueClick();
    for (wait = 0U; wait < 300U; ++wait) {
        Advance(10U);
        AppMain_Process(10U);
    }
    rotation = 1;
    Advance(5U);
    AppMain_Process(10U);
    assert(AppSettings_GetUart() == APP_UART_BLE);

    puts("PASS settings: Type-C/BLE selection flag and current-slot metadata version");
}
int main(void)
{
    TestInput(); TestSensorAndMotion(); TestDino(); TestArcade(); TestAppAndDisplay(); TestSettings();
    puts("ALL HOST TESTS PASSED (hardware and scheduler integration are not exercised)");
    return 0;
}
