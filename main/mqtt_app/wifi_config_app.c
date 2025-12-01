/*
 * @FilePath: \xn_esp32_web_mqtt_manager\main\mqtt_app\wifi_config_app.c
 * @Description: 通过 MQTT 远程配置 WiFi 的应用模块
 *
 * 职责：
 *  - 订阅 Web 下发的 WiFi 相关 Topic（基于 base_topic = "xn/web"）：
 *      - xn/web/wifi/<device_id>/set            下发一组新的 WiFi 配置（ssid/password）
 *      - xn/web/wifi/<device_id>/get_status     请求当前 WiFi 连接状态
 *      - xn/web/wifi/<device_id>/get_saved      请求已保存 WiFi 列表
 *      - xn/web/wifi/<device_id>/connect_saved  请求切换到某个已保存 WiFi
 *  - 将结果通过上行前缀 WEB_MQTT_UPLINK_BASE_TOPIC（"xn/esp"）回报给服务器：
 *      - xn/esp/wifi/<device_id>/status         JSON 格式的当前 WiFi 状态
 *      - xn/esp/wifi/<device_id>/saved          JSON 格式的已保存 WiFi 列表
 *
 * 该模块只负责“命令解析 + 调用底层 WiFi/存储模块 + 通过 MQTT 上报”，
 * 不直接维护状态机，保持与 wifi_manage 模块的解耦。
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "wifi_module.h"
#include "storage_module.h"
#include "mqtt_module.h"
#include "mqtt_app_module.h"
#include "web_mqtt_manager.h"
#include "mqtt_app/wifi_config_app.h"

static const char *TAG = "wifi_cfg_app";              ///< 本模块日志 TAG

/* -------------------- 内部工具：发布 JSON 到指定 WiFi 子 Topic -------------------- */
/**
 * @brief 辅助函数：以 JSON 形式向 xn/esp/wifi/<device_id>/<sub> 发布上行消息
 */
static void wifi_cfg_publish_json(const char *sub, const char *json)
{
    const char *client_id = web_mqtt_manager_get_client_id();
    if (client_id == NULL || client_id[0] == '\0' || sub == NULL || json == NULL) {
        return;
    }

    char topic[128];
    int  n = snprintf(topic,
                      sizeof(topic),
                      "%s/wifi/%s/%s",
                      WEB_MQTT_UPLINK_BASE_TOPIC,
                      client_id,
                      sub);
    if (n <= 0 || n >= (int)sizeof(topic)) {
        return;
    }

    (void)mqtt_module_publish(topic, json, (int)strlen(json), 1, false);
}

/* -------------------- 处理命令：下发新 WiFi 配置 -------------------- */
/**
 * @brief 从 payload 中解析 ssid/password，并调用 wifi_module_connect 提交连接
 */
static void wifi_cfg_handle_set(const char *payload, int payload_len)
{
    if (payload == NULL || payload_len <= 0) {
        return;
    }

    if (payload_len >= 256) {
        payload_len = 255;   ///< 限制到本地缓冲区大小
    }

    char buf[256];
    memcpy(buf, payload, (size_t)payload_len);
    buf[payload_len] = '\0';

    const char *ssid     = NULL;
    const char *password = NULL;

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

        if (strncmp(line, "ssid=", 5) == 0) {
            ssid = line + 5;
        } else if (strncmp(line, "password=", 9) == 0) {
            password = line + 9;
        }

        if (next == NULL) {
            break;
        }
        line = next + 1;
    }

    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGW(TAG, "wifi cfg: missing ssid");
        return;
    }

    const char *pwd = (password != NULL && password[0] != '\0') ? password : NULL;

    esp_err_t ret = wifi_module_connect(ssid, pwd);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "wifi cfg: try connect, ssid=%s", ssid);
    } else {
        ESP_LOGW(TAG, "wifi cfg: connect submit failed, err=%d", (int)ret);
    }
}

/* -------------------- 处理命令：上报当前 WiFi 状态 -------------------- */
/**
 * @brief 读取当前 WiFi 连接状态并通过 MQTT 上报 JSON
 */
static void wifi_cfg_handle_get_status(void)
{
    bool   connected = false;
    char   ssid[32]  = "-";
    char   ip[16]    = "-";
    int8_t rssi      = 0;
    char   mode[8]   = "-";

    /* 判断是否已连接：能成功获取 AP 信息且 SSID 非空则认为已连接 */
    wifi_ap_record_t ap_info = {0};
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK && ap_info.ssid[0] != '\0') {
        connected = true;
        strncpy(ssid, (const char *)ap_info.ssid, sizeof(ssid));
        ssid[sizeof(ssid) - 1] = '\0';
        rssi = ap_info.rssi;
    }

    /* 获取当前 STA IP 地址 */
    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif != NULL) {
        esp_netif_ip_info_t ip_info = {0};
        if (esp_netif_get_ip_info(sta_netif, &ip_info) == ESP_OK) {
            snprintf(ip, sizeof(ip), IPSTR, IP2STR(&ip_info.ip));
        }
    }

    /* 获取当前 WiFi 工作模式字符串 */
    wifi_mode_t wifi_mode = WIFI_MODE_NULL;
    if (esp_wifi_get_mode(&wifi_mode) == ESP_OK) {
        const char *mode_str = "-";
        switch (wifi_mode) {
        case WIFI_MODE_STA:
            mode_str = "STA";
            break;
        case WIFI_MODE_AP:
            mode_str = "AP";
            break;
        case WIFI_MODE_APSTA:
            mode_str = "AP+STA";
            break;
        default:
            break;
        }
        strncpy(mode, mode_str, sizeof(mode));
        mode[sizeof(mode) - 1] = '\0';
    }

    /* 组装简单 JSON（不依赖额外 JSON 库） */
    char json[256];
    snprintf(json,
             sizeof(json),
             "{\"connected\":%s,\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"mode\":\"%s\"}",
             connected ? "true" : "false",
             ssid,
             ip,
             (int)rssi,
             mode);

    wifi_cfg_publish_json("status", json);
}

/* -------------------- 处理命令：上报已保存 WiFi 列表 -------------------- */
/**
 * @brief 读取存储模块中的已保存 WiFi 列表并通过 MQTT 上报 JSON
 */
static void wifi_cfg_handle_get_saved(void)
{
    uint8_t max_num = 5;
    wifi_config_t *configs = (wifi_config_t *)malloc(max_num * sizeof(wifi_config_t));
    if (configs == NULL) {
        return;
    }

    uint8_t count = 0;
    if (wifi_storage_load_all(configs, &count) != ESP_OK || count == 0) {
        free(configs);
        wifi_cfg_publish_json("saved", "{\"list\":[]}");
        return;
    }

    if (count > max_num) {
        count = max_num;
    }

    /* 简单构造 [{"ssid":"..."},...] 列表 */
    char json[512];
    size_t pos = 0;
    pos += snprintf(json + pos, sizeof(json) - pos, "{\"list\":[");

    for (uint8_t i = 0; i < count && pos < sizeof(json) - 1; i++) {
        char ssid[32];
        strncpy(ssid, (const char *)configs[i].sta.ssid, sizeof(ssid));
        ssid[sizeof(ssid) - 1] = '\0';
        if (ssid[0] == '\0') {
            continue;
        }

        if (pos > 10) { /* 非首项时添加逗号 */
            pos += snprintf(json + pos, sizeof(json) - pos, ",");
        }
        pos += snprintf(json + pos,
                        sizeof(json) - pos,
                        "{\"ssid\":\"%s\"}",
                        ssid);
    }

    snprintf(json + pos, sizeof(json) - pos, "]}");

    free(configs);

    wifi_cfg_publish_json("saved", json);
}

/* -------------------- 处理命令：切换到已保存 WiFi -------------------- */
/**
 * @brief 解析 payload 中的 ssid=...，在存储列表中提升优先级并触发断开重连
 */
static void wifi_cfg_handle_connect_saved(const char *payload, int payload_len)
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

    const char *ssid = NULL;
    if (strncmp(buf, "ssid=", 5) == 0) {
        ssid = buf + 5;
    }

    if (ssid == NULL || ssid[0] == '\0') {
        ESP_LOGW(TAG, "wifi cfg: connect_saved missing ssid");
        return;
    }

    /* 读取当前已保存列表，找到对应 SSID */
    uint8_t max_num = 5;
    wifi_config_t *configs = (wifi_config_t *)malloc(max_num * sizeof(wifi_config_t));
    if (configs == NULL) {
        return;
    }

    uint8_t   count = 0;
    esp_err_t ret   = wifi_storage_load_all(configs, &count);
    if (ret != ESP_OK || count == 0) {
        free(configs);
        return;
    }

    int found_index = -1;
    for (uint8_t i = 0; i < count; i++) {
        if (configs[i].sta.ssid[0] == '\0') {
            continue;
        }
        if (strncmp((const char *)configs[i].sta.ssid,
                    ssid,
                    sizeof(configs[i].sta.ssid)) == 0) {
            found_index = (int)i;
            break;
        }
    }

    if (found_index < 0) {
        free(configs);
        ESP_LOGW(TAG, "wifi cfg: ssid not found in saved list: %s", ssid);
        return;
    }

    /* 提升优先级并触发一次断开，让 wifi_manage 状态机按新顺序重连 */
    ret = wifi_storage_on_connected(&configs[found_index]);
    free(configs);

    if (ret == ESP_OK) {
        (void)esp_wifi_disconnect();
        ESP_LOGI(TAG, "wifi cfg: request reconnect to saved ssid=%s", ssid);
    } else {
        ESP_LOGW(TAG, "wifi cfg: wifi_storage_on_connected failed, err=%d", (int)ret);
    }
}

/* -------------------- MQTT 回调：统一解析 WiFi 相关指令 -------------------- */
/**
 * @brief MQTT 应用模块回调：解析 WiFi 指令并分发到对应处理函数
 */
static esp_err_t wifi_config_app_on_message(const char    *topic,
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
    int  n = snprintf(prefix, sizeof(prefix), "%s/wifi/", base_topic);
    if (n <= 0 || n >= (int)sizeof(prefix)) {
        return ESP_OK;
    }

    if (topic_len <= n) {
        return ESP_OK;
    }

    if (memcmp(topic, prefix, (size_t)n) != 0) {
        return ESP_OK;   ///< 非 "<base>/wifi/..." 前缀，忽略
    }

    const char *rest     = topic + n;
    int         rest_len = topic_len - n;

    /* rest 形如："<client_id>/set" / "<client_id>/get_status" 等 */
    size_t id_len = strlen(client_id);
    if (rest_len <= (int)id_len || strncmp(rest, client_id, id_len) != 0) {
        return ESP_OK;   ///< Topic 中携带的 device_id 不匹配本设备，忽略
    }

    if (rest_len <= (int)id_len + 1 || rest[id_len] != '/') {
        return ESP_OK;
    }

    const char *cmd     = rest + id_len + 1;           ///< 指向 "set" / "get_status" 等
    int         cmd_len = rest_len - (int)id_len - 1;

    /* 根据命令后缀分发处理逻辑 */
    if (cmd_len == 3 && strncmp(cmd, "set", 3) == 0) {
        /* 下发新 WiFi 配置 */
        wifi_cfg_handle_set((const char *)payload, payload_len);
    } else if (cmd_len == 10 && strncmp(cmd, "get_status", 10) == 0) {
        /* 请求当前 WiFi 状态 */
        wifi_cfg_handle_get_status();
    } else if (cmd_len == 9 && strncmp(cmd, "get_saved", 9) == 0) {
        /* 请求已保存 WiFi 列表 */
        wifi_cfg_handle_get_saved();
    } else if (cmd_len == 13 && strncmp(cmd, "connect_saved", 13) == 0) {
        /* 请求切换到已保存的某个 WiFi，payload 中携带 ssid=... */
        wifi_cfg_handle_connect_saved((const char *)payload, payload_len);
    }

    return ESP_OK;
}

esp_err_t wifi_config_app_init(void)
{
    /* 注册到 Web MQTT 管理器，使用模块前缀 "wifi" */
    return web_mqtt_manager_register_app("wifi", wifi_config_app_on_message);
}
