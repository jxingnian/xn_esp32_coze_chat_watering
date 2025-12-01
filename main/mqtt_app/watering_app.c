/*
 * @FilePath: \xn_esp32_coze_chat_watering\main\mqtt_app\watering_app.c
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
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

typedef struct {
    bool enabled;
    int  days_mask;  /* bit0=Mon ... bit6=Sun */
    int  hour;
    int  minute;
    int  duration_s;
} watering_plan_t;

static watering_plan_t s_plan      = { false, 0x7F, 8, 0, 10 };
static TaskHandle_t    s_plan_task = NULL;

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

static void watering_publish_plan(void)
{
    const char *client_id = web_mqtt_manager_get_client_id();
    if (client_id == NULL || client_id[0] == '\0') {
        return;
    }

    char topic[128];
    int  n = snprintf(topic,
                      sizeof(topic),
                      "%s/watering/%s/plan",
                      WEB_MQTT_UPLINK_BASE_TOPIC,
                      client_id);
    if (n <= 0 || n >= (int)sizeof(topic)) {
        return;
    }

    watering_plan_t plan = s_plan;

    char json[160];
    snprintf(json,
             sizeof(json),
             "{\"enabled\":%s,\"days_mask\":%d,\"hour\":%d,\"minute\":%d,\"duration_s\":%d}",
             plan.enabled ? "true" : "false",
             plan.days_mask,
             plan.hour,
             plan.minute,
             plan.duration_s);

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

static void watering_plan_task(void *arg)
{
    (void)arg;

    for (;;) {
        watering_plan_t plan = s_plan;

        if (!plan.enabled || plan.duration_s <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        time_t    now = 0;
        struct tm tm_now;
        memset(&tm_now, 0, sizeof(tm_now));

        if (time(&now) < 0 || localtime_r(&now, &tm_now) == NULL) {
            vTaskDelay(pdMS_TO_TICKS(60000));
            continue;
        }

        int week_min = 7 * 24 * 60;

        int now_wday = tm_now.tm_wday;
        if (now_wday < 0 || now_wday > 6) {
            now_wday = 0;
        }

        int now_total_min = now_wday * 24 * 60 + tm_now.tm_hour * 60 + tm_now.tm_min;

        int days_mask = plan.days_mask;
        if (days_mask <= 0) {
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        int plan_hour = plan.hour;
        if (plan_hour < 0) {
            plan_hour = 0;
        }
        if (plan_hour > 23) {
            plan_hour = 23;
        }

        int plan_minute = plan.minute;
        if (plan_minute < 0) {
            plan_minute = 0;
        }
        if (plan_minute > 59) {
            plan_minute = 59;
        }

        int best_diff_min = week_min;

        for (int d = 1; d <= 7; d++) {
            if ((days_mask & (1 << (d - 1))) == 0) {
                continue;
            }

            int plan_total_min = (d - 1) * 24 * 60 + plan_hour * 60 + plan_minute;
            int diff_min;
            if (plan_total_min > now_total_min) {
                diff_min = plan_total_min - now_total_min;
            } else {
                diff_min = week_min - (now_total_min - plan_total_min);
            }

            if (diff_min < best_diff_min) {
                best_diff_min = diff_min;
            }
        }

        if (best_diff_min <= 0 || best_diff_min > week_min) {
            best_diff_min = 1;
        }

        int64_t diff_ms = (int64_t)best_diff_min * 60 * 1000;
        if (diff_ms <= 0) {
            diff_ms = 1000;
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)diff_ms));

        plan = s_plan;
        if (!plan.enabled || plan.duration_s <= 0) {
            continue;
        }

        watering_set_state(true);

        int64_t dur_ms = (int64_t)plan.duration_s * 1000;
        if (dur_ms <= 0) {
            dur_ms = 1000;
        }

        vTaskDelay(pdMS_TO_TICKS((uint32_t)dur_ms));

        watering_set_state(false);
    }
}

static void watering_ensure_plan_task(void)
{
    if (s_plan_task != NULL) {
        return;
    }

    BaseType_t ret = xTaskCreate(watering_plan_task,
                                 "watering_plan",
                                 2048,
                                 NULL,
                                 tskIDLE_PRIORITY + 1,
                                 &s_plan_task);
    if (ret != pdPASS) {
        s_plan_task = NULL;
    }
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

static void watering_handle_set_plan(const uint8_t *payload, int payload_len)
{
    if (payload == NULL || payload_len <= 0) {
        return;
    }

    if (payload_len >= 128) {
        payload_len = 127;
    }

    char buf[128];
    memcpy(buf, payload, (size_t)payload_len);
    buf[payload_len] = '\0';

    watering_plan_t plan = s_plan;

    char *line = buf;
    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        if (next != NULL) {
            *next = '\0';
        }

        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\r' || line[len - 1] == ' ')) {
            line[len - 1] = '\0';
            len--;
        }

        if (strncmp(line, "enabled=", 8) == 0) {
            int v = atoi(line + 8);
            plan.enabled = (v != 0);
        } else if (strncmp(line, "days_mask=", 10) == 0) {
            int v = atoi(line + 10);
            if (v < 0) {
                v = 0;
            }
            if (v > 0x7F) {
                v = 0x7F;
            }
            plan.days_mask = v;
        } else if (strncmp(line, "weekday=", 8) == 0) {
            int v = atoi(line + 8);
            if (v < 1) {
                v = 1;
            }
            if (v > 7) {
                v = 7;
            }
            plan.days_mask = (1 << (v - 1));
        } else if (strncmp(line, "hour=", 5) == 0) {
            int v = atoi(line + 5);
            if (v < 0) {
                v = 0;
            }
            if (v > 23) {
                v = 23;
            }
            plan.hour = v;
        } else if (strncmp(line, "minute=", 7) == 0) {
            int v = atoi(line + 7);
            if (v < 0) {
                v = 0;
            }
            if (v > 59) {
                v = 59;
            }
            plan.minute = v;
        } else if (strncmp(line, "duration_s=", 11) == 0) {
            int v = atoi(line + 11);
            if (v < 1) {
                v = 1;
            }
            if (v > 600) {
                v = 600;
            }
            plan.duration_s = v;
        }

        if (next == NULL) {
            break;
        }
        line = next + 1;
    }

    s_plan = plan;

    watering_publish_plan();
    watering_ensure_plan_task();
}

static void watering_handle_get_plan(void)
{
    watering_publish_plan();
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
    } else if (cmd_len == 8 && strncmp(cmd, "set_plan", 8) == 0) {
        watering_handle_set_plan(payload, payload_len);
    } else if (cmd_len == 8 && strncmp(cmd, "get_plan", 8) == 0) {
        watering_handle_get_plan();
    }

    return ESP_OK;
}

esp_err_t watering_app_init(void)
{
    watering_gpio_init();
    watering_set_state(false);

    return web_mqtt_manager_register_app("watering", watering_app_on_message);
}
