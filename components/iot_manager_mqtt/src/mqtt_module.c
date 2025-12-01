/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 12:40:30
 * @LastEditors: xingnian && jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 12:40:30
 * @FilePath: \xn_web_mqtt_manager\components\iot_manager_mqtt\src\mqtt_module.c
 * @Description: MQTT 模块实现（封装 esp-mqtt 客户端）
 * 
 * 仅负责：
 *  - 根据配置创建 MQTT 客户端；
 *  - 启动/停止客户端；
 *  - 将底层事件转换为简单的 mqtt_module_event_t 上报给上层。
 * 
 * 不直接处理业务 Topic，由上层管理器决定订阅/发布策略。
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "mqtt_client.h"

#include "mqtt_module.h"

/* 日志 TAG */
static const char *TAG = "mqtt_module";           ///< 本模块日志 TAG

/* MQTT 模块配置与状态 */
static mqtt_module_config_t   s_mqtt_cfg;          ///< 保存一份配置副本
static bool                   s_mqtt_inited = false; ///< 是否已初始化
static esp_mqtt_client_handle_t s_mqtt_client = NULL; ///< MQTT 客户端句柄

/**
 * @brief 内部辅助：统一分发事件到上层回调
 */
static void mqtt_module_dispatch_event(mqtt_module_event_t event)
{
    if (s_mqtt_cfg.event_cb) {                    ///< 若上层配置了回调
        s_mqtt_cfg.event_cb(event);               ///< 则转发事件
    }
}

/**
 * @brief esp-mqtt 事件回调
 *
 * 由 IDF MQTT 客户端在其内部任务中调用，用于通知连接状态变化等。
 */
static void mqtt_module_event_handler(void           *handler_args,
                                      esp_event_base_t base,
                                      int32_t         event_id,
                                      void           *event_data)
{
    (void)handler_args;                            ///< 未使用参数
    (void)base;                                    ///< 未使用参数

    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data; ///< 事件数据

    switch ((esp_mqtt_event_id_t)event_id) {       ///< 根据事件 ID 分类处理
    case MQTT_EVENT_CONNECTED:                     ///< 已连接事件
        ESP_LOGI(TAG, "MQTT connected");          ///< 打印日志
        mqtt_module_dispatch_event(MQTT_MODULE_EVENT_CONNECTED); ///< 上报已连接
        break;                                     ///< 结束分支

    case MQTT_EVENT_DISCONNECTED:                  ///< 断开连接事件
        ESP_LOGW(TAG, "MQTT disconnected");       ///< 打印警告日志
        mqtt_module_dispatch_event(MQTT_MODULE_EVENT_DISCONNECTED); ///< 上报断开
        break;                                     ///< 结束分支

    case MQTT_EVENT_ERROR:                         ///< 错误事件
        ESP_LOGE(TAG, "MQTT error");             ///< 打印错误日志
        mqtt_module_dispatch_event(MQTT_MODULE_EVENT_ERROR); ///< 上报错误
        break;                                     ///< 结束分支

    case MQTT_EVENT_DATA:                          ///< 收到一条 MQTT 消息
        ESP_LOGI(TAG, "MQTT data: topic=%.*s, len=%d", ///< 打印简单日志
                 event->topic_len,                  ///< Topic 长度
                 event->topic,                      ///< Topic 内容
                 event->data_len);                  ///< 数据长度

        if (s_mqtt_cfg.message_cb) {                ///< 若配置了消息回调
            s_mqtt_cfg.message_cb(                  ///< 调用上层回调
                event->topic,                       ///< Topic 指针
                (int)event->topic_len,              ///< Topic 长度
                (const uint8_t *)event->data,       ///< 负载指针
                (int)event->data_len);              ///< 负载长度
        }
        break;                                     ///< 结束分支

    default:                                       ///< 其他事件暂不关心
        break;                                     ///< 直接忽略
    }
}

esp_err_t mqtt_module_init(const mqtt_module_config_t *config)
{
    /* 若已初始化则直接返回 */
    if (s_mqtt_inited) {                           ///< 已初始化标志
        return ESP_OK;                             ///< 直接视为成功
    }

    /* 装载配置：优先使用外部配置，否则使用默认配置 */
    if (config == NULL) {                          ///< 未传入配置
        s_mqtt_cfg = MQTT_MODULE_DEFAULT_CONFIG(); ///< 使用默认配置
    } else {                                       ///< 传入了配置
        s_mqtt_cfg = *config;                      ///< 直接保存一份副本
    }

    /* broker_uri 为必填项 */
    if (s_mqtt_cfg.broker_uri == NULL ||           ///< URI 为空
        s_mqtt_cfg.broker_uri[0] == '\0') {       ///< 或者空字符串
        return ESP_ERR_INVALID_ARG;                ///< 返回参数错误
    }

    /* 构造 esp-mqtt 配置 */
    esp_mqtt_client_config_t mqtt_cfg = (esp_mqtt_client_config_t){ 0 }; ///< 清零配置结构体

    mqtt_cfg.broker.address.uri = s_mqtt_cfg.broker_uri; ///< 设置服务器 URI

    if (s_mqtt_cfg.client_id != NULL) {            ///< 如配置了 client_id
        mqtt_cfg.credentials.client_id = s_mqtt_cfg.client_id; ///< 则传递给底层
    }
    if (s_mqtt_cfg.username != NULL) {             ///< 如配置了用户名
        mqtt_cfg.credentials.username = s_mqtt_cfg.username;   ///< 则传递给底层
    }
    if (s_mqtt_cfg.password != NULL) {             ///< 如配置了密码
        mqtt_cfg.credentials.authentication.password = s_mqtt_cfg.password; ///< 则传递给底层
    }

    if (s_mqtt_cfg.keepalive_sec > 0) {            ///< 配置了 keepalive
        mqtt_cfg.session.keepalive = (uint16_t)s_mqtt_cfg.keepalive_sec; ///< 使用该值
    }

    /* 创建 MQTT 客户端实例 */
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg); ///< 初始化客户端
    if (s_mqtt_client == NULL) {                    ///< 创建失败
        ESP_LOGE(TAG, "esp_mqtt_client_init failed"); ///< 打印错误日志
        return ESP_ERR_NO_MEM;                      ///< 返回内存不足
    }

    /* 注册事件回调 */
    esp_err_t ret = esp_mqtt_client_register_event( ///< 注册事件回调
        s_mqtt_client,                              ///< 客户端句柄
        ESP_EVENT_ANY_ID,                           ///< 关心所有事件
        mqtt_module_event_handler,                  ///< 回调函数
        NULL);                                      ///< 传入参数（此处未用）

    if (ret != ESP_OK) {                            ///< 注册失败
        ESP_LOGE(TAG, "esp_mqtt_client_register_event failed: %s", ///< 打印错误
                 esp_err_to_name(ret));             ///< 错误码转字符串
        esp_mqtt_client_destroy(s_mqtt_client);     ///< 销毁客户端
        s_mqtt_client = NULL;                       ///< 句柄清空
        return ret;                                 ///< 返回错误码
    }

    s_mqtt_inited = true;                           ///< 标记已初始化
    return ESP_OK;                                  ///< 返回成功
}

esp_err_t mqtt_module_start(void)
{
    if (!s_mqtt_inited || s_mqtt_client == NULL) {  ///< 未初始化或无客户端
        return ESP_ERR_INVALID_STATE;               ///< 返回状态错误
    }

    /* 启动 MQTT 客户端（内部将自动重连等） */
    esp_err_t ret = esp_mqtt_client_start(s_mqtt_client); ///< 启动客户端
    if (ret != ESP_OK) {                            ///< 启动失败
        ESP_LOGE(TAG, "esp_mqtt_client_start failed: %s", ///< 打印错误
                 esp_err_to_name(ret));             ///< 错误码转字符串
        return ret;                                 ///< 返回错误
    }

    return ESP_OK;                                  ///< 返回成功
}

esp_err_t mqtt_module_stop(void)
{
    if (!s_mqtt_inited || s_mqtt_client == NULL) {  ///< 未初始化或无客户端
        return ESP_ERR_INVALID_STATE;               ///< 返回状态错误
    }

    /* 停止客户端，不销毁句柄，便于后续再次启动 */
    esp_err_t ret = esp_mqtt_client_stop(s_mqtt_client); ///< 停止客户端
    if (ret != ESP_OK) {                            ///< 停止失败
        ESP_LOGE(TAG, "esp_mqtt_client_stop failed: %s", ///< 打印错误
                 esp_err_to_name(ret));             ///< 错误码转字符串
        return ret;                                 ///< 返回错误
    }

    return ESP_OK;                                  ///< 返回成功
}

esp_err_t mqtt_module_publish(const char *topic,
                              const void *payload,
                              int         len,
                              int         qos,
                              bool        retain)
{
    if (!s_mqtt_inited || s_mqtt_client == NULL) {  ///< 未初始化或无客户端
        return ESP_ERR_INVALID_STATE;               ///< 返回状态错误
    }

    if (topic == NULL || topic[0] == '\0') {       ///< Topic 非法
        return ESP_ERR_INVALID_ARG;                 ///< 返回参数错误
    }

    if (len < 0) {                                  ///< 长度为负数
        return ESP_ERR_INVALID_ARG;                 ///< 返回参数错误
    }

    const char *data = (const char *)payload;       ///< 负载按字节视为字符串

    int msg_id = esp_mqtt_client_publish(           ///< 调用底层发布接口
        s_mqtt_client,                              ///< 客户端句柄
        topic,                                      ///< Topic 字符串
        data,                                       ///< 数据指针（可为 NULL）
        len,                                        ///< 数据长度
        qos,                                        ///< QoS 等级
        retain);                                    ///< 是否保留

    if (msg_id < 0) {                               ///< 发布失败
        ESP_LOGE(TAG, "esp_mqtt_client_publish failed, ret=%d", msg_id);
        return ESP_FAIL;                            ///< 返回失败
    }

    return ESP_OK;                                  ///< 返回成功
}

esp_err_t mqtt_module_subscribe(const char *topic, int qos)
{
    if (!s_mqtt_inited || s_mqtt_client == NULL) {  ///< 未初始化或无客户端
        return ESP_ERR_INVALID_STATE;               ///< 返回状态错误
    }

    if (topic == NULL || topic[0] == '\0') {       ///< Topic 非法
        return ESP_ERR_INVALID_ARG;                 ///< 返回参数错误
    }

    if (qos < 0 || qos > 2) {                       ///< QoS 范围非法
        return ESP_ERR_INVALID_ARG;                 ///< 返回参数错误
    }

    int msg_id = esp_mqtt_client_subscribe(        ///< 调用底层订阅接口
        s_mqtt_client,                              ///< 客户端句柄
        topic,                                      ///< Topic 字符串
        qos);                                       ///< QoS 等级

    if (msg_id < 0) {                               ///< 订阅失败
        ESP_LOGE(TAG, "esp_mqtt_client_subscribe failed, ret=%d", msg_id);
        return ESP_FAIL;                            ///< 返回失败
    }

    return ESP_OK;                                  ///< 返回成功
}

esp_err_t mqtt_module_unsubscribe(const char *topic)
{
    if (!s_mqtt_inited || s_mqtt_client == NULL) {  ///< 未初始化或无客户端
        return ESP_ERR_INVALID_STATE;               ///< 返回状态错误
    }

    if (topic == NULL || topic[0] == '\0') {       ///< Topic 非法
        return ESP_ERR_INVALID_ARG;                 ///< 返回参数错误
    }

    int msg_id = esp_mqtt_client_unsubscribe(      ///< 调用底层取消订阅接口
        s_mqtt_client,                              ///< 客户端句柄
        topic);                                     ///< Topic 字符串

    if (msg_id < 0) {                               ///< 取消失败
        ESP_LOGE(TAG, "esp_mqtt_client_unsubscribe failed, ret=%d", msg_id);
        return ESP_FAIL;                            ///< 返回失败
    }

    return ESP_OK;                                  ///< 返回成功
}
