/*
 * @Author: AI Assistant
 * @Description: 音频下行模块实现（使用独立的Opus缓冲区）
 */

#include "audio_downlink.h"
#include "base64_codec.h"
#include "coze_opus_decoder.h"
#include "opus_buffer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "AUDIO_DOWNLINK";

/**
 * @brief 音频下行结构体
 */
typedef struct audio_downlink_s {
    // Opus 解码器
    CozeOpusDecoder *opus_decoder;
    
    // Opus包缓冲区（独立模块，环形缓冲区）
    opus_buffer_handle_t opus_buffer;
    
    // PCM 帧缓冲区（解码任务使用）
    int16_t *pcm_buffer;
    size_t pcm_buffer_size;  // 样本数
    
    // 解码任务
    TaskHandle_t decode_task;
    bool decode_running;
    
    // 配置
    audio_downlink_config_t config;
    
    // 统计信息
    uint32_t total_packets;
    uint32_t error_count;
    uint32_t buffer_full_count;  // 缓冲区满次数
    
} audio_downlink_t;

/**
 * @brief Opus解码任务（从环形缓冲区读取Opus包→解码→回调PCM）
 */
static void opus_decode_task(void *arg)
{
    audio_downlink_t *downlink = (audio_downlink_t *)arg;
    
    // 临时缓冲区（读取Opus数据）
    uint8_t *opus_temp = (uint8_t *)heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!opus_temp) {
        ESP_LOGE(TAG, "解码任务临时缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }
    
    ESP_LOGI(TAG, "🚀 Opus解码任务启动");
    
    while (downlink->decode_running) {
        // 从环形缓冲区读取Opus包（阻塞等待）
        size_t opus_len = 0;
        esp_err_t ret = opus_buffer_read(
            downlink->opus_buffer,
            opus_temp,
            512,
            &opus_len,
            portMAX_DELAY
        );
        
        if (ret == ESP_OK && opus_len > 0) {
            // 解码Opus → PCM
            size_t decoded_samples = 0;
            ret = downlink->opus_decoder->Decode(
                opus_temp,
                opus_len,
                downlink->pcm_buffer,
                downlink->pcm_buffer_size,
                &decoded_samples
            );
            
            if (ret == ESP_OK && decoded_samples > 0) {
                // 回调PCM数据给播放器
                if (downlink->config.callback) {
                    downlink->config.callback(
                        downlink->pcm_buffer,
                        decoded_samples,
                        downlink->config.callback_ctx
                    );
                }
            } else {
                downlink->error_count++;
            }
        }
    }
    
    heap_caps_free(opus_temp);
    ESP_LOGI(TAG, "Opus解码任务退出");
    vTaskDelete(NULL);
}

audio_downlink_handle_t audio_downlink_create(const audio_downlink_config_t *config)
{
    if (!config || !config->callback) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }
    
    audio_downlink_t *downlink = new audio_downlink_t();
    if (!downlink) {
        ESP_LOGE(TAG, "分配结构体失败");
        return NULL;
    }
    memset(downlink, 0, sizeof(audio_downlink_t));
    
    // 复制配置
    memcpy(&downlink->config, config, sizeof(audio_downlink_config_t));
    
    // 创建 Opus 解码器
    downlink->opus_decoder = new CozeOpusDecoder(config->sample_rate, config->channels);
    if (!downlink->opus_decoder || !downlink->opus_decoder->IsReady()) {
        ESP_LOGE(TAG, "创建 Opus 解码器失败");
        delete downlink;
        return NULL;
    }
    
    // 创建Opus缓冲区（环形缓冲区，2000包 ≈ 120秒音频）
    // 每包~200字节（压缩后），总大小 ≈ 400KB（一次分配，PSRAM）
    opus_buffer_config_t opus_buf_cfg = {
        .capacity = 2000,           // 2000包
        .max_packet_size = 512,     // 单包最大512字节
    };
    
    downlink->opus_buffer = opus_buffer_create(&opus_buf_cfg);
    if (!downlink->opus_buffer) {
        ESP_LOGE(TAG, "创建Opus缓冲区失败");
        delete downlink->opus_decoder;
        delete downlink;
        return NULL;
    }
    
    // 预分配 PCM 缓冲区（16kHz * 60ms = 960 样本）
    downlink->pcm_buffer_size = 960;
    downlink->pcm_buffer = (int16_t *)heap_caps_malloc(
        downlink->pcm_buffer_size * sizeof(int16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT
    );
    
    if (!downlink->pcm_buffer) {
        ESP_LOGE(TAG, "分配 PCM 缓冲区失败");
        opus_buffer_destroy(downlink->opus_buffer);
        delete downlink->opus_decoder;
        delete downlink;
        return NULL;
    }
    
    // 启动解码任务（优先级5，栈8KB在PSRAM）
    downlink->decode_running = true;
    
    StaticTask_t *decode_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    StackType_t *decode_stack = (StackType_t *)heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!decode_tcb || !decode_stack) {
        ESP_LOGE(TAG, "解码任务内存分配失败");
        if (decode_tcb) heap_caps_free(decode_tcb);
        if (decode_stack) heap_caps_free(decode_stack);
        heap_caps_free(downlink->pcm_buffer);
        opus_buffer_destroy(downlink->opus_buffer);
        delete downlink->opus_decoder;
        delete downlink;
        return NULL;
    }
    
    downlink->decode_task = xTaskCreateStaticPinnedToCore(
        opus_decode_task,
        "opus_decode",
        8192 / sizeof(StackType_t),
        downlink,
        5,              // 优先级5
        decode_stack,
        decode_tcb,
        0               // Core 0
    );
    
    if (!downlink->decode_task) {
        ESP_LOGE(TAG, "创建解码任务失败");
        heap_caps_free(decode_tcb);
        heap_caps_free(decode_stack);
        heap_caps_free(downlink->pcm_buffer);
        opus_buffer_destroy(downlink->opus_buffer);
        delete downlink->opus_decoder;
        delete downlink;
        return NULL;
    }
    
    ESP_LOGI(TAG, "✅ 音频下行模块创建成功（环形缓冲区架构）");
    ESP_LOGI(TAG, "  采样率: %d Hz", config->sample_rate);
    ESP_LOGI(TAG, "  声道数: %d", config->channels);
    ESP_LOGI(TAG, "  Opus缓冲: 2000 包 (~120秒)");
    ESP_LOGI(TAG, "  PCM缓冲: %d 样本 (PSRAM)", downlink->pcm_buffer_size);
    
    return downlink;
}

void audio_downlink_destroy(audio_downlink_handle_t handle)
{
    if (!handle) return;
    
    // 停止解码任务
    if (handle->decode_task) {
        handle->decode_running = false;
        vTaskDelay(pdMS_TO_TICKS(100));  // 等待任务退出
        handle->decode_task = NULL;
    }
    
    // 销毁Opus缓冲区（自动清空）
    if (handle->opus_buffer) {
        opus_buffer_destroy(handle->opus_buffer);
    }
    
    // 销毁 Opus 解码器
    if (handle->opus_decoder) {
        delete handle->opus_decoder;
    }
    
    // 释放 PCM 缓冲区
    if (handle->pcm_buffer) {
        heap_caps_free(handle->pcm_buffer);
    }
    
    delete handle;
    ESP_LOGI(TAG, "音频下行模块已销毁");
}

esp_err_t audio_downlink_process(audio_downlink_handle_t handle, const char *base64_audio)
{
    if (!handle || !base64_audio) {
        return ESP_ERR_INVALID_ARG;
    }
    
    handle->total_packets++;
    
    // 步骤1：Base64 解码（使用静态缓冲区，零拷贝）
    size_t opus_len = 0;
    uint8_t *opus_data = base64_decode_audio(base64_audio, &opus_len);
    
    if (!opus_data || opus_len == 0) {
        ESP_LOGE(TAG, "❌ Base64 解码失败 (包 #%lu)", handle->total_packets);
        handle->error_count++;
        return ESP_FAIL;
    }
    
    // 步骤2：写入环形缓冲区（内部自动复制）
    esp_err_t ret = opus_buffer_write(handle->opus_buffer, opus_data, opus_len);
    
    if (ret != ESP_OK) {
        // 缓冲区满，丢弃这个包
        handle->buffer_full_count++;
        
        // 每100次缓冲区满打印一次警告
        if (handle->buffer_full_count % 100 == 0) {
            ESP_LOGW(TAG, "⚠️ Opus缓冲区满！已丢弃 %lu 包", handle->buffer_full_count);
        }
        return ESP_FAIL;
    }
    
    // 每100包打印一次统计（避免日志刷屏）
    if (handle->total_packets % 100 == 0) {
        size_t buffer_count = opus_buffer_get_count(handle->opus_buffer);
        float buffer_usage = (float)buffer_count / 2000 * 100.0f;
        
        ESP_LOGI(TAG, "📊 已接收 %lu 包 (错误: %lu, 缓冲区满: %lu, 缓冲区使用: %.1f%%)", 
                 handle->total_packets,
                 handle->error_count,
                 handle->buffer_full_count,
                 buffer_usage);
    }
    
    return ESP_OK;
}

void audio_downlink_get_stats(audio_downlink_handle_t handle, 
                               uint32_t *total_packets, 
                               uint32_t *error_count)
{
    if (!handle) return;
    
    if (total_packets) {
        *total_packets = handle->total_packets;
    }
    if (error_count) {
        *error_count = handle->error_count;
    }
}

void audio_downlink_reset_stats(audio_downlink_handle_t handle)
{
    if (!handle) return;
    
    handle->total_packets = 0;
    handle->error_count = 0;
    ESP_LOGI(TAG, "统计信息已重置");
}

