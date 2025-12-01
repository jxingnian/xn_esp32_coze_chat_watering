/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-30 16:00:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-25 16:42:00
 * @FilePath: \xn_esp32_lottie\components\xn_lottie\include\xn_lottie_panel_spd2010.h
 * @Description: 使用官方SPD2010组件的LCD驱动头文件
 */

#pragma once

// ESP-IDF及标准库头文件
#include "esp_err.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_io_interface.h"
#include "esp_intr_alloc.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "lvgl.h"
#include "driver/ledc.h"

// 官方SPD2010组件头文件
#include "esp_lcd_spd2010.h"
#include "bsp_touch_spd2010.h"
#include "bsp_exio_tca9554.h"

// LCD显示屏参数定义
#define EXAMPLE_LCD_WIDTH                   (412)                 // LCD屏幕宽度，单位：像素
#define EXAMPLE_LCD_HEIGHT                  (412)                 // LCD屏幕高度，单位：像素
#define EXAMPLE_LCD_COLOR_BITS              (16)                  // 颜色位深，16位表示RGB565格式

// SPI总线配置参数
#define ESP_PANEL_HOST_SPI_ID_DEFAULT       (SPI2_HOST)           // 使用SPI2主机接口
#define ESP_PANEL_LCD_SPI_MODE              (3)                   // SPI模式，SPD2010使用模式3
#define ESP_PANEL_LCD_SPI_CLK_HZ            (20 * 1000 * 1000)    // SPI时钟频率20MHz（QSPI模式推荐值）
#define ESP_PANEL_LCD_SPI_TRANS_QUEUE_SZ    (20)                  // SPI传输队列大小（增加以避免队列溢出）
#define ESP_PANEL_LCD_SPI_CMD_BITS          (32)                  // SPI命令位数（QSPI模式）
#define ESP_PANEL_LCD_SPI_PARAM_BITS        (8)                   // SPI参数位数

// LCD SPI接口引脚定义（保持您原来的引脚配置）
#define ESP_PANEL_LCD_SPI_IO_SCK            (40)                  // SPI时钟信号引脚
#define ESP_PANEL_LCD_SPI_IO_DATA0          (46)                  // SPI数据线0引脚（QSPI模式下的D0）
#define ESP_PANEL_LCD_SPI_IO_DATA1          (45)                  // SPI数据线1引脚（QSPI模式下的D1）
#define ESP_PANEL_LCD_SPI_IO_DATA2          (42)                  // SPI数据线2引脚（QSPI模式下的D2）
#define ESP_PANEL_LCD_SPI_IO_DATA3          (41)                  // SPI数据线3引脚（QSPI模式下的D3）
#define ESP_PANEL_LCD_SPI_IO_CS             (21)                  // SPI片选信号引脚
#define EXAMPLE_LCD_PIN_NUM_RST             (-1)                  // LCD复位引脚，-1表示通过EXIO2扩展IO控制
#define EXAMPLE_LCD_PIN_NUM_BK_LIGHT        (5)                   // LCD背光控制引脚

// 背光控制电平定义
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL       (1)                   // 背光开启时的电平状态（高电平有效）
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  // 背光关闭时的电平状态

// LEDC PWM背光控制配置
#define LEDC_HS_TIMER          LEDC_TIMER_0                       // 使用LEDC定时器0
#define LEDC_LS_MODE           LEDC_LOW_SPEED_MODE                // LEDC低速模式
#define LEDC_HS_CH0_GPIO       EXAMPLE_LCD_PIN_NUM_BK_LIGHT       // PWM输出引脚，连接到背光控制引脚
#define LEDC_HS_CH0_CHANNEL    LEDC_CHANNEL_0                     // 使用LEDC通道0
#define LEDC_TEST_DUTY         (4000)                             // PWM占空比测试值
#define LEDC_ResolutionRatio   LEDC_TIMER_13_BIT                  // PWM分辨率为13位（0-8191）
#define LEDC_MAX_Duty          ((1 << LEDC_ResolutionRatio) - 1)  // PWM最大占空比值（8191）
#define Backlight_MAX   100                                       // 背光亮度最大值（百分比）

// 全局变量声明
extern esp_lcd_panel_handle_t panel_handle;                      // LCD面板句柄，用于LCD操作
extern esp_lcd_panel_io_handle_t io_handle;                      // LCD面板IO句柄
extern uint8_t LCD_Backlight;                                    // 当前背光亮度值（0-100）

// 函数声明

/**
 * @brief SPD2010复位函数（官方组件版本）
 * @note 通过控制EXIO2引脚实现SPD2010的硬件复位
 */
void SPD2010_Reset_Official(void);

/**
 * @brief SPD2010显示驱动初始化（官方组件版本）
 * @note 使用官方SPD2010组件初始化显示控制器
 */
void SPD2010_Init_Official(void);

/**
 * @brief LCD显示屏初始化函数（官方组件版本）
 * @note 必须在主函数中调用此函数来初始化屏幕！
 *       包含SPI接口初始化、LCD控制器初始化、背光初始化等
 */
void LCD_Init_Official(void);

/**
 * @brief 初始化LCD背光PWM控制（官方组件版本）
 * @note 配置LEDC PWM用于背光亮度控制
 */
void Backlight_Init_Official(void);

/**
 * @brief 设置LCD背光亮度（官方组件版本）
 * @param Light 背光亮度值，范围0-100（0为最暗，100为最亮）
 * @note 通过PWM控制背光亮度，实现平滑的亮度调节
 */
void Set_Backlight_Official(uint8_t Light);

/**
 * @brief 注册LVGL flush完成回调
 * @param display LVGL显示对象指针
 * @return esp_err_t 注册结果
 * @note 使用官方组件的硬件完成回调机制
 */
esp_err_t SPD2010_Register_LVGL_Callback(lv_display_t *display);

/**
 * @brief 获取面板句柄
 * @return esp_lcd_panel_handle_t 面板句柄
 * @note 用于LVGL驱动获取面板句柄
 */
esp_lcd_panel_handle_t SPD2010_Get_Panel_Handle(void);
