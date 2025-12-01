/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 13:52:00
 * @LastEditors: xingnian && jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 13:52:00
 * @FilePath: \xn_web_mqtt_manager\components\iot_manager_mqtt\include\mqtt_heartbeat_module.h
 * @Description: 心跳模块接口（根据注册状态定时上报心跳）
 */

#ifndef MQTT_HEARTBEAT_MODULE_H
#define MQTT_HEARTBEAT_MODULE_H

#include "esp_err.h"
#include "web_mqtt_manager.h"

/**
 * @brief 初始化心跳模块
 *
 * 由 Web MQTT 管理器在初始化阶段调用一次：
 *  - 保存 base_topic / client_id 等必要信息；
 *  - 创建内部心跳任务，按固定间隔检查注册状态并发送心跳。
 */
esp_err_t mqtt_heartbeat_module_init(const web_mqtt_manager_config_t *mgr_cfg);

#endif /* MQTT_HEARTBEAT_MODULE_H */
