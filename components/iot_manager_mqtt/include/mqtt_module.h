/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 12:40:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 14:08:18
 * @FilePath: \xn_web_mqtt_manager\components\iot_manager_mqtt\include\mqtt_module.h
 * @Description: MQTT 模块对外接口（仅负责 MQTT 客户端连接与事件上报）
 * 
 * 设计要点：
 * - 只关心 MQTT 客户端本身，不直接耦合上层业务；
 * - 通过简单事件回调向上层报告连接状态变化；
 * - 由 web_mqtt_manager 在初始化时配置 broker_uri / 认证信息等。
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */

#ifndef MQTT_MODULE_H
#define MQTT_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"  ///< ESP-IDF 通用错误码

/* -------------------------------------------------------------------------- */
/*                               事件与回调类型                                */
/* -------------------------------------------------------------------------- */

/**
 * @brief MQTT 模块向上层上报的事件
 *
 * 仅抽象出连接相关的几个阶段，便于上层状态机统一处理。
 */
typedef enum {
    MQTT_MODULE_EVENT_CONNECTED = 0,  ///< 已成功与服务器建立 MQTT 连接
    MQTT_MODULE_EVENT_DISCONNECTED,   ///< MQTT 连接已断开
    MQTT_MODULE_EVENT_ERROR,          ///< 发生错误（解析失败、网络异常等）
} mqtt_module_event_t;

/**
 * @brief MQTT 模块事件回调
 *
 * @param event 当前发生的 MQTT 事件
 */
typedef void (*mqtt_module_event_cb_t)(mqtt_module_event_t event);

/**
 * @brief MQTT 模块收到消息时的回调
 *
 * 由底层在收到任意 MQTT 消息时调用，上层可在此解析 Topic 与负载。
 *
 * @param topic       消息所属 Topic 指针（不保证以 '\0' 结尾）
 * @param topic_len   Topic 长度（字节数）
 * @param payload     负载数据指针
 * @param payload_len 负载长度（字节数）
 */
typedef void (*mqtt_module_message_cb_t)(const char  *topic,
                                         int          topic_len,
                                         const uint8_t *payload,
                                         int          payload_len);

/* -------------------------------------------------------------------------- */
/*                                   配置体                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief MQTT 模块初始化配置
 *
 * 只包含与 MQTT 客户端本身相关的参数，不涉及业务 Topic。
 */
typedef struct {
    const char           *broker_uri;    ///< MQTT 服务器 URI，如 "mqtt://192.168.1.10:1883"
    const char           *client_id;     ///< 客户端 ID，NULL 表示使用内部默认
    const char           *username;      ///< 用户名，可为 NULL 表示匿名
    const char           *password;      ///< 密码，可为 NULL 表示无密码
    int                   keepalive_sec; ///< keepalive 保活时间（秒），<=0 使用内部默认
    mqtt_module_event_cb_t  event_cb;    ///< 连接事件回调，可为 NULL 表示不关心
    mqtt_module_message_cb_t message_cb; ///< 消息回调，可为 NULL 表示不关心
} mqtt_module_config_t;

/* -------------------------------------------------------------------------- */
/*                                  默认配置                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief MQTT 模块默认配置
 */
#define MQTT_MODULE_DEFAULT_CONFIG()                 \
    (mqtt_module_config_t){                         \
        .broker_uri    = NULL,                      \
        .client_id     = NULL,                      \
        .username      = NULL,                      \
        .password      = NULL,                      \
        .keepalive_sec = 60,                        \
        .event_cb      = NULL,                      \
        .message_cb    = NULL,                      \
    }

/* -------------------------------------------------------------------------- */
/*                                  对外接口                                   */
/* -------------------------------------------------------------------------- */

/**
 * @brief 初始化 MQTT 模块
 *
 * - config 为 NULL 时使用 MQTT_MODULE_DEFAULT_CONFIG；
 * - 仅保存配置并创建 MQTT 客户端实例，不主动连接服务器；
 * - 可多次调用，只有第一次真正初始化。
 *
 * @param config MQTT 模块配置指针
 *
 * @return
 *      - ESP_OK              : 初始化成功（或已初始化）
 *      - ESP_ERR_INVALID_ARG : broker_uri 为空
 *      - ESP_ERR_NO_MEM 等   : 内部资源不足
 */
esp_err_t mqtt_module_init(const mqtt_module_config_t *config);

/**
 * @brief 启动 MQTT 客户端并发起连接
 *
 * - 需在 mqtt_module_init 之后调用；
 * - 连接结果通过事件回调上报。
 */
esp_err_t mqtt_module_start(void);

/**
 * @brief 停止 MQTT 客户端并断开连接
 */
esp_err_t mqtt_module_stop(void);

/**
 * @brief 发布一条 MQTT 消息
 *
 * @param topic   目标 Topic 字符串
 * @param payload 负载数据指针
 * @param len     负载长度（字节）
 * @param qos     MQTT QoS 等级（0/1/2）
 * @param retain  是否保留消息
 *
 * @return
 *      - ESP_OK              : 已成功提交到客户端
 *      - ESP_ERR_INVALID_ARG : 参数非法
 *      - ESP_ERR_INVALID_STATE : 客户端未初始化或未启动
 *      - ESP_FAIL            : 底层发送失败
 */
esp_err_t mqtt_module_publish(const char *topic,
                              const void *payload,
                              int         len,
                              int         qos,
                              bool        retain);

/**
 * @brief 订阅指定 Topic
 *
 * @param topic  需要订阅的 Topic 字符串
 * @param qos    期望的 QoS 等级（0/1/2）
 *
 * @return
 *      - ESP_OK               : 已成功提交订阅请求
 *      - ESP_ERR_INVALID_ARG  : 参数非法
 *      - ESP_ERR_INVALID_STATE: 客户端未初始化或未启动
 *      - ESP_FAIL             : 底层返回订阅失败
 */
esp_err_t mqtt_module_subscribe(const char *topic, int qos);

/**
 * @brief 取消订阅指定 Topic
 *
 * @param topic  需要取消订阅的 Topic 字符串
 *
 * @return
 *      - ESP_OK               : 已成功提交取消订阅请求
 *      - ESP_ERR_INVALID_ARG  : 参数非法
 *      - ESP_ERR_INVALID_STATE: 客户端未初始化或未启动
 *      - ESP_FAIL             : 底层返回取消失败
 */
esp_err_t mqtt_module_unsubscribe(const char *topic);

#endif /* MQTT_MODULE_H */
