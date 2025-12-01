/*
 * @FilePath: \xn_esp32_web_mqtt_manager\main\mqtt_app\watering_app.c
 * @Description: 通过 MQTT 远程控制浇花电机的应用模块
 *
 * 职责：
 *  - 订阅 Web 下发的浇花相关 Topic（基于 base_topic = "xn/web"）：
 *      - xn/web/watering/<device_id>/set        下发浇花开关命令（payload: "on" / "off"）
 *      - xn/web/watering/<device_id>/get_status 请求当前浇花开关状态
 *  - 将结果通过上行前缀 WEB_MQTT_UPLINK_BASE_TOPIC（"xn/esp"）回报给服务器：
 *      - xn/esp/watering/<device_id>/status     JSON 格式的当前浇花状态
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include "esp_log.h"
#include "driver/gpio.h"

#include "mqtt_module.h"
#include "web_mqtt_manager.h"
#include "mqtt_app_module.h"
#include "mqtt_app/watering_app.h"

static const char *TAG = "watering_app";

/* 浇花电机控制 GPIO，可按实际硬件修改 */
#ifndef WATERING_MOTOR_GPIO
#define WATERING_MOTOR_GPIO GPIO_NUM_4
#endif

static bool s_gpio_inited  = false;
static bool s_watering_on  = false;

static void watering_gpio_init(void)
{
    if (s_gpio_inited) {
        return;
    }

    gpio_config_t io_conf = {0};
    io_conf.intr_type    = GPIO_INTR_DISABLE;
    io_conf.mode         = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = 1ULL << WATERING_MOTOR_GPIO;
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en   = 0;
    gpio_config(&io_conf);

    gpio_set_level(WATERING_MOTOR_GPIO, 0);
    s_gpio_inited = true;
}

static void watering_publish_status(void)
{
    const char *client_id = web_mqtt_manager_get_client_id();
    if (client_id == NULL || client_id[0] == '\0') {
        return;
    }

    char topic[128];
    int  n = snprintf(topic,
                      sizeof(topic),
                      "%s/watering/%s/status",
                      WEB_MQTT_UPLINK_BASE_TOPIC,
                      client_id);
    if (n <= 0 || n >= (int)sizeof(topic)) {
        return;
    }

    char json[64];
    snprintf(json,
             sizeof(json),
             "{\"on\":%s}",
             s_watering_on ? "true" : "false");

    (void)mqtt_module_publish(topic, json, (int)strlen(json), 1, false);
}

static void watering_set_state(bool on)
{
    watering_gpio_init();

    gpio_set_level(WATERING_MOTOR_GPIO, on ? 1 : 0);
    s_watering_on = on;

    ESP_LOGI(TAG, "set watering %s", on ? "ON" : "OFF");

    watering_publish_status();
}

static void watering_handle_set(const uint8_t *payload, int payload_len)
{
    if (payload == NULL || payload_len <= 0) {
        return;
    }

    if (payload_len >= 16) {
        payload_len = 15;
    }

    char buf[16];
    memcpy(buf, payload, (size_t)payload_len);
    buf[payload_len] = '\0';

    int len = (int)strlen(buf);
    while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n' || buf[len - 1] == ' ')) {
        buf[len - 1] = '\0';
        len--;
    }

    for (int i = 0; i < len; i++) {
        if (buf[i] >= 'A' && buf[i] <= 'Z') {
            buf[i] = (char)(buf[i] - 'A' + 'a');
        }
    }

    if (strcmp(buf, "on") == 0) {
        watering_set_state(true);
    } else if (strcmp(buf, "off") == 0) {
        watering_set_state(false);
    } else {
        ESP_LOGW(TAG, "unknown watering cmd: %s", buf);
    }
}

static void watering_handle_get_status(void)
{
    watering_publish_status();
}

static esp_err_t watering_app_on_message(const char    *topic,
                                         int            topic_len,
                                         const uint8_t *payload,
                                         int            payload_len)
{
    const char *base_topic = web_mqtt_manager_get_base_topic();
    const char *client_id  = web_mqtt_manager_get_client_id();

    if (base_topic == NULL || base_topic[0] == '\0' ||
        client_id == NULL || client_id[0] == '\0' ||
        topic == NULL || topic_len <= 0) {
        return ESP_OK;
    }

    char prefix[128];
    int  n = snprintf(prefix, sizeof(prefix), "%s/watering/", base_topic);
    if (n <= 0 || n >= (int)sizeof(prefix)) {
        return ESP_OK;
    }

    if (topic_len <= n) {
        return ESP_OK;
    }

    if (memcmp(topic, prefix, (size_t)n) != 0) {
        return ESP_OK;
    }

    const char *rest     = topic + n;
    int         rest_len = topic_len - n;

    size_t id_len = strlen(client_id);
    if (rest_len <= (int)id_len || strncmp(rest, client_id, id_len) != 0) {
        return ESP_OK;
    }

    if (rest_len <= (int)id_len + 1 || rest[id_len] != '/') {
        return ESP_OK;
    }

    const char *cmd     = rest + id_len + 1;
    int         cmd_len = rest_len - (int)id_len - 1;

    if (cmd_len == 3 && strncmp(cmd, "set", 3) == 0) {
        watering_handle_set(payload, payload_len);
    } else if (cmd_len == 10 && strncmp(cmd, "get_status", 10) == 0) {
        watering_handle_get_status();
    }

    return ESP_OK;
}

esp_err_t watering_app_init(void)
{
    watering_gpio_init();
    watering_set_state(false);

    return web_mqtt_manager_register_app("watering", watering_app_on_message);
}
