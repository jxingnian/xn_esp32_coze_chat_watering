/*
 * @Author: 星年 jixingnian@gmail.com
 * @Date: 2025-11-22 13:43:50
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-12-01 14:32:48
 * @FilePath: \xn_esp32_coze_chat_watering\main\main.c
 * @Description: esp32 网页WiFi配网 By.星年
 */

#include <stdio.h>
#include <inttypes.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_err.h"
#include "xn_wifi_manage.h"
#include "audio_manager.h"
#include "coze_chat.h"
#include "coze_chat_app.h"
#include "audio_app/audio_config_app.h"
#include "lottie_app/lottie_app.h"
#include "web_mqtt_manager.h"
#include "mqtt_app/wifi_config_app.h"
#include "mqtt_app/watering_app.h"

static const char *TAG = "app";

extern coze_chat_handle_t coze_chat_get_handle(void);

static bool s_coze_started = false;
static bool s_mqtt_inited  = false;

// 统计当前轮对话已上行的采样点数，用于在超时场景下决定 complete/cancel
static size_t s_uplink_samples_this_turn = 0;

static void app_mqtt_event_cb(web_mqtt_state_t state);

static void app_wifi_event_cb(wifi_manage_state_t state)
{
    switch (state) {
    case WIFI_MANAGE_STATE_CONNECTED:
        if (!s_mqtt_inited) {
            ESP_LOGI(TAG, "WiFi connected, init Coze chat");

            if (coze_chat_app_init() == ESP_OK) {
                s_coze_started = true;
                lottie_app_show_mic_idle();
            } else {
                ESP_LOGE(TAG, "Coze chat init failed on WiFi connect");
            }
            
            web_mqtt_manager_config_t mqtt_cfg = WEB_MQTT_MANAGER_DEFAULT_CONFIG();
            mqtt_cfg.broker_uri = "mqtt://120.55.96.194:1883";
            mqtt_cfg.base_topic = "xn/web";
            mqtt_cfg.event_cb   = app_mqtt_event_cb;

            esp_err_t ret_mqtt = web_mqtt_manager_init(&mqtt_cfg);
            (void)ret_mqtt;

            (void)wifi_config_app_init();
            (void)watering_app_init();

            s_mqtt_inited = true;
        }
        break;

    case WIFI_MANAGE_STATE_DISCONNECTED:
    case WIFI_MANAGE_STATE_CONNECT_FAILED:
        if (s_coze_started) {
            ESP_LOGI(TAG, "WiFi disconnected, deinit Coze chat");
            coze_chat_app_deinit();
            s_coze_started = false;
        }
        lottie_app_show_wifi_connecting();
        break;

    default:
        break;
    }
}

static void app_mqtt_event_cb(web_mqtt_state_t state)
{
    switch (state) {
    case WEB_MQTT_STATE_CONNECTED:
    case WEB_MQTT_STATE_READY:
        ESP_LOGI(TAG, "MQTT connected");
        break;
    case WEB_MQTT_STATE_DISCONNECTED:
    case WEB_MQTT_STATE_ERROR:
        ESP_LOGW(TAG, "MQTT disconnected or error");
        break;
    default:
        break;
    }
}

/**
 * @brief 录音数据回调函数
 * 
 * ⚠️ 注意：只有在录音状态下才上传音频到 Coze
 * 录音状态由唤醒词、按键或 VAD 触发
 * 
 * @param pcm_data 采集到的PCM数据指针（16位有符号整数）
 * @param sample_count PCM数据采样点数
 * @param user_ctx 用户上下文指针（指向loopback_ctx_t）
 */
static void loopback_record_cb(const int16_t *pcm_data,
                               size_t sample_count,
                               void *user_ctx)
{
    (void)user_ctx;

    // ✅ 关键修复：只有在录音状态下才上传音频
    // 录音状态由 audio_manager 根据唤醒词/按键/VAD 事件控制
    if (!audio_manager_is_recording()) {
        return;
    }

    coze_chat_handle_t handle = coze_chat_get_handle();
    if (!handle || !pcm_data || sample_count == 0) {
        return;
    }

    int len_bytes = (int)(sample_count * sizeof(int16_t));
    esp_err_t ret = coze_chat_send_audio_data(handle, (char *)pcm_data, len_bytes);
    if (ret == ESP_OK) {
        s_uplink_samples_this_turn += sample_count;
    } else {
        ESP_LOGW(TAG, "send audio to Coze failed: %s", esp_err_to_name(ret));
    }
}

/**
 * @brief 音频管理器事件回调函数
 * 
 * 处理音频管理器产生的各种事件（唤醒、VAD开始/结束、按键等），
 * 驱动录音→播放的状态流转
 * 
 * @param event 音频事件指针
 * @param user_ctx 用户上下文指针（指向loopback_ctx_t）
 */
static void audio_event_cb(const audio_mgr_event_t *event, void *user_ctx)
{
    (void)user_ctx;

    if (!event) {
        return;
    }

    switch (event->type) {
    case AUDIO_MGR_EVENT_WAKEUP_DETECTED: {
        // 唤醒词检测成功，播放唤醒音效 + mic 动画
        ESP_LOGI(TAG, "🎤 唤醒词检测: 索引=%d, 音量=%.1f dB",
                 event->data.wakeup.wake_word_index,
                 event->data.wakeup.volume_db);
        
        // ✅ 打断功能：如果正在播放，停止播放并清空缓冲区
        if (audio_manager_is_playing()) {
            ESP_LOGI(TAG, "⏸️ 检测到唤醒，打断当前播放");
            audio_manager_stop_playback();
            audio_manager_clear_playback_buffer();
            
            // 取消当前 Coze 对话
            coze_chat_handle_t handle = coze_chat_get_handle();
            if (handle) {
                coze_chat_send_audio_cancel(handle);
            }
        }
        
        // 开启新一轮对话：重置本轮上行计数
        s_uplink_samples_this_turn = 0;

        // 重新启动播放任务（准备接收新的回复）
        audio_manager_start_playback();
        break;
    }

    case AUDIO_MGR_EVENT_VAD_START:
        // VAD检测到语音开始
        ESP_LOGI(TAG, "VAD start, begin capture");
        break;

    case AUDIO_MGR_EVENT_VAD_END: {
        // VAD检测到语音结束，通知 Coze 结束一轮语音输入
        ESP_LOGI(TAG, "VAD end, send audio complete to Coze");
        coze_chat_handle_t handle = coze_chat_get_handle();
        if (handle) {
            coze_chat_send_audio_complete(handle);
        }
        // 本轮提交完成，复位计数
        s_uplink_samples_this_turn = 0;
        break;
    }

    case AUDIO_MGR_EVENT_WAKEUP_TIMEOUT: {
        // 唤醒超时：根据是否已上传过音频决定 complete/cancel
        coze_chat_handle_t handle = coze_chat_get_handle();
        if (handle) {
            if (s_uplink_samples_this_turn > 0) {
                ESP_LOGW(TAG, "wake window timeout, auto send audio complete (%u samples)", (unsigned)s_uplink_samples_this_turn);
                coze_chat_send_audio_complete(handle);
            } else {
                ESP_LOGW(TAG, "wake window timeout, cancel Coze audio (no input)");
                coze_chat_send_audio_cancel(handle);
            }
            s_uplink_samples_this_turn = 0;
        }
        break;
    }

    case AUDIO_MGR_EVENT_BUTTON_TRIGGER: {
        // 按键触发录音，播放 mic 动画
        ESP_LOGI(TAG, "button trigger, force capture");
        
        // ✅ 打断功能：如果正在播放，停止播放并清空缓冲区
        if (audio_manager_is_playing()) {
            ESP_LOGI(TAG, "⏸️ 检测到按键，打断当前播放");
            audio_manager_stop_playback();
            audio_manager_clear_playback_buffer();
            
            // 取消当前 Coze 对话
            coze_chat_handle_t handle = coze_chat_get_handle();
            if (handle) {
                coze_chat_send_audio_cancel(handle);
            }
        }
        
        // 开启新一轮对话：重置本轮上行计数
        s_uplink_samples_this_turn = 0;

        lottie_app_show_mic_idle();
        
        // 重新启动播放任务（准备接收新的回复）
        audio_manager_start_playback();
        break;
    }

    case AUDIO_MGR_EVENT_BUTTON_RELEASE: {
        // 按键松开：提交本轮语音输入
        ESP_LOGI(TAG, "button release, send audio complete to Coze");
        coze_chat_handle_t handle = coze_chat_get_handle();
        if (handle) {
            coze_chat_send_audio_complete(handle);
        }
        s_uplink_samples_this_turn = 0;
        break;
    }

    default:
        break;
    }
}

/**
 * @brief 应用程序主入口函数
 * 
 * 初始化音频管理器，配置录音回调，启动音频采集和播放任务
 */
void app_main(void)
{
    esp_err_t ret;

    printf("esp32 网页WiFi配网 By.星年\n");

    ret = lottie_app_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "lottie_app_init failed: %s", esp_err_to_name(ret));
    } else {
        lottie_app_show_wifi_connecting();
    }

    wifi_manage_config_t wifi_cfg = WIFI_MANAGE_DEFAULT_CONFIG();
    wifi_cfg.wifi_event_cb = app_wifi_event_cb;
    ret = wifi_manage_init(&wifi_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "wifi_manage_init failed: %s", esp_err_to_name(ret));
    }
    
    // 构建音频管理器配置
    audio_mgr_config_t audio_cfg = {0};
    audio_config_app_build(&audio_cfg, audio_event_cb, NULL);

    // 初始化音频管理器
    ESP_LOGI(TAG, "init audio manager");
    ESP_ERROR_CHECK(audio_manager_init(&audio_cfg));
    
    // 设置播放音量为100%
    audio_manager_set_volume(100);
    
    // 注册录音数据回调，将麦克风PCM送入 Coze
    audio_manager_set_record_callback(loopback_record_cb, NULL);
    
    // 启动播放任务（保持播放任务常驻，随时准备播放数据）
    ESP_ERROR_CHECK(audio_manager_start_playback());
    
    // 启动音频管理器（开始录音和VAD检测）
    ESP_ERROR_CHECK(audio_manager_start());
}
