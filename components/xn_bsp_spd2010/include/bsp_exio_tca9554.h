/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-28 15:16:27
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-08-28 15:32:51
 * @FilePath: \esp-chunfeng\main\LCD_Driver\Display_SPD2010.c
 * @Description: TCA9554PWR I2C扩展IO芯片驱动头文件，包含寄存器定义、宏定义及相关函数声明
 */

#pragma once

#include <stdio.h>
#include "bsp_i2c_driver.h"

// TCA9554PWR扩展IO引脚编号宏定义（1~8号引脚，注意与实际芯片引脚对应关系）
#define TCA9554_EXIO1 0x01   // EXIO1编号
#define TCA9554_EXIO2 0x02   // EXIO2编号
#define TCA9554_EXIO3 0x03   // EXIO3编号
#define TCA9554_EXIO4 0x04   // EXIO4编号
#define TCA9554_EXIO5 0x05   // EXIO5编号
#define TCA9554_EXIO6 0x06   // EXIO6编号
#define TCA9554_EXIO7 0x07   // EXIO7编号
#define TCA9554_EXIO8 0x08   // EXIO8编号

/******************************************************
 * TCA9554PWR芯片相关宏定义及寄存器地址
 ******************************************************/

#define TCA9554_ADDRESS             0x20    // TCA9554PWR I2C器件地址（7位地址，默认0x20）

// TCA9554PWR寄存器地址定义
#define TCA9554_INPUT_REG           0x00    // 输入寄存器，读取各引脚输入电平
#define TCA9554_OUTPUT_REG          0x01    // 输出寄存器，设置各引脚输出高低电平
#define TCA9554_Polarity_REG        0x02    // 极性反转寄存器，仅对输入引脚有效
#define TCA9554_CONFIG_REG          0x03    // 配置寄存器，设置引脚为输入/输出模式（0=输出，1=输入）

/******************************************************
 * TCA9554PWR操作相关函数声明
 ******************************************************/

/**
 * @brief 读取TCA9554PWR指定寄存器的值
 * @param REG 寄存器地址（TCA9554_INPUT_REG等）
 * @return 读取到的8位寄存器值
 */
uint8_t Read_REG(uint8_t REG);

/**
 * @brief 向TCA9554PWR指定寄存器写入数据
 * @param REG  寄存器地址
 * @param Data 待写入的数据
 */
void Write_REG(uint8_t REG, uint8_t Data);

/**
 * @brief 设置单个EXIO引脚的输入/输出模式
 * @param Pin   EXIO引脚编号（TCA9554_EXIO1~TCA9554_EXIO8）
 * @param State 模式选择，0=输出模式，1=输入模式
 * @note 仅设置指定引脚，其余引脚模式不变
 */
void Mode_EXIO(uint8_t Pin, uint8_t State);

/**
 * @brief 批量设置所有EXIO引脚的输入/输出模式
 * @param PinState 8位模式值，每一位对应一个引脚，0=输出，1=输入
 * @note 例如PinState=0x23，表示EXIO1/EXIO2/EXIO6为输入，其余为输出
 */
void Mode_EXIOS(uint8_t PinState);

/**
 * @brief 读取指定EXIO引脚的电平状态
 * @param Pin EXIO引脚编号（TCA9554_EXIO1~TCA9554_EXIO8）
 * @return 该引脚当前电平（0/1）
 */
uint8_t Read_EXIO(uint8_t Pin);

/**
 * @brief 读取所有EXIO引脚的电平状态
 * @return 8位输入寄存器值，每一位对应一个引脚的输入电平
 * @note 默认读取输入寄存器（TCA9554_INPUT_REG），如需读取输出状态请直接调用Read_REG(TCA9554_OUTPUT_REG)
 */
uint8_t Read_EXIOS(void);

/**
 * @brief 设置单个EXIO引脚的输出电平
 * @param Pin   EXIO引脚编号（TCA9554_EXIO1~TCA9554_EXIO8）
 * @param State 输出电平，true=高电平，false=低电平
 * @note 仅改变指定引脚的输出电平，其余引脚状态不变
 */
void Set_EXIO(uint8_t Pin, bool State);

/**
 * @brief 批量设置所有EXIO引脚的输出电平
 * @param PinState 8位输出值，每一位对应一个引脚的输出电平
 * @note 例如PinState=0x23，表示EXIO1/EXIO2/EXIO6输出高电平，其余为低电平
 */
void Set_EXIOS(uint8_t PinState);

/**
 * @brief 翻转指定EXIO引脚的输出电平
 * @param Pin EXIO引脚编号（TCA9554_EXIO1~TCA9554_EXIO8）
 * @note 仅对配置为输出模式的引脚有效
 */
void Set_Toggle(uint8_t Pin);

/**
 * @brief 初始化TCA9554PWR芯片所有引脚的输入/输出模式
 * @param PinState 8位模式值，每一位对应一个引脚，0=输出，1=输入
 * @note 例如PinState=0x23，表示EXIO1/EXIO2/EXIO6为输入，其余为输出。默认全部为输出模式
 */
void TCA9554PWR_Init(uint8_t PinState);

/**
 * @brief TCA9554PWR扩展IO初始化（推荐调用）
 * @return ESP_OK表示初始化成功，其他为错误码
 */
esp_err_t EXIO_Init(void);
