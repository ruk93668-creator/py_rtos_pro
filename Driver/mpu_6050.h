#ifndef MPU6050_H
#define MPU6050_H

#include <stdint.h>
#include <stdbool.h>

#include "py32f0xx_hal.h"

#define MPU_SAMPLE_RATE_REG      0X19   // 采样率分频寄存器，用于设置陀螺仪输出采样率
#define MPU_CFG_REG              0X1A   // 配置寄存器，用于设置数字低通滤波器 DLPF
#define MPU_GYRO_CFG_REG         0X1B   // 陀螺仪配置寄存器，用于设置陀螺仪满量程范围
#define MPU_ACCEL_CFG_REG        0X1C   // 加速度计配置寄存器，用于设置加速度计满量程范围

#define MPU_FIFO_EN_REG          0X23   // FIFO 使能寄存器，用于控制哪些传感器数据写入 FIFO

#define MPU_INTBP_CFG_REG        0X37   // 中断引脚/旁路配置寄存器
#define MPU_INT_EN_REG           0X38   // 中断使能寄存器，用于开启或关闭各类中断

#define MPU_ACCEL_XOUTH_REG      0X3B   // X轴加速度高8位寄存器
#define MPU_ACCEL_XOUTL_REG      0X3C   // X轴加速度低8位寄存器
#define MPU_ACCEL_YOUTH_REG      0X3D   // Y轴加速度高8位寄存器
#define MPU_ACCEL_YOUTL_REG      0X3E   // Y轴加速度低8位寄存器
#define MPU_ACCEL_ZOUTH_REG      0X3F   // Z轴加速度高8位寄存器
#define MPU_ACCEL_ZOUTL_REG      0X40   // Z轴加速度低8位寄存器

#define MPU_TEMP_OUTH_REG        0X41   // 温度数据高8位寄存器
#define MPU_TEMP_OUTL_REG        0X42   // 温度数据低8位寄存器

#define MPU_GYRO_XOUTH_REG       0X43   // X轴陀螺仪数据高8位寄存器
#define MPU_GYRO_XOUTL_REG       0X44   // X轴陀螺仪数据低8位寄存器
#define MPU_GYRO_YOUTH_REG       0X45   // Y轴陀螺仪数据高8位寄存器
#define MPU_GYRO_YOUTL_REG       0X46   // Y轴陀螺仪数据低8位寄存器
#define MPU_GYRO_ZOUTH_REG       0X47   // Z轴陀螺仪数据高8位寄存器
#define MPU_GYRO_ZOUTL_REG       0X48   // Z轴陀螺仪数据低8位寄存器

#define MPU_USER_CTRL_REG        0X6A   // 用户控制寄存器，用于控制 FIFO、I2C 主机等功能
#define MPU_PWR_MGMT1_REG        0X6B   // 电源管理寄存器1，用于复位、休眠和时钟源选择
#define MPU_PWR_MGMT2_REG        0X6C   // 电源管理寄存器2，用于控制各轴加速度计和陀螺仪工作状态

#define MPU_DEVICE_ID_REG        0X75   // 器件ID寄存器 WHO_AM_I，用于确认 MPU6050 是否通信正常

#define MPU_ADDR								 0X68




uint8_t MPU_INIT(void);
/*-------------------------------------------------------
 * 获取加速度计原始值
 *------------------------------------------------------*/
uint8_t MPU_Get_Accelerometer(
    short *ax,
    short *ay,
    short *az);
/*-------------------------------------------------------
 * 获取陀螺仪原始值
 *------------------------------------------------------*/
uint8_t MPU_Get_Gyroscope(
    short *gx,
    short *gy,
    short *gz);
/*-------------------------------------------------------
 * 获取温度
 * 返回值扩大100倍
 * 例如 3653 = 36.53℃
 *------------------------------------------------------*/
short MPU_Get_Temperature(void);
#endif
