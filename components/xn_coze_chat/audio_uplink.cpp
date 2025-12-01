/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-28 22:18:28
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-10-30 17:32:42
 * @FilePath: \ESP_ChunFeng\components\coze_chat\audio_uplink.cpp
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/*
 * @Author: AI Assistant
 * @Description: 音频上行模块实现
 */

#include "audio_uplink.h"
#include "simple_ring_buffer.h"
#include "base64_codec.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "encoder/impl/esp_opus_enc.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "AUDIO_UPLINK";

/**
 * @brief 音频上行结构体
 */
typedef struct audio_uplink_s {
    // 配置
    audio_uplink_config_t config;
    
    // 环形缓冲区
    simple_ring_buffer_handle_t rb;
    
    // Opus 编码器（可选）
    void *opus_encoder;
    
    // 发送任务
    TaskHandle_t task;
    bool running;
    
} audio_uplink_t;

/**
 * @brief 音频发送任务
 */
static void audio_uplink_task(void *arg)
{
    audio_uplink_t *uplink = (audio_uplink_t *)arg;
    
    ESP_LOGI(TAG, "🚀 音频上行任务启动");
    ESP_LOGI(TAG, "  格式: %s", uplink->config.format == AUDIO_UPLINK_FORMAT_OPUS ? "Opus" : "PCM");
    ESP_LOGI(TAG, "  采样率: %d Hz", uplink->config.sample_rate);
    
    // 固定读取 640 字节（320样本 × 2字节 = 20ms@16kHz）
    const size_t FRAME_SIZE = 640;
    uint32_t packet_count = 0;  // 移到这里，避免 goto 跨越初始化
    
    uint8_t *pcm_frame = (uint8_t *)heap_caps_malloc(FRAME_SIZE, MALLOC_CAP_SPIRAM);
    uint8_t *opus_buffer = NULL;
    
    if (uplink->config.format == AUDIO_UPLINK_FORMAT_OPUS) {
        opus_buffer = (uint8_t *)heap_caps_malloc(4000, MALLOC_CAP_SPIRAM);
    }
    
    if (!pcm_frame || (uplink->config.format == AUDIO_UPLINK_FORMAT_OPUS && !opus_buffer)) {
        ESP_LOGE(TAG, "❌ 分配缓冲区失败");
        goto cleanup;
    }
    
    while (uplink->running) {
        // 从环形缓冲区读取固定大小的音频帧
        size_t got = simple_ring_buffer_read(uplink->rb, pcm_frame, FRAME_SIZE, 200);
        
        if (got != FRAME_SIZE) {
            // 数据不够，继续等待
            continue;
        }
        
        const uint8_t *send_data = pcm_frame;
        size_t send_len = FRAME_SIZE;
        
        // 如果启用 Opus 编码
        if (uplink->config.format == AUDIO_UPLINK_FORMAT_OPUS && uplink->opus_encoder) {
            esp_audio_enc_in_frame_t in_frame = {
                .buffer = pcm_frame,
                .len = FRAME_SIZE,
            };
            
            esp_audio_enc_out_frame_t out_frame = {
                .buffer = opus_buffer,
                .len = 4000,
                .encoded_bytes = 0,
                .pts = 0,
            };
            
            esp_audio_err_t ret = esp_opus_enc_process(uplink->opus_encoder, &in_frame, &out_frame);
            if (ret == ESP_AUDIO_ERR_OK && out_frame.encoded_bytes > 0) {
                send_data = opus_buffer;
                send_len = out_frame.encoded_bytes;
            } else {
                ESP_LOGE(TAG, "❌ Opus 编码失败: %d", ret);
                continue;
            }
        }
        
        // Base64 编码
        size_t base64_len = 0;
        char *base64_str = base64_encode_audio(send_data, send_len, &base64_len);
        
        if (!base64_str) {
            ESP_LOGE(TAG, "❌ Base64 编码失败");
            continue;
        }
        
        // 构建 JSON 消息
        cJSON *root = cJSON_CreateObject();
        char event_id[64];
        snprintf(event_id, sizeof(event_id), "audio_%lld", esp_timer_get_time() / 1000);
        
        cJSON_AddStringToObject(root, "id", event_id);
        cJSON_AddStringToObject(root, "event_type", "input_audio_buffer.append");
        
        cJSON *data = cJSON_CreateObject();
        cJSON_AddStringToObject(data, "delta", base64_str);
        cJSON_AddItemToObject(root, "data", data);
        
        char *json_str = cJSON_PrintUnformatted(root);
        
        // 通过回调函数发送
        if (uplink->config.send_callback && json_str) {
            packet_count++;
            bool success = uplink->config.send_callback(json_str, uplink->config.send_callback_ctx);
            
            if (!success) {
                ESP_LOGW(TAG, "⚠️ 音频包 #%lu 发送失败", packet_count);
            }
            // 每100包打印一次统计
            else if (packet_count % 100 == 0) {
                ESP_LOGI(TAG, "📊 已发送 %lu 包 (缓冲区: %d bytes)", 
                         packet_count, simple_ring_buffer_available(uplink->rb));
            }
        }
        
        // 清理 JSON
        if (json_str) free(json_str);
        cJSON_Delete(root);
        // base64_str 指向静态缓冲区，不需要释放
    }
    
cleanup:
    if (pcm_frame) heap_caps_free(pcm_frame);
    if (opus_buffer) heap_caps_free(opus_buffer);
    
    ESP_LOGI(TAG, "音频上行任务退出");
    vTaskDelete(NULL);
}

audio_uplink_handle_t audio_uplink_create(const audio_uplink_config_t *config)
{
    if (!config || !config->send_callback) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }
    
    audio_uplink_t *uplink = (audio_uplink_t *)malloc(sizeof(audio_uplink_t));
    if (!uplink) {
        ESP_LOGE(TAG, "分配结构体失败");
        return NULL;
    }
    memset(uplink, 0, sizeof(audio_uplink_t));
    
    // 复制配置
    memcpy(&uplink->config, config, sizeof(audio_uplink_config_t));
    
    // 创建环形缓冲区（16KB，约 250ms@16kHz）
    uplink->rb = simple_ring_buffer_create(16384);
    if (!uplink->rb) {
        ESP_LOGE(TAG, "创建环形缓冲区失败");
        free(uplink);
        return NULL;
    }
    
    // 如果需要 Opus 编码，创建编码器
    if (config->format == AUDIO_UPLINK_FORMAT_OPUS) {
        esp_opus_enc_config_t opus_cfg = {
            .sample_rate = config->sample_rate,
            .channel = config->channels,
            .bits_per_sample = config->bit_depth,
            .bitrate = config->opus_bitrate > 0 ? config->opus_bitrate : 16000,
            .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS,
            .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP,
            .complexity = 0,  // 最低复杂度
            .enable_fec = false,
            .enable_dtx = false,
            .enable_vbr = false,
        };
        
        esp_audio_err_t ret = esp_opus_enc_open(&opus_cfg, sizeof(opus_cfg), &uplink->opus_encoder);
        if (ret != ESP_AUDIO_ERR_OK) {
            ESP_LOGE(TAG, "创建 Opus 编码器失败: %d", ret);
            simple_ring_buffer_destroy(uplink->rb);
            free(uplink);
            return NULL;
        }
        ESP_LOGI(TAG, "✅ Opus 编码器创建成功 (码率: %d bps)", opus_cfg.bitrate);
    }
    
    ESP_LOGI(TAG, "✅ 音频上行模块创建成功");
    return uplink;
}

void audio_uplink_destroy(audio_uplink_handle_t handle)
{
    if (!handle) return;
    
    // 停止任务
    audio_uplink_stop(handle);
    
    // 销毁编码器
    if (handle->opus_encoder) {
        esp_opus_enc_close(handle->opus_encoder);
    }
    
    // 销毁环形缓冲区
    if (handle->rb) {
        simple_ring_buffer_destroy(handle->rb);
    }
    
    free(handle);
    ESP_LOGI(TAG, "音频上行模块已销毁");
}

esp_err_t audio_uplink_start(audio_uplink_handle_t handle)
{
    if (!handle) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (handle->running) {
        ESP_LOGW(TAG, "任务已在运行");
        return ESP_OK;
    }
    
    handle->running = true;
    
    // 创建任务（栈在 PSRAM）
    BaseType_t ret = xTaskCreate(
        audio_uplink_task,
        "audio_uplink",
        12288*2,  // 12KB 栈（增加至12KB，确保足够处理Base64+JSON+WebSocket）
        handle,
        6,     // 优先级
        &handle->task
    );
    
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "创建任务失败");
        handle->running = false;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ 音频上行任务已启动");
    return ESP_OK;
}

esp_err_t audio_uplink_stop(audio_uplink_handle_t handle)
{
    if (!handle || !handle->running) {
        return ESP_OK;
    }
    
    handle->running = false;
    
    // 等待任务退出
    if (handle->task) {
        vTaskDelay(pdMS_TO_TICKS(200));
        handle->task = NULL;
    }
    
    ESP_LOGI(TAG, "音频上行任务已停止");
    return ESP_OK;
}

esp_err_t audio_uplink_write(audio_uplink_handle_t handle, 
                              const uint8_t *data, size_t len)
{
    if (!handle || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 直接写入环形缓冲区（零拷贝）
    return simple_ring_buffer_write(handle->rb, data, len);
}

void audio_uplink_clear(audio_uplink_handle_t handle)
{
    if (!handle) return;
    
    simple_ring_buffer_clear(handle->rb);
    ESP_LOGI(TAG, "音频缓冲区已清空");
}

