/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-30 11:30:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-15 10:55:06
 * @FilePath: \ESP_ChunFeng\main\bsp\bsp_display_lvgl\bsp_lvgl_driver.h
 * @Description: LVGL 9.2.2 驱动头文件 - 为ESP32S3 + SPD2010显示屏设计
 */

#pragma once

#include <stdio.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// LVGL 核心头文件
#include "lvgl.h"

// 硬件驱动头文件（BSP 层）
#include "bsp_panel_spd2010.h"
#include "bsp_touch_spd2010.h"

/*********************
 * 配置宏定义
 *********************/

// LVGL Tick 定时器周期 (毫秒)
// 【性能优化】调整为100ms（10 FPS）以降低CPU负载
#define LVGL_TICK_PERIOD_MS     100

// LVGL 显示缓冲区大小 (像素数)
// 【性能优化】设置为屏幕的1/10，减少刷新次数，降低CPU负载
// 每次可刷新更多像素，减少刷新回调次数（内存增加约8.5KB PSRAM）
#define LVGL_BUFFER_SIZE        (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 20)

/*********************
 * 全局变量声明
 *********************/

// LVGL 显示对象
extern lv_display_t *g_lvgl_display;

// LVGL 输入设备对象
extern lv_indev_t *g_lvgl_indev;

/*********************
 * 函数声明
 *********************/

/**
 * @brief 初始化LVGL库和驱动
 * @return ESP_OK 成功, 其他值表示失败
 */
esp_err_t lvgl_driver_init(void);

/**
 * @brief 反初始化LVGL驱动
 */
void lvgl_driver_deinit(void);

/**
 * @brief LVGL tick增加回调函数
 * @param arg 未使用的参数
 */
void lvgl_tick_inc_cb(void *arg);

/**
 * @brief LVGL显示刷新回调函数
 * @param disp 显示对象指针
 * @param area 要刷新的区域
 * @param px_map 像素数据缓冲区
 */
void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

/**
 * @brief LVGL触摸输入读取回调函数
 * @param indev 输入设备对象指针
 * @param data 输入数据结构指针
 */
void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);