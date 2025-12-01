/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 13:54:00
 * @LastEditors: xingnian && jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 13:54:00
 * @FilePath: \xn_web_mqtt_manager\components\iot_manager_mqtt\src\mqtt_heartbeat_module.c
 * @Description: 心跳模块实现
 *
 * 功能概述：
 *  - 周期性检查设备是否已注册；
 *  - 若已注册，则按固定间隔向服务器发送心跳包；
 *  - 心跳 Topic 默认为 base_topic+"/hb"，负载使用设备 ID。
 */

#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"

#include "mqtt_module.h"
#include "mqtt_reg_module.h"
#include "mqtt_heartbeat_module.h"

/* 日志 TAG */
static const char *TAG = "mqtt_hb";               ///< 本模块日志 TAG

static const web_mqtt_manager_config_t *s_mgr_cfg = NULL; ///< 管理器配置指针
static TaskHandle_t                      s_hb_task = NULL; ///< 心跳任务句柄

#define MQTT_HEARTBEAT_INTERVAL_MS 30000           ///< 心跳间隔（ms），示例为 30 秒

/**
 * @brief 获取设备唯一标识
 *
 * 与注册模块保持一致，使用 client_id 作为设备 ID。
 */
static const char *mqtt_hb_get_device_id(void)
{
    if (s_mgr_cfg && s_mgr_cfg->client_id && s_mgr_cfg->client_id[0] != '\0') {
        return s_mgr_cfg->client_id;               ///< 使用上层配置 client_id
    }
    return "unknown_device";                      ///< 占位设备 ID
}

/**
 * @brief 心跳任务主体
 */
static void mqtt_hb_task(void *arg)
{
    (void)arg;                                     ///< 未使用参数

    if (s_mgr_cfg == NULL || s_mgr_cfg->base_topic == NULL) { ///< 未正确初始化
        vTaskDelete(NULL);                          ///< 删除任务
    }

    char topic[128];                                ///< Topic 缓冲区
    const char *device_id = mqtt_hb_get_device_id(); ///< 设备 ID 字符串

    /* 预构造心跳 Topic: base_topic + "/hb" */
    snprintf(topic, sizeof(topic), "%s/hb", WEB_MQTT_UPLINK_BASE_TOPIC);

    for (;;) {                                     ///< 永久循环
        vTaskDelay(pdMS_TO_TICKS(MQTT_HEARTBEAT_INTERVAL_MS)); ///< 按间隔休眠

        if (!mqtt_reg_module_is_registered()) {    ///< 未注册则跳过本轮
            continue;                               ///< 等待下一次
        }

        ESP_LOGI(TAG, "send heartbeat, id=%s, topic=%s", ///< 打印日志
                 device_id, topic);                ///< 设备 ID 与 Topic

        (void)mqtt_module_publish(topic,           ///< 心跳 Topic
                                   device_id,      ///< 负载为设备 ID
                                   (int)strlen(device_id), ///< 负载长度
                                   1,              ///< QoS 1 示例
                                   false);         ///< 不保留
    }
}

esp_err_t mqtt_heartbeat_module_init(const web_mqtt_manager_config_t *mgr_cfg)
{
    if (mgr_cfg == NULL) {                         ///< 管理器配置不可为空
        return ESP_ERR_INVALID_ARG;                 ///< 返回参数错误
    }

    s_mgr_cfg = mgr_cfg;                           ///< 保存配置指针

    if (s_hb_task != NULL) {                       ///< 若任务已创建
        return ESP_OK;                              ///< 直接视为成功
    }

    BaseType_t ret = xTaskCreate(                  ///< 创建心跳任务
        mqtt_hb_task,                              ///< 任务函数
        "mqtt_hb",                               ///< 任务名
        4096,                                      ///< 栈大小
        NULL,                                      ///< 参数
        tskIDLE_PRIORITY + 1,                      ///< 优先级
        &s_hb_task);                               ///< 任务句柄

    if (ret != pdPASS) {                           ///< 创建失败
        s_hb_task = NULL;                          ///< 清空句柄
        return ESP_ERR_NO_MEM;                     ///< 返回内存不足
    }

    return ESP_OK;                                  ///< 返回成功
}
