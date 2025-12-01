/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-31 23:19:25
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-25 17:09:57
 * @FilePath: \xn_esp32_lottie\components\xn_lottie_manager\include\xn_lottie_manager.h
 * @Description: 简单的Lottie动画管理器
 */

#ifndef XN_LOTTIE_APP_H
#define XN_LOTTIE_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

// 动画类型宏定义
#define LOTTIE_ANIM_WIFI           0
#define LOTTIE_ANIM_MIC            1
#define LOTTIE_ANIM_SPEAK          2
#define LOTTIE_ANIM_THINK          3
#define LOTTIE_ANIM_COOL           4
#define LOTTIE_ANIM_LOADING        5
#define LOTTIE_ANIM_OTA            6

// 可以继续添加更多动画类型...

// Lottie 管理器初始化配置（预留多屏兼容等扩展使用）
typedef struct {
    uint16_t screen_width;   // 屏幕宽度
    uint16_t screen_height;  // 屏幕高度
} xn_lottie_app_config_t;

/**
 * @brief 初始化 Lottie 管理器（包含底层 LVGL / 屏幕 / SPIFFS / 管理器）
 *
 * 若 cfg 传入 NULL，则使用默认配置。
 *
 * @param cfg 配置指针，可为 NULL
 * @return esp_err_t ESP_OK 表示成功
 */
esp_err_t xn_lottie_manager_init(const xn_lottie_app_config_t *cfg);

/**
 * @brief 播放指定路径的动画
 * @param file_path 动画文件路径
 * @param width 动画宽度
 * @param height 动画高度
 * @return true 成功，false 失败
 */
bool lottie_manager_play(const char *file_path, uint16_t width, uint16_t height);

/**
 * @brief 播放指定路径的动画并设置中心对齐偏移
 * @param file_path 动画文件路径
 * @param width 动画宽度
 * @param height 动画高度
 * @param x 相对于中心的X轴偏移
 * @param y 相对于中心的Y轴偏移
 * @return true 成功，false 失败
 */
bool lottie_manager_play_at_pos(const char *file_path, uint16_t width, uint16_t height, int16_t x, int16_t y);

/**
 * @brief 停止当前动画
 */
void lottie_manager_stop(void);

/**
 * @brief 隐藏当前动画
 */
void lottie_manager_hide(void);

/**
 * @brief 显示当前动画
 */
void lottie_manager_show(void);

/**
 * @brief 设置动画位置
 * @param x X坐标
 * @param y Y坐标
 */
void lottie_manager_set_pos(int16_t x, int16_t y);

/**
 * @brief 居中显示动画
 */
void lottie_manager_center(void);

/**
 * @brief 播放指定类型的动画（简单API）
 * @param anim_type 动画类型宏（如LOTTIE_ANIM_MIC）
 * @return true 成功，false 失败
 */
bool lottie_manager_play_anim(int anim_type);

/**
 * @brief 播放指定类型的动画并设置中心对齐偏移（简单API）
 * @param anim_type 动画类型宏（如LOTTIE_ANIM_MIC）
 * @param x 相对于中心的X轴偏移
 * @param y 相对于中心的Y轴偏移
 * @return true 成功，false 失败
 */
bool lottie_manager_play_anim_at_pos(int anim_type, int16_t x, int16_t y);

/**
 * @brief 停止指定类型的动画（简单API）
 * @param anim_type 动画类型宏，-1表示停止当前所有动画
 */
void lottie_manager_stop_anim(int anim_type);

/**
 * @brief 显示图片
 */
bool lottie_manager_show_image(const char *img_path, uint16_t width, uint16_t height);

/**
 * @brief 隐藏图片
 */
void lottie_manager_hide_image(void);

#ifdef __cplusplus
}
#endif

#endif // XN_LOTTIE_APP_H