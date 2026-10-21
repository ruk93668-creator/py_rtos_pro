#include "key.h"
#include "bsp_gpio.h"

#include "FreeRTOS.h"
#include "task.h"

/*
 * ------------------------------------------------------------
 * AB 相状态
 *
 * bit0 = A
 * bit1 = B
 *
 * A B
 * -----
 * 0 0 -> 0
 * 1 0 -> 1
 * 0 1 -> 2
 * 1 1 -> 3
 * ------------------------------------------------------------
 */
static uint8_t s_previous = 0U;

/*
 * partial = 部分的、不完整的
 *
 * 用于累计一次完整旋转中的 AB 边沿。
 *
 * +4 -> 完整转一格
 * -4 -> 反方向完整转一格
 */
static int8_t s_partial = 0;

/*
 * 已经完整旋转的格数
 *
 * 在 EXTI 中修改，
 * 在任务中通过 KEY_TakeRotation() 取出。
 */
static volatile int8_t s_turns = 0;


/*
 * ------------------------------------------------------------
 * AB 相状态转换表
 *
 * previous：上一次 AB
 * phase   ：当前 AB
 *
 * 下标：
 *
 * (previous << 2) | phase
 *
 * 4种旧状态 × 4种新状态 = 16
 * ------------------------------------------------------------
 */
static const int8_t s_delta[16] =
{
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};


/*
 * ------------------------------------------------------------
 * 读取当前 AB 两相
 *
 * 注意：
 *
 * Driver 不知道 PA3 / PA5。
 *
 * Driver 只知道：
 *
 * gpioa = A相
 * gpiob = B相
 *
 * PA3 / PA5 属于 BSP 的事情。
 * ------------------------------------------------------------
 */
static uint8_t KEY_ReadPhase(void)
{
    uint8_t phase = 0U;

    /*
     * A 相放到 bit0
     */
    if (BSP_GPIO_READ(gpioa))
    {
        phase |= 0x01U;
    }

    /*
     * B 相放到 bit1
     */
    if (BSP_GPIO_READ(gpiob))
    {
        phase |= 0x02U;
    }

    return phase;
}


/*
 * ------------------------------------------------------------
 * AB 相处理
 *
 * 每次 A 或 B 发生上升沿 / 下降沿时调用一次。
 * ------------------------------------------------------------
 */
static void KEY_AB_Process(void)
{
    uint8_t phase;
    uint8_t index;

    /*
     * 获取当前 AB 状态
     */
    phase = KEY_ReadPhase();


    /*
     * 检查非法跳变。
     *
     * ^ 是 XOR。
     *
     * 例如：
     *
     * 00 ^ 11 = 11 = 3
     * 01 ^ 10 = 11 = 3
     *
     * 说明 A、B 两位同时发生变化。
     *
     * 正常正交编码器一次应该只变化一位。
     */
    if ((s_previous ^ phase) == 3U)
    {
        /*
         * 发生异常跳变，
         * 放弃之前没有完成的一格。
         */
        s_partial = 0;
    }
    else
    {
        /*
         * 把：
         *
         * previous previous phase phase
         *
         * 拼成一个 4bit 下标。
         */
        index = (uint8_t)((s_previous << 2) | phase);

        /*
         * 根据状态转换获得：
         *
         * +1
         * -1
         *  0
         */
        s_partial += s_delta[index];
    }


    /*
     * 当前状态保存为下一次的 previous
     */
    s_previous = phase;


    /*
     * 一个完整周期：
     *
     * 00
     * ↓
     * 01
     * ↓
     * 11
     * ↓
     * 10
     * ↓
     * 00
     *
     * 一共4次有效变化。
     */
    if (s_partial >= 4)
    {
        if (s_turns < 10)
        {
            s_turns++;
        }

        s_partial = 0;
    }
    else if (s_partial <= -4)
    {
        if (s_turns > -10)
        {
            s_turns--;
        }

        s_partial = 0;
    }
}


/*
 * ------------------------------------------------------------
 * 初始化
 * ------------------------------------------------------------
 */
void KEY_Init(void)
{
    /*
     * 必须读取编码器当前真实状态。
     *
     * 不能默认：
     *
     * s_previous = 0
     *
     * 因为上电时 AB 不一定刚好是 00。
     */
    s_previous = KEY_ReadPhase();

    s_partial = 0;
    s_turns = 0;
}


/*
 * ------------------------------------------------------------
 * 取出已经累计的旋转格数
 * ------------------------------------------------------------
 */
int8_t KEY_TakeRotation(void)
{
    int8_t turns;

    /*
     * s_turns 会在 EXTI 中改变。
     *
     * 这里：
     *
     * 读取
     * +
     * 清零
     *
     * 必须作为一个完整操作。
     */
    taskENTER_CRITICAL();

    turns = s_turns;
    s_turns = 0;

    taskEXIT_CRITICAL();

    return turns;
}


/*
 * ------------------------------------------------------------
 * 获取 S 按键状态
 * ------------------------------------------------------------
 */
bool KEY_IsPressed(void)
{
    return !BSP_GPIO_READ(gpios);
}


/*
 * ------------------------------------------------------------
 * HAL EXTI 回调
 *
 * 注意：
 * GPIO读取仍然全部通过 BSP。
 *
 * GPIO_Pin 在这里只用于判断：
 * 到底是哪一条 EXTI 触发。
 * ------------------------------------------------------------
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    /*
     * PA3 = A
     */
    if (GPIO_Pin == GPIO_PIN_3)
    {
        KEY_AB_Process();
    }

    /*
     * PA5 = B
     */
    else if (GPIO_Pin == GPIO_PIN_5)
    {
        KEY_AB_Process();
    }

    /*
     * PA4 = S
     *
     * 这里不需要做任何事情。
     *
     * 因为 KEY_IsPressed()
     * 会直接通过 BSP_GPIO_READ(gpios)
     * 获取当前状态。
     */
}
