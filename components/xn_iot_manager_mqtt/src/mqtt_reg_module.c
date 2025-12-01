/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 13:53:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 16:16:58
 * @FilePath: \xn_esp32_web_mqtt_manager\components\iot_manager_mqtt\src\mqtt_reg_module.c
 * @Description: 设备注册模块实现
 *
 * 功能概述：
 *  - 在 MQTT 连接建立后，向服务器查询当前设备是否已注册；
 *  - 收到注册相关回复后标记注册完成；
 *  - 后续可供心跳模块查询注册状态。
 *
 * 具体报文格式可根据实际后台约定在此模块内调整，这里仅给出简单占位实现。
 */

#include <string.h>
#include <stdio.h>

#include "esp_log.h"

#include "mqtt_module.h"
#include "mqtt_app_module.h"
#include "mqtt_reg_module.h"

/* 日志 TAG */
static const char *TAG = "mqtt_reg";              ///< 本模块日志 TAG

static const web_mqtt_manager_config_t *s_mgr_cfg = NULL; ///< 保存管理器配置指针
static bool                             s_registered = false; ///< 是否已注册

/**
 * @brief 内部辅助：设备唯一标识
 *
 * 当前简单使用 client_id 作为设备唯一标识；若为空则使用占位字符串。
 */
static const char *mqtt_reg_get_device_id(void)
{
    if (s_mgr_cfg && s_mgr_cfg->client_id && s_mgr_cfg->client_id[0] != '\0') {
        return s_mgr_cfg->client_id;               ///< 优先使用上层配置的 client_id
    }
    return "unknown_device";                      ///< 占位设备 ID
}

/**
 * @brief 设备注册模块的消息回调
 *
 * 注册到管理器时使用的回调：只要 Topic 前缀匹配 base_topic+"/reg" 即会被调用。
 * 这里简单地认为：只要收到任意来自服务器的 reg 相关回复，即表示注册成功。
 */
static esp_err_t mqtt_reg_module_on_message(const char    *topic,
                                            int            topic_len,
                                            const uint8_t *payload,
                                            int            payload_len)
{
    (void)payload;
    (void)payload_len;

    if (s_mgr_cfg == NULL || s_mgr_cfg->base_topic == NULL || topic == NULL) {
        return ESP_OK;
    }

    const char *device_id = mqtt_reg_get_device_id();
    if (device_id == NULL || device_id[0] == '\0') {
        return ESP_OK;
    }

    char prefix[128];
    int  n = snprintf(prefix, sizeof(prefix), "%s/reg/", s_mgr_cfg->base_topic);
    if (n <= 0 || n >= (int)sizeof(prefix)) {
        return ESP_OK;
    }

    int prefix_len = n;
    size_t dev_len = strlen(device_id);
    int suffix_len = 5; /* strlen("/resp") */

    if (topic_len != prefix_len + (int)dev_len + suffix_len) {
        return ESP_OK;
    }

    if (memcmp(topic, prefix, prefix_len) != 0) {
        return ESP_OK;
    }

    if (memcmp(topic + prefix_len, device_id, dev_len) != 0) {
        return ESP_OK;
    }

    if (memcmp(topic + prefix_len + (int)dev_len, "/resp", (size_t)suffix_len) != 0) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "recv reg message, mark as registered");
    s_registered = true;

    return ESP_OK;
}

esp_err_t mqtt_reg_module_init(const web_mqtt_manager_config_t *mgr_cfg)
{
    if (mgr_cfg == NULL) {                         ///< 管理器配置不可为空
        return ESP_ERR_INVALID_ARG;                 ///< 返回参数错误
    }

    s_mgr_cfg   = mgr_cfg;                         ///< 保存配置指针
    s_registered = false;                          ///< 初始视为未注册

    /* 在管理器中注册本模块的消息回调，使用前缀 "reg" */
    esp_err_t ret = web_mqtt_manager_register_app("reg", ///< 模块前缀
                                                   mqtt_reg_module_on_message); ///< 回调
    if (ret != ESP_OK) {                           ///< 注册失败
        ESP_LOGE(TAG, "register app failed: %s",  ///< 打印错误日志
                 esp_err_to_name(ret));            ///< 错误码转字符串
        return ret;                                 ///< 返回错误
    }

    return ESP_OK;                                  ///< 返回成功
}

void mqtt_reg_module_on_connected(void)
{
    if (s_mgr_cfg == NULL || s_mgr_cfg->base_topic == NULL) { ///< 未正确初始化
        return;                                     ///< 直接返回
    }

    char topic[128];                                ///< Topic 缓冲区
    const char *device_id = mqtt_reg_get_device_id(); ///< 设备 ID 字符串

    /* 约定查询 Topic: base_topic + "/reg/query" */
    snprintf(topic, sizeof(topic), "%s/reg/query", WEB_MQTT_UPLINK_BASE_TOPIC);

    ESP_LOGI(TAG, "send reg query, id=%s, topic=%s", ///< 打印日志
             device_id, topic);                      ///< 设备 ID 与 Topic

    (void)mqtt_module_publish(topic,                ///< 发送到查询 Topic
                               device_id,           ///< 负载使用设备 ID
                               (int)strlen(device_id), ///< 负载长度
                               1,                   ///< QoS 1 示例
                               false);              ///< 不保留
}

bool mqtt_reg_module_is_registered(void)
{
    return s_registered;                            ///< 返回注册标志
}
