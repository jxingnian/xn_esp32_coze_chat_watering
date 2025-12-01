/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 12:41:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 15:12:46
 * @FilePath: \xn_web_mqtt_manager\components\iot_manager_mqtt\src\web_mqtt_manager.c
 * @Description: Web MQTT 管理器实现（基于 mqtt_module 封装状态机与重连逻辑）
 * 
 * 职责划分：
 *  - 作为上层管理模块，负责 MQTT 连接状态机与自动重连策略；
 *  - 通过 mqtt_module 完成具体的 MQTT 客户端初始化与连接；
 *  - 通过 web_mqtt_event_cb_t 回调向上层报告抽象状态变化。
 * 
 * 不直接处理业务 Topic，只关注“是否连上 MQTT 服务器”。
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "esp_mac.h"

#include "mqtt_module.h"
#include "mqtt_app_module.h"
#include "mqtt_reg_module.h"
#include "mqtt_heartbeat_module.h"
#include "web_mqtt_manager.h"

/* 日志 TAG */
static const char *TAG = "web_mqtt_manager";       ///< 本模块日志 TAG

/* 管理器内部状态 */
static web_mqtt_manager_config_t s_mgr_cfg;        ///< 上层传入的管理配置副本
static web_mqtt_state_t          s_mgr_state = WEB_MQTT_STATE_DISCONNECTED; ///< 当前状态
static TaskHandle_t              s_mgr_task  = NULL; ///< 管理任务句柄
static TickType_t                s_last_error_ts = 0; ///< 最近一次错误/断开的时间戳

/* 若上层未指定 client_id，则使用该缓冲区生成一个基于 MAC 的默认 ID */
static char s_client_id_buf[32];

/* 应用模块注册表配置 */
#define WEB_MQTT_APP_MAX_NUM         8            ///< 支持的应用模块最大数量
#define WEB_MQTT_APP_SUFFIX_MAX_LEN  16           ///< 单个模块前缀最大长度

typedef struct {
    char                  suffix[WEB_MQTT_APP_SUFFIX_MAX_LEN]; ///< 模块 Topic 前缀
    web_mqtt_app_msg_cb_t cb;                   ///< 模块消息回调
} web_mqtt_app_entry_t;

static web_mqtt_app_entry_t s_app_entries[WEB_MQTT_APP_MAX_NUM]; ///< 模块表
static int                  s_app_entry_count = 0; ///< 已注册模块数量

/**
 * @brief 统一更新状态并通知上层回调
 */
static void web_mqtt_manager_notify_state(web_mqtt_state_t new_state)
{
    s_mgr_state = new_state;                       ///< 更新内部状态

    if (s_mgr_cfg.event_cb) {                      ///< 如上层配置了回调
        s_mgr_cfg.event_cb(new_state);             ///< 通知上层当前状态
    }
}

/**
 * @brief 确保管理器配置中存在有效的 client_id
 *
 * 若上层未提供（NULL 或空串），则基于芯片 MAC 生成形如
 * "ESP32_XXXXXXXXXXXX" 的默认 client_id，保证单设备唯一。
 */
static void web_mqtt_manager_ensure_client_id(void)
{
    if (s_mgr_cfg.client_id != NULL &&            ///< 已显式配置
        s_mgr_cfg.client_id[0] != '\0') {        ///< 且非空串
        return;                                    ///< 直接使用上层提供的值
    }

    uint8_t mac[6] = {0};                         ///< MAC 地址缓冲区
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA); ///< 读取 STA MAC
    if (err != ESP_OK) {                          ///< 读取失败
        snprintf(s_client_id_buf, sizeof(s_client_id_buf),
                 "ESP32_UNKNOWN");              ///< 退化为占位 ID
    } else {
        snprintf(s_client_id_buf, sizeof(s_client_id_buf),
                 "ESP32_%02X%02X%02X%02X%02X%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    s_mgr_cfg.client_id = s_client_id_buf;        ///< 填入默认 client_id
}

/**
 * @brief 在 MQTT 已连接时，为所有已注册应用模块订阅 Topic
 */
static void web_mqtt_manager_subscribe_all_apps(void)
{
    if (s_mgr_cfg.base_topic == NULL) {            ///< 未配置基础 Topic
        return;                                    ///< 直接返回
    }

    for (int i = 0; i < s_app_entry_count; ++i) {  ///< 遍历所有模块
        char filter[128];                          ///< 订阅过滤字符串
        snprintf(filter, sizeof(filter), "%s/%s/#", ///< base_topic/prefix/#
                 s_mgr_cfg.base_topic,             ///< 基础 Topic
                 s_app_entries[i].suffix);         ///< 模块前缀

        (void)mqtt_module_subscribe(filter, 1);    ///< 订阅，忽略返回值
    }
}

/**
 * @brief MQTT 底层消息回调：统一分发到各应用模块
 */
static void web_mqtt_manager_on_mqtt_message(const char    *topic,
                                             int            topic_len,
                                             const uint8_t *payload,
                                             int            payload_len)
{
    if (s_mgr_cfg.base_topic == NULL) {            ///< 未配置基础 Topic
        return;                                    ///< 不做分发
    }

    for (int i = 0; i < s_app_entry_count; ++i) {  ///< 遍历模块表
        char prefix[128];                          ///< 完整前缀缓冲区
        int  n = snprintf(prefix, sizeof(prefix),  ///< 生成前缀字符串
                          "%s/%s",               ///< base_topic/prefix
                          s_mgr_cfg.base_topic,    ///< 基础 Topic
                          s_app_entries[i].suffix);///< 模块前缀
        if (n <= 0 || n >= (int)sizeof(prefix)) {  ///< 生成失败或溢出
            continue;                              ///< 跳过本模块
        }

        int prefix_len = n;                        ///< 前缀长度
        if (topic_len < prefix_len) {              ///< Topic 过短
            continue;                              ///< 跳过
        }

        if (memcmp(topic, prefix, prefix_len) != 0) { ///< 前缀不匹配
            continue;                              ///< 跳过
        }

        if (topic_len > prefix_len &&              ///< 有后续字符时
            topic[prefix_len] != '/') {            ///< 要求分隔符为 '/'
            continue;                              ///< 否则视为不匹配
        }

        if (s_app_entries[i].cb) {                 ///< 存在回调
            (void)s_app_entries[i].cb(topic,       ///< 将消息转交模块
                                       topic_len,
                                       payload,
                                       payload_len);
        }
    }
}

/**
 * @brief MQTT 模块事件回调
 *
 * 由 mqtt_module 在底层连接状态变化时调用，用于驱动管理器状态机。
 */
static void web_mqtt_manager_on_mqtt_event(mqtt_module_event_t event)
{
    switch (event) {                               ///< 根据事件类型分类处理
    case MQTT_MODULE_EVENT_CONNECTED:              ///< 底层已连接
        ESP_LOGI(TAG, "MQTT connected");          ///< 打印日志
        web_mqtt_manager_notify_state(WEB_MQTT_STATE_CONNECTED); ///< 更新为已连接
        s_last_error_ts = 0;                       ///< 清空错误时间戳
        web_mqtt_manager_subscribe_all_apps();     ///< 为各模块订阅 Topic
        mqtt_reg_module_on_connected();            ///< 触发一次注册查询
        break;                                     ///< 结束分支

    case MQTT_MODULE_EVENT_DISCONNECTED:           ///< 底层断开
        ESP_LOGW(TAG, "MQTT disconnected");       ///< 打印日志
        web_mqtt_manager_notify_state(WEB_MQTT_STATE_DISCONNECTED); ///< 更新为断开
        s_last_error_ts = xTaskGetTickCount();     ///< 记录断开时间
        break;                                     ///< 结束分支

    case MQTT_MODULE_EVENT_ERROR:                  ///< 底层错误
    default:                                       ///< 其他视为错误
        ESP_LOGE(TAG, "MQTT error");             ///< 打印日志
        web_mqtt_manager_notify_state(WEB_MQTT_STATE_ERROR); ///< 更新为错误状态
        s_last_error_ts = xTaskGetTickCount();     ///< 记录错误时间
        break;                                     ///< 结束分支
    }
}

/**
 * @brief 单步执行 Web MQTT 管理状态机
 *
 * 根据当前状态与配置决定是否发起连接或等待重试。
 */
static void web_mqtt_manager_step(void)
{
    switch (s_mgr_state) {                         ///< 根据当前状态分类
    case WEB_MQTT_STATE_DISCONNECTED:              ///< 断开状态
    case WEB_MQTT_STATE_ERROR: {                   ///< 错误状态
        if (s_mgr_cfg.reconnect_interval_ms < 0) { ///< 小于 0 表示不自动重连
            break;                                 ///< 保持当前状态
        }

        TickType_t now   = xTaskGetTickCount();    ///< 当前 Tick
        TickType_t delta = (s_last_error_ts == 0)  ///< 若尚未记录
                               ? 0                 ///< 则视为已到时间
                               : (now - s_last_error_ts); ///< 与上次错误的间隔

        TickType_t need = pdMS_TO_TICKS(           ///< 计算需要等待的 Tick
            (s_mgr_cfg.reconnect_interval_ms <= 0) ///< 间隔 <=0 表示立即重连
                ? 0
                : s_mgr_cfg.reconnect_interval_ms);

        if (delta >= need) {                       ///< 已到达重试时间
            ESP_LOGI(TAG, "try connect MQTT server"); ///< 打印日志
            web_mqtt_manager_notify_state(WEB_MQTT_STATE_CONNECTING); ///< 进入连接中
            (void)mqtt_module_start();             ///< 发起一次连接尝试
            if (s_last_error_ts == 0) {           ///< 首次尝试时记录时间
                s_last_error_ts = now;            ///< 避免频繁重试
            }
        }
        break;                                     ///< 结束分支
    }

    case WEB_MQTT_STATE_CONNECTED:                 ///< 已连接状态
    case WEB_MQTT_STATE_READY:                     ///< 业务准备就绪
    default:                                       ///< 其他状态暂不做周期操作
        break;                                     ///< 保持静默
    }
}

/**
 * @brief Web MQTT 管理任务：周期性驱动状态机
 */
static void web_mqtt_manager_task(void *arg)
{
    (void)arg;                                     ///< 未使用参数

    for (;;) {                                     ///< 永久循环
        web_mqtt_manager_step();                   ///< 单步执行状态机

        int interval_ms = s_mgr_cfg.step_interval_ms; ///< 从配置读取间隔
        if (interval_ms <= 0) {                    ///< 若未配置或非法
            interval_ms = WEB_MQTT_MANAGER_STEP_INTERVAL_MS; ///< 使用默认值
        }

        vTaskDelay(pdMS_TO_TICKS(interval_ms));    ///< 休眠一段时间
    }
}

esp_err_t web_mqtt_manager_init(const web_mqtt_manager_config_t *config)
{
    /* 装载配置：优先使用上层配置，否则使用默认配置 */
    if (config == NULL) {                          ///< 未传入配置
        s_mgr_cfg = WEB_MQTT_MANAGER_DEFAULT_CONFIG(); ///< 使用默认配置
    } else {                                       ///< 传入了配置
        s_mgr_cfg = *config;                       ///< 直接保存一份副本
    }

    /* broker_uri 必须提供 */
    if (s_mgr_cfg.broker_uri == NULL ||            ///< URI 为空
        s_mgr_cfg.broker_uri[0] == '\0') {        ///< 或者空字符串
        return ESP_ERR_INVALID_ARG;                ///< 返回参数错误
    }

    /* 若未指定 client_id，则基于 MAC 生成一个默认 client_id */
    web_mqtt_manager_ensure_client_id();

    /* 组装 MQTT 模块配置 */
    mqtt_module_config_t mqtt_cfg = MQTT_MODULE_DEFAULT_CONFIG(); ///< 基础配置

    mqtt_cfg.broker_uri    = s_mgr_cfg.broker_uri; ///< 服务器 URI
    mqtt_cfg.client_id     = s_mgr_cfg.client_id;  ///< 客户端 ID
    mqtt_cfg.username      = s_mgr_cfg.username;   ///< 用户名
    mqtt_cfg.password      = s_mgr_cfg.password;   ///< 密码

    if (s_mgr_cfg.keepalive_sec > 0) {             ///< 如显式配置 keepalive
        mqtt_cfg.keepalive_sec = s_mgr_cfg.keepalive_sec; ///< 覆盖默认值
    }

    mqtt_cfg.event_cb      = web_mqtt_manager_on_mqtt_event; ///< 绑定事件回调
    mqtt_cfg.message_cb    = web_mqtt_manager_on_mqtt_message; ///< 绑定消息回调

    /* 初始化底层 MQTT 模块 */
    esp_err_t ret = mqtt_module_init(&mqtt_cfg);   ///< 调用底层初始化
    if (ret != ESP_OK) {                           ///< 初始化失败
        return ret;                                 ///< 直接返回错误码
    }

    /* 初始化内部应用模块（设备注册 + 心跳） */
    ret = mqtt_reg_module_init(&s_mgr_cfg);        ///< 初始化注册模块
    if (ret != ESP_OK) {                           ///< 初始化失败
        return ret;                                 ///< 直接返回错误码
    }

    ret = mqtt_heartbeat_module_init(&s_mgr_cfg);  ///< 初始化心跳模块
    if (ret != ESP_OK) {                           ///< 初始化失败
        return ret;                                 ///< 直接返回错误码
    }

    /* 初始化状态 */
    s_mgr_state     = WEB_MQTT_STATE_DISCONNECTED; ///< 初始设为断开
    s_last_error_ts = 0;                           ///< 清空错误时间戳

    /* 创建管理任务（仅创建一次） */
    if (s_mgr_task == NULL) {                      ///< 尚未创建任务
        BaseType_t ret_task = xTaskCreate(         ///< 创建 FreeRTOS 任务
            web_mqtt_manager_task,                 ///< 任务函数
            "web_mqtt_mgr",                      ///< 任务名
            4096,                                  ///< 栈大小
            NULL,                                  ///< 任务参数
            tskIDLE_PRIORITY + 1,                  ///< 优先级
            &s_mgr_task);                          ///< 输出任务句柄

        if (ret_task != pdPASS) {                  ///< 创建失败
            s_mgr_task = NULL;                     ///< 清空句柄
            return ESP_ERR_NO_MEM;                 ///< 返回内存不足
        }
    }

    /* 初始化完成后，可立即触发一次连接尝试 */
    web_mqtt_manager_notify_state(WEB_MQTT_STATE_DISCONNECTED); ///< 确认状态
    (void)mqtt_module_start();                     ///< 直接尝试连接一次

    return ESP_OK;                                 ///< 返回成功
}

esp_err_t web_mqtt_manager_register_app(const char *topic_suffix,
                                        web_mqtt_app_msg_cb_t cb)
{
    if (topic_suffix == NULL || cb == NULL) {      ///< 参数不可为空
        return ESP_ERR_INVALID_ARG;                ///< 返回参数错误
    }

    if (topic_suffix[0] == '\0') {                ///< 前缀不能为空串
        return ESP_ERR_INVALID_ARG;                ///< 返回参数错误
    }

    if (s_app_entry_count >= WEB_MQTT_APP_MAX_NUM) { ///< 模块数量已达上限
        return ESP_ERR_NO_MEM;                     ///< 返回内存不足
    }

    size_t len = strlen(topic_suffix);             ///< 计算前缀长度
    if (len >= WEB_MQTT_APP_SUFFIX_MAX_LEN) {      ///< 超出可用空间
        return ESP_ERR_INVALID_ARG;                ///< 返回参数错误
    }

    for (int i = 0; i < s_app_entry_count; ++i) {  ///< 查找是否已存在
        if (strcmp(s_app_entries[i].suffix, topic_suffix) == 0) {
            s_app_entries[i].cb = cb;              ///< 更新回调
            return ESP_OK;                         ///< 返回成功
        }
    }

    strcpy(s_app_entries[s_app_entry_count].suffix, topic_suffix); ///< 保存前缀
    s_app_entries[s_app_entry_count].cb = cb;       ///< 保存回调
    s_app_entry_count++;                            ///< 模块数量加一

    /* 若 MQTT 已连接，则立即为该模块订阅一次 */
    if (s_mgr_cfg.base_topic != NULL &&
        (s_mgr_state == WEB_MQTT_STATE_CONNECTED ||
         s_mgr_state == WEB_MQTT_STATE_READY)) {
        char filter[128];                          ///< 订阅过滤字符串
        snprintf(filter, sizeof(filter), "%s/%s/#", ///< base_topic/prefix/#
                 s_mgr_cfg.base_topic,             ///< 基础 Topic
                 topic_suffix);                    ///< 模块前缀
        (void)mqtt_module_subscribe(filter, 1);    ///< 订阅，忽略返回值
    }

    return ESP_OK;                                  ///< 返回成功
}

const char *web_mqtt_manager_get_client_id(void)
{
    return s_mgr_cfg.client_id;
}

const char *web_mqtt_manager_get_base_topic(void)
{
    return s_mgr_cfg.base_topic;
}
