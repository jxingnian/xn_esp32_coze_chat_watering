/*
 * @Author: AI Assistant
 * @Description: 音频下行模块 - 处理从服务器接收的音频
 * 
 * 功能：
 * - Base64 解码（使用静态缓冲区，零拷贝）
 * - Opus 解码为 PCM
 * - PCM 数据回调给用户
 * - 统计信息（包数、错误率等）
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 音频下行句柄（不透明类型）
 */
typedef struct audio_downlink_s *audio_downlink_handle_t;

/**
 * @brief PCM 音频回调函数
 * 
 * @param pcm PCM 音频数据（16-bit signed）
 * @param samples 样本数
 * @param user_ctx 用户上下文
 */
typedef void (*audio_downlink_pcm_callback_t)(const int16_t *pcm, size_t samples, void *user_ctx);

/**
 * @brief 音频下行配置
 */
typedef struct {
    int sample_rate;                          ///< 输出采样率（16000 Hz）
    int channels;                             ///< 声道数（1=单声道）
    audio_downlink_pcm_callback_t callback;   ///< PCM 回调函数
    void *callback_ctx;                       ///< 回调的用户上下文
} audio_downlink_config_t;

/**
 * @brief 创建音频下行模块
 * 
 * @param config 配置参数
 * @return audio_downlink_handle_t 模块句柄，失败返回 NULL
 * 
 * @note 内部会预分配 Opus 和 PCM 缓冲区（PSRAM）
 */
audio_downlink_handle_t audio_downlink_create(const audio_downlink_config_t *config);

/**
 * @brief 销毁音频下行模块
 * 
 * @param handle 模块句柄
 */
void audio_downlink_destroy(audio_downlink_handle_t handle);

/**
 * @brief 处理音频数据（Base64 → Opus → PCM → 回调）
 * 
 * 这个函数会：
 * 1. Base64 解码（使用静态缓冲区，零拷贝）
 * 2. Opus 解码为 PCM
 * 3. 通过回调函数返回 PCM 数据
 * 
 * @param handle 模块句柄
 * @param base64_audio Base64 编码的音频数据
 * @return esp_err_t ESP_OK 成功
 * 
 * @note 非阻塞，快速返回
 */
esp_err_t audio_downlink_process(audio_downlink_handle_t handle, const char *base64_audio);

/**
 * @brief 获取统计信息
 * 
 * @param handle 模块句柄
 * @param total_packets 输出：总处理包数
 * @param error_count 输出：错误包数
 */
void audio_downlink_get_stats(audio_downlink_handle_t handle, 
                               uint32_t *total_packets, 
                               uint32_t *error_count);

/**
 * @brief 重置统计信息
 * 
 * @param handle 模块句柄
 */
void audio_downlink_reset_stats(audio_downlink_handle_t handle);

#ifdef __cplusplus
}
#endif

