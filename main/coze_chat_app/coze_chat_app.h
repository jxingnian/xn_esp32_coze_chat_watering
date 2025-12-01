/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-12 16:30:00
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-10-12 16:30:00
 * @FilePath: \esp-chunfeng\main\coze_chat\coze_chat_app.h
 * @Description: Coze聊天应用程序头文件（基于espressif/esp_coze组件）
 *
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

/**
 * @brief 初始化Coze聊天应用程序
 *
 * @return
 *       - ESP_OK  成功
 *       - Other   失败时返回相应的esp_err_t错误代码
 */
esp_err_t coze_chat_app_init(void);

/**
 * @brief 反初始化Coze聊天应用程序
 *
 * @return
 *       - ESP_OK  成功
 *       - Other   失败时返回相应的esp_err_t错误代码
 */
esp_err_t coze_chat_app_deinit(void);

// 注意：使用USB RNDIS后，4G和WiFi统一，不再需要modem相关函数

#ifdef __cplusplus
}
#endif  /* __cplusplus */

