/*
 * @Author: xingnian
 * @Date: 2025-11-30
 * @FilePath: \xn_esp32_coze_chat\main\lottie_app\lottie_app.h
 * @Description: Lottie 动画应用封装头文件
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/**
 * @brief 初始化 Lottie 动画应用
 *
 * 负责初始化 LVGL 显示驱动、挂载 Lottie SPIFFS 分区，
 * 并启动 Lottie 管理器任务。
 */
esp_err_t lottie_app_init(void);

/**
 * @brief 显示 WiFi 连接中动画
 */
void lottie_app_show_wifi_connecting(void);

/**
 * @brief 显示麦克风待机动画
 */
void lottie_app_show_mic_idle(void);

/**
 * @brief 显示「说话中」动画
 */
void lottie_app_show_speaking(void);

/**
 * @brief 显示「思考中」动画
 */
void lottie_app_show_thinking(void);

/**
 * @brief 显示通用加载动画
 */
void lottie_app_show_loading(void);

/**
 * @brief 显示 OTA 升级动画
 */
void lottie_app_show_ota(void);

/**
 * @brief 停止当前动画
 */
void lottie_app_stop(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

