/*
 * @Author: AI Assistant
 * @Description: 音频上行模块 - 负责音频编码和发送
 * 
 * 功能：
 * - 接收 PCM 音频数据（通过环形缓冲区）
 * - 可选 Opus 编码（节省带宽）
 * - Base64 编码
 * - JSON 封装
 * - WebSocket 发送
 */

#pragma once

#include "esp_err.h"
#include "simple_ring_buffer.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// 前向声明（避免循环依赖）
typedef struct audio_uplink_s *audio_uplink_handle_t;

/**
 * @brief 音频格式枚举
 */
typedef enum {
    AUDIO_UPLINK_FORMAT_PCM = 0,   ///< PCM 原始音频
    AUDIO_UPLINK_FORMAT_OPUS = 1,  ///< Opus 压缩音频
} audio_uplink_format_t;

/**
 * @brief WebSocket 发送回调函数
 * 
 * @param json_str JSON 格式的消息字符串
 * @param user_ctx 用户上下文
 * @return true 发送成功，false 发送失败
 */
typedef bool (*audio_uplink_send_callback_t)(const char *json_str, void *user_ctx);

/**
 * @brief 音频上行配置
 */
typedef struct {
    audio_uplink_format_t format;        ///< 音频格式（PCM 或 Opus）
    int sample_rate;                     ///< 采样率（通常 16000）
    int channels;                        ///< 声道数（通常 1）
    int bit_depth;                       ///< 位深度（通常 16）
    
    // Opus 编码配置（仅在 format=OPUS 时有效）
    int opus_bitrate;                    ///< Opus 码率（推荐 16000）
    
    // WebSocket 发送回调
    audio_uplink_send_callback_t send_callback;  ///< 发送回调函数
    void *send_callback_ctx;             ///< 发送回调的用户上下文
    
} audio_uplink_config_t;

/**
 * @brief 创建音频上行模块
 * 
 * @param config 配置参数
 * @return audio_uplink_handle_t 模块句柄，失败返回 NULL
 */
audio_uplink_handle_t audio_uplink_create(const audio_uplink_config_t *config);

/**
 * @brief 销毁音频上行模块
 * 
 * @param handle 模块句柄
 */
void audio_uplink_destroy(audio_uplink_handle_t handle);

/**
 * @brief 启动音频上行任务
 * 
 * @param handle 模块句柄
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t audio_uplink_start(audio_uplink_handle_t handle);

/**
 * @brief 停止音频上行任务
 * 
 * @param handle 模块句柄
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t audio_uplink_stop(audio_uplink_handle_t handle);

/**
 * @brief 写入音频数据（零拷贝，直接写入环形缓冲区）
 * 
 * @param handle 模块句柄
 * @param data 音频数据
 * @param len 数据长度
 * @return esp_err_t ESP_OK 成功
 */
esp_err_t audio_uplink_write(audio_uplink_handle_t handle, 
                              const uint8_t *data, size_t len);

/**
 * @brief 清空音频缓冲区
 * 
 * @param handle 模块句柄
 */
void audio_uplink_clear(audio_uplink_handle_t handle);

#ifdef __cplusplus
}
#endif

