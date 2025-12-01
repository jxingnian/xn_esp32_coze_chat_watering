/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 13:51:00
 * @LastEditors: xingnian && jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 13:51:00
 * @FilePath: \xn_web_mqtt_manager\components\iot_manager_mqtt\include\mqtt_reg_module.h
 * @Description: 设备注册模块接口（查询是否注册 + 触发注册）
 */

#ifndef MQTT_REG_MODULE_H
#define MQTT_REG_MODULE_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "web_mqtt_manager.h"

/**
 * @brief 初始化设备注册模块
 *
 * 由 Web MQTT 管理器在初始化阶段调用一次：
 *  - 保存 base_topic / client_id 等必要信息；
 *  - 在管理器中注册消息回调（当前使用前缀 "reg"）。
 */
esp_err_t mqtt_reg_module_init(const web_mqtt_manager_config_t *mgr_cfg);

/**
 * @brief 在 MQTT 连接建立后触发一次“查询是否已注册”
 *
 * 由 Web MQTT 管理器在收到 MQTT_MODULE_EVENT_CONNECTED 时调用。
 */
void mqtt_reg_module_on_connected(void);

/**
 * @brief 设备是否已经完成注册
 */
bool mqtt_reg_module_is_registered(void);

#endif /* MQTT_REG_MODULE_H */
