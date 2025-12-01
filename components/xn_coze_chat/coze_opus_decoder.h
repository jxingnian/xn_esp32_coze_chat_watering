/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-24
 * @Description: Opus解码器封装（C++版本）
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "esp_opus_dec.h"

/**
 * @brief Opus解码器类
 */
class CozeOpusDecoder
{
public:
    /**
     * @brief 构造函数
     * @param sample_rate 采样率（默认16kHz）
     * @param channels 声道数（默认1）
     */
    explicit CozeOpusDecoder(int sample_rate = 16000, int channels = 1);

    /**
     * @brief 析构函数
     */
    ~CozeOpusDecoder();

    /**
     * @brief 解码Opus数据到PCM
     * @param opus_data Opus编码数据
     * @param opus_len Opus数据长度
     * @param pcm_out PCM输出缓冲区
     * @param max_samples 最大样本数
     * @param decoded_samples 实际解码样本数
     * @return esp_err_t
     */
    esp_err_t Decode(const uint8_t *opus_data, size_t opus_len,
                     int16_t *pcm_out, size_t max_samples,
                     size_t *decoded_samples);

    /**
     * @brief 检查解码器是否就绪
     * @return bool
     */
    bool IsReady() const { return decoder_ != nullptr; }

    /**
     * @brief 获取采样率
     * @return int
     */
    int GetSampleRate() const { return sample_rate_; }

    /**
     * @brief 获取声道数
     * @return int
     */
    int GetChannels() const { return channels_; }

private:
    void *decoder_;          ///< Opus解码器句柄
    int16_t *pcm_buffer_;    ///< PCM缓冲区
    size_t pcm_buffer_size_; ///< PCM缓冲区大小
    int sample_rate_;        ///< 采样率
    int channels_;           ///< 声道数
};

