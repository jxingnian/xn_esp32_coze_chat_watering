#ifndef WIFI_CONFIG_APP_H
#define WIFI_CONFIG_APP_H

#include "esp_err.h"

/**
 * @brief 初始化 WiFi 配置 MQTT 应用模块
 *
 * 在 Web MQTT 管理器中注册 "wifi" 前缀的消息回调，
 * 以便处理 xn/web/wifi/<device_id>/... 相关指令。
 */
esp_err_t wifi_config_app_init(void);

#endif /* WIFI_CONFIG_APP_H */
