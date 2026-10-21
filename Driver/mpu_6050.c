#include "bsp_iic.h"
#include "mpu_6050.h"

#define MPU_IIC_ID    iic_1


/*-------------------------------------------------------
 * I2C写一个字节
 * reg : 寄存器地址
 * data: 写入数据
 * 返回值:
 * 0 成功
 * 1 失败
 *------------------------------------------------------*/
static uint8_t MPU_Write_Byte(uint8_t reg, uint8_t data)
{
    return (BSP_IIC_WriteReg(
                MPU_IIC_ID,
                MPU_ADDR,
                reg,
                data
            ) == HAL_OK) ? 0U : 1U;
}


/*-------------------------------------------------------
 * I2C读一个字节
 * reg: 寄存器地址
 * 返回值: 读取到的数据
 *------------------------------------------------------*/
static uint8_t MPU_Read_Byte(uint8_t reg)
{
    uint8_t res = 0xFFU;

    BSP_IIC_ReadReg(
        MPU_IIC_ID,
        MPU_ADDR,
        reg,
        &res
    );

    return res;
}


/*-------------------------------------------------------
 * I2C连续写
 * addr: 器件地址
 * reg : 起始寄存器地址
 * len : 写入长度
 * buf : 数据区
 * 返回值:
 * 0 成功
 * 1 失败
 *------------------------------------------------------*/
//static uint8_t MPU_Write_Len(
//    uint8_t addr,
//    uint8_t reg,
//    uint8_t len,
//    uint8_t *buf)
//{
//    return (BSP_IIC_WriteRegs(
//                MPU_IIC_ID,
//                addr,
//                reg,
//                buf,
//                len
//            ) == HAL_OK) ? 0U : 1U;
//}

/*-------------------------------------------------------
 * I2C连续读
 * addr: 器件地址
 * reg : 起始寄存器地址
 * len : 读取长度
 * buf : 数据存储区
 * 返回值:
 * 0 成功
 * 1 失败
 *------------------------------------------------------*/
static uint8_t MPU_Read_Len(
    uint8_t addr,
    uint8_t reg,
    uint8_t len,
    uint8_t *buf)
{
    return (BSP_IIC_ReadRegs(
                MPU_IIC_ID,
                addr,
                reg,
                buf,
                len
            ) == HAL_OK) ? 0U : 1U;
}

/*-------------------------------------------------------
 * 设置陀螺仪满量程
 * fsr:
 * 0 ±250dps
 * 1 ±500dps
 * 2 ±1000dps
 * 3 ±2000dps
 *------------------------------------------------------*/
static uint8_t MPU_Set_Gyro_Fsr(uint8_t fsr)
{
    return MPU_Write_Byte(
        MPU_GYRO_CFG_REG,
        fsr << 3
    );
}


/*-------------------------------------------------------
 * 设置加速度计满量程
 * fsr:
 * 0 ±2g
 * 1 ±4g
 * 2 ±8g
 * 3 ±16g
 *------------------------------------------------------*/
static uint8_t MPU_Set_Accel_Fsr(uint8_t fsr)
{
    return MPU_Write_Byte(
        MPU_ACCEL_CFG_REG,
        fsr << 3
    );
}


/*-------------------------------------------------------
 * 设置数字低通滤波器
 *------------------------------------------------------*/
static uint8_t MPU_Set_LPF(uint16_t lpf)
{
    uint8_t data = 0U;

    if(lpf >= 188U)
    {
        data = 1U;
    }
    else if(lpf >= 98U)
    {
        data = 2U;
    }
    else if(lpf >= 42U)
    {
        data = 3U;
    }
    else if(lpf >= 20U)
    {
        data = 4U;
    }
    else if(lpf >= 10U)
    {
        data = 5U;
    }
    else
    {
        data = 6U;
    }

    return MPU_Write_Byte(
        MPU_CFG_REG,
        data
    );
}


/*-------------------------------------------------------
 * 设置采样率
 * rate: 4~1000Hz
 *------------------------------------------------------*/
static uint8_t MPU_Set_Rate(uint16_t rate)
{
    uint8_t data;
    uint8_t res;

    if(rate > 1000U)
    {
        rate = 1000U;
    }

    if(rate < 4U)
    {
        rate = 4U;
    }

    data = (uint8_t)(1000U / rate - 1U);

    res = MPU_Write_Byte(
        MPU_SAMPLE_RATE_REG,
        data
    );

    if(res != 0U)
    {
        return res;
    }

    return MPU_Set_LPF(rate / 2U);
}


/*-------------------------------------------------------
 * 初始化 MPU6050
 * 返回:
 * 0 成功
 * 1 失败
 *------------------------------------------------------*/
uint8_t MPU_INIT(void)
{
    uint8_t res;

    /*
     * BSP始化中调用 BSP_I2C1_INIT()
     */
//    if(BSP_I2C1_INIT() != HAL_OK)
//    {
//        return 1U;
//    }

    /* 复位 MPU6050 */
    if(MPU_Write_Byte(
        MPU_PWR_MGMT1_REG,
        0X80) != 0U)
    {
        return 1U;
    }

    HAL_Delay(100);

    /* 唤醒 MPU6050 */
    if(MPU_Write_Byte(
        MPU_PWR_MGMT1_REG,
        0X00) != 0U)
    {
        return 1U;
    }

    /* 陀螺仪 ±2000dps */
    if(MPU_Set_Gyro_Fsr(3U) != 0U)
    {
        return 1U;
    }

    /* 加速度 ±2g */
    if(MPU_Set_Accel_Fsr(0U) != 0U)
    {
        return 1U;
    }

    /* 50Hz采样 */
    if(MPU_Set_Rate(50U) != 0U)
    {
        return 1U;
    }

    /* 关闭所有中断 */
    if(MPU_Write_Byte(
        MPU_INT_EN_REG,
        0X00) != 0U)
    {
        return 1U;
    }

    /* MPU6050自身I2C Master关闭 */
    if(MPU_Write_Byte(
        MPU_USER_CTRL_REG,
        0X00) != 0U)
    {
        return 1U;
    }

    /* 关闭FIFO */
    if(MPU_Write_Byte(
        MPU_FIFO_EN_REG,
        0X00) != 0U)
    {
        return 1U;
    }

    /* INT引脚配置 */
    if(MPU_Write_Byte(
        MPU_INTBP_CFG_REG,
        0X80) != 0U)
    {
        return 1U;
    }

    /* 读取WHO_AM_I */
    res = MPU_Read_Byte(
        MPU_DEVICE_ID_REG
    );

    if(res != MPU_ADDR)
    {
        return 1U;
    }

    /* PLL X轴陀螺仪作为时钟源 */
    if(MPU_Write_Byte(
        MPU_PWR_MGMT1_REG,
        0X01) != 0U)
    {
        return 1U;
    }

    /* 加速度计、陀螺仪全部开启 */
    if(MPU_Write_Byte(
        MPU_PWR_MGMT2_REG,
        0X00) != 0U)
    {
        return 1U;
    }

    /* 再次设置50Hz */
    if(MPU_Set_Rate(50U) != 0U)
    {
        return 1U;
    }

    return 0U;
}

/*-------------------------------------------------------
 * 获取温度
 * 返回值扩大100倍
 * 例如 3653 = 36.53℃
 *------------------------------------------------------*/
short MPU_Get_Temperature(void)
{
    uint8_t buf[2];
    short raw;
    float temp;

    if(MPU_Read_Len(
        MPU_ADDR,
        MPU_TEMP_OUTH_REG,
        2U,
        buf) != 0U)
    {
        return 0;
    }

    raw = (short)(
        ((uint16_t)buf[0] << 8) |
        buf[1]
    );

    temp = 36.53f +
           ((float)raw / 340.0f);

    return (short)(temp * 100.0f);
}


/*-------------------------------------------------------
 * 获取陀螺仪原始值
 *------------------------------------------------------*/
uint8_t MPU_Get_Gyroscope(
    short *gx,
    short *gy,
    short *gz)
{
    uint8_t buf[6];
    uint8_t res;

    res = MPU_Read_Len(
        MPU_ADDR,
        MPU_GYRO_XOUTH_REG,
        6U,
        buf
    );

    if(res == 0U)
    {
        *gx = (short)(
            ((uint16_t)buf[0] << 8) |
            buf[1]
        );

        *gy = (short)(
            ((uint16_t)buf[2] << 8) |
            buf[3]
        );

        *gz = (short)(
            ((uint16_t)buf[4] << 8) |
            buf[5]
        );
    }

    return res;
}


/*-------------------------------------------------------
 * 获取加速度计原始值
 *------------------------------------------------------*/
uint8_t MPU_Get_Accelerometer(
    short *ax,
    short *ay,
    short *az)
{
    uint8_t buf[6];
    uint8_t res;

    res = MPU_Read_Len(
        MPU_ADDR,
        MPU_ACCEL_XOUTH_REG,
        6U,
        buf
    );

    if(res == 0U)
    {
        *ax = (short)(
            ((uint16_t)buf[0] << 8) |
            buf[1]
        );

        *ay = (short)(
            ((uint16_t)buf[2] << 8) |
            buf[3]
        );

        *az = (short)(
            ((uint16_t)buf[4] << 8) |
            buf[5]
        );
    }

    return res;
}
