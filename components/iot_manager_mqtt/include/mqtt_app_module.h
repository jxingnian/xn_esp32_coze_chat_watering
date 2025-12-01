/*
 * @Author: 星年 && jixingnian@gmail.com
 * @Date: 2025-11-24 13:50:00
 * @LastEditors: xingnian && jixingnian@gmail.com
 * @LastEditTime: 2025-11-24 13:50:00
 * @FilePath: \xn_web_mqtt_manager\components\iot_manager_mqtt\include\mqtt_app_module.h
 * @Description: Web MQTT 应用模块注册与消息分发接口
 *
 * 设计目标：
 *  - 由 web_mqtt_manager 统一接收 MQTT 消息；
 *  - 按 base_topic + 模块前缀 将消息分发给各个“应用模块”；
 *  - 方便在 main 目录或组件内部新增模块（如设备注册、WiFi 配置等）。
 */

#ifndef MQTT_APP_MODULE_H
#define MQTT_APP_MODULE_H

#include <stdint.h>

#include "esp_err.h"
#include "web_mqtt_manager.h"  ///< 复用管理器中的状态定义

/**
 * @brief 应用模块消息回调
 *
 * 所有经管理器分发的 MQTT 消息都会通过该回调送达模块：
 *  - topic 为完整 Topic 指针（不保证以 '\0' 结尾）；
 *  - topic_len 为 Topic 字节长度；
 *  - payload / payload_len 为原始二进制负载。
 */
typedef esp_err_t (*web_mqtt_app_msg_cb_t)(const char    *topic,
                                           int            topic_len,
                                           const uint8_t *payload,
                                           int            payload_len);

/**
 * @brief 在 Web MQTT 管理器中注册一个应用模块消息处理回调
 *
 * 管理器内部会维护一个简单的“路由表”，按如下规则匹配并分发消息：
 *  - 实际 Topic 形如： base_topic "/" topic_suffix "/..."；
 *  - 只要前缀 "base_topic/topic_suffix" 匹配，即调用对应回调；
 *  - 这样可以很方便地按模块前缀拆分：例如 "reg" / "hb" / "wifi_cfg" 等。
 *
 * @param topic_suffix 模块的 Topic 前缀（不含 base_topic 和前导 '/'），如 "reg"；
 * @param cb           模块的消息处理回调，不可为 NULL。
 *
 * @return
 *  - ESP_OK             : 注册成功
 *  - ESP_ERR_INVALID_ARG: 参数非法
 *  - ESP_ERR_NO_MEM    : 模块数量已达上限
 */
esp_err_t web_mqtt_manager_register_app(const char *topic_suffix,
                                        web_mqtt_app_msg_cb_t cb);

#endif /* MQTT_APP_MODULE_H */
