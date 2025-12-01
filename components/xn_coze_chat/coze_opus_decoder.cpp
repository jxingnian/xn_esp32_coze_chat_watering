/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-24
 * @Description: Opus解码器实现
 */

#include "coze_opus_decoder.h"
#include "esp_log.h"
#include <cstring>
#include <cstdlib>

static const char *TAG = "OPUS_DECODER";

/**
 * @brief 构造函数：初始化Opus解码器
 */
CozeOpusDecoder::CozeOpusDecoder(int sample_rate, int channels)
    : decoder_(nullptr)
    , pcm_buffer_(nullptr)
    , pcm_buffer_size_(0)
    , sample_rate_(sample_rate)
    , channels_(channels)
{
    // 配置Opus解码器参数
    esp_opus_dec_cfg_t config = {
        .sample_rate = (uint32_t)sample_rate,
        .channel = (uint8_t)channels,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_INVALID,
        .self_delimited = false
    };
    
    // 创建Opus解码器实例
    esp_audio_err_t ret = esp_opus_dec_open(&config, sizeof(config), &decoder_);
    if (ret != ESP_AUDIO_ERR_OK || !decoder_) {
        ESP_LOGE(TAG, "创建Opus解码器失败: %d", ret);
        return;
    }
    
    // 分配PCM缓冲区 (60ms @ 48kHz * 2 channels = 5760 samples)
    pcm_buffer_size_ = 5760 * channels;
    pcm_buffer_ = (int16_t *)malloc(pcm_buffer_size_ * sizeof(int16_t));
    if (!pcm_buffer_) {
        ESP_LOGE(TAG, "分配PCM缓冲区失败");
        esp_opus_dec_close(decoder_);
        decoder_ = nullptr;
        return;
    }
    
    ESP_LOGI(TAG, "✅ Opus解码器初始化成功 (采样率: %dHz, 声道: %d)", sample_rate, channels);
}

/**
 * @brief 析构函数：清理Opus解码器资源
 */
CozeOpusDecoder::~CozeOpusDecoder()
{
    if (decoder_) {
        esp_opus_dec_close(decoder_);
        decoder_ = nullptr;
    }
    
    if (pcm_buffer_) {
        free(pcm_buffer_);
        pcm_buffer_ = nullptr;
    }
    
    ESP_LOGI(TAG, "Opus解码器已销毁");
}

/**
 * @brief 解码Opus音频数据为PCM格式
 */
esp_err_t CozeOpusDecoder::Decode(const uint8_t *opus_data, size_t opus_len,
                                  int16_t *pcm_out, size_t max_samples,
                                  size_t *decoded_samples)
{
    if (!decoder_) {
        ESP_LOGE(TAG, "Opus解码器未初始化");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!opus_data || opus_len == 0 || !pcm_out || max_samples == 0 || !decoded_samples) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 准备输入数据
    esp_audio_dec_in_raw_t raw_data = {};
    raw_data.buffer = (uint8_t *)opus_data;
    raw_data.len = (int)opus_len;
    raw_data.consumed = 0;
    
    // 准备输出缓冲区
    esp_audio_dec_out_frame_t frame_data = {};
    frame_data.buffer = (uint8_t *)pcm_buffer_;
    frame_data.len = (int)(pcm_buffer_size_ * sizeof(int16_t));
    frame_data.needed_size = 0;
    
    // 解码信息
    esp_audio_dec_info_t dec_info = {};
    
    // 调用解码
    esp_audio_err_t ret = esp_opus_dec_decode(decoder_, &raw_data, &frame_data, &dec_info);
    
    if (ret != ESP_AUDIO_ERR_OK) {
        ESP_LOGW(TAG, "Opus解码失败: %d", ret);
        *decoded_samples = 0;
        return ESP_FAIL;
    }
    
    // 计算实际解码的样本数
    size_t total_samples = frame_data.len / sizeof(int16_t);
    
    // 复制到输出缓冲区
    if (total_samples > max_samples) {
        // ⚠️ 屏蔽高频日志：每个音频包都打印会导致UART溢出
        // ESP_LOGW(TAG, "输出缓冲区太小: 需要%d样本, 只有%d样本", (int)total_samples, (int)max_samples);
        total_samples = max_samples;
    }
    
    memcpy(pcm_out, pcm_buffer_, total_samples * sizeof(int16_t));
    *decoded_samples = total_samples;
    
    // ⚠️ 屏蔽高频日志
    // ESP_LOGI(TAG, "Opus解码成功: %d字节 → %d样本", (int)opus_len, (int)*decoded_samples);
    
    return ESP_OK;
}

