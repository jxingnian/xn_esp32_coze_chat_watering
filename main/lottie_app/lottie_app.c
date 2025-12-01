/*
 * @Author: xingnian
 * @Date: 2025-11-30
 * @FilePath: \xn_esp32_coze_chat\main\lottie_app\lottie_app.c
 * @Description: 简单的 Lottie 动画应用封装
 */

#include <stdio.h>

#include "esp_err.h"
#include "esp_log.h"

#include "xn_lottie_manager.h"
#include "lottie_app.h"

static const char *TAG = "LOTTIE_APP";

static bool s_lottie_inited = false;

esp_err_t lottie_app_init(void)
{
    if (s_lottie_inited) {
        ESP_LOGW(TAG, "Lottie app already initialized");
        return ESP_OK;
    }

    xn_lottie_app_config_t cfg = {
        .screen_width = 412,
        .screen_height = 412,
    };

    esp_err_t ret = xn_lottie_manager_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "xn_lottie_manager_init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_lottie_inited = true;

    /* 默认显示一个加载动画 */
    lottie_app_show_loading();

    return ESP_OK;
}

static inline bool lottie_app_is_ready(void)
{
    if (!s_lottie_inited) {
        ESP_LOGW(TAG, "Lottie app not initialized");
        return false;
    }
    return true;
}

void lottie_app_show_wifi_connecting(void)
{
    if (!lottie_app_is_ready()) {
        return;
    }
    lottie_manager_play_anim(LOTTIE_ANIM_WIFI);
}

void lottie_app_show_mic_idle(void)
{
    if (!lottie_app_is_ready()) {
        return;
    }
    lottie_manager_play_anim(LOTTIE_ANIM_MIC);
}

void lottie_app_show_speaking(void)
{
    if (!lottie_app_is_ready()) {
        return;
    }
    lottie_manager_play_anim(LOTTIE_ANIM_SPEAK);
}

void lottie_app_show_thinking(void)
{
    if (!lottie_app_is_ready()) {
        return;
    }
    lottie_manager_play_anim(LOTTIE_ANIM_THINK);
}

void lottie_app_show_loading(void)
{
    if (!lottie_app_is_ready()) {
        return;
    }
    lottie_manager_play_anim(LOTTIE_ANIM_LOADING);
}

void lottie_app_show_ota(void)
{
    if (!lottie_app_is_ready()) {
        return;
    }
    lottie_manager_play_anim(LOTTIE_ANIM_OTA);
}

void lottie_app_stop(void)
{
    if (!lottie_app_is_ready()) {
        return;
    }
    lottie_manager_stop_anim(-1);
}

