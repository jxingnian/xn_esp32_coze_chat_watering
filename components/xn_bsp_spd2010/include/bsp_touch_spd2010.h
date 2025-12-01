/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-30 16:30:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-25 16:47:42
 * @FilePath: \xn_esp32_lottie\components\xn_lottie\include\xn_lottie_touch_spd2010.h
 * @Description: 使用官方esp_lcd_touch_spd2010组件的触摸驱动头文件
 */

#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_types.h"

// 官方SPD2010触摸组件头文件
#include "esp_lcd_touch.h"
#include "esp_lcd_touch_spd2010.h"

/*********************
 * 触摸屏参数配置
 *********************/

// 触摸屏分辨率（与显示屏分辨率一致）
#define TOUCH_MAX_X                     412
#define TOUCH_MAX_Y                     412

// I2C接口配置（使用外部已初始化的I2C驱动）
#define TOUCH_I2C_PORT                  0                   // I2C端口号（与I2C_Driver.h一致）

// 触摸控制引脚配置
#define TOUCH_RST_PIN                   (-1)                // 复位引脚（-1表示不使用）
#define TOUCH_INT_PIN                   (-1)                // 中断引脚（-1表示不使用，因为LVGL使用轮询方式）

// 触摸屏坐标变换配置
#define TOUCH_SWAP_XY                   0                   // 是否交换X和Y坐标
#define TOUCH_MIRROR_X                  0                   // 是否镜像X坐标
#define TOUCH_MIRROR_Y                  0                   // 是否镜像Y坐标

// 触摸相关常量
#define TOUCH_MAX_POINTS                5                   // 最大支持触摸点数
#define TOUCH_DEBOUNCE_TIME_MS          50                  // 触摸防抖时间

/*********************
 * 全局变量声明
 *********************/

// 触摸句柄
extern esp_lcd_touch_handle_t touch_handle;
extern esp_lcd_panel_io_handle_t touch_io_handle;

/*********************
 * 函数声明
 *********************/

esp_err_t Touch_Init_Official(void);
void      Touch_Deinit_Official(void);
bool      Touch_Get_xy_Official(uint16_t *touch_x, uint16_t *touch_y, uint16_t *strength,
                                uint8_t *touch_count, uint8_t max_points);
esp_lcd_touch_handle_t Touch_Get_Handle_Official(void);
bool      Touch_Is_Initialized_Official(void);

/*********************
 * 兼容性宏定义
 *********************/

#define Touch_Init()                    Touch_Init_Official()
#define Touch_Get_xy(x, y, s, c, m)     Touch_Get_xy_Official(x, y, s, c, m)
#define Touch_Deinit()                  Touch_Deinit_Official()
