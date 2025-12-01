/*
 * @Author: AI Assistant
 * @Description: Opus数据缓冲区实现 - 使用环形缓冲区
 */

#include "opus_buffer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "OPUS_BUFFER";

/**
 * @brief Opus包头（存储包大小）
 */
typedef struct {
    uint16_t size;  ///< 包大小（字节）
} opus_packet_header_t;

/**
 * @brief Opus缓冲区结构体
 * 
 * 环形缓冲区设计：
 * [header1|data1][header2|data2]...[headerN|dataN]
 * 
 * 每个包 = 2字节头（大小） + 实际数据
 */
typedef struct opus_buffer_s {
    uint8_t *buffer;                ///< 缓冲区（PSRAM）
    size_t buffer_size;             ///< 缓冲区总大小（字节）
    size_t capacity;                ///< 最大包数
    size_t max_packet_size;         ///< 单包最大大小
    
    volatile size_t write_pos;      ///< 写位置
    volatile size_t read_pos;       ///< 读位置
    volatile size_t count;          ///< 当前包数
    
    SemaphoreHandle_t mutex;        ///< 互斥锁
    SemaphoreHandle_t data_sem;     ///< 数据可用信号量
} opus_buffer_t;

opus_buffer_handle_t opus_buffer_create(const opus_buffer_config_t *config)
{
    if (!config || config->capacity == 0 || config->max_packet_size == 0) {
        ESP_LOGE(TAG, "无效的配置参数");
        return NULL;
    }
    
    opus_buffer_t *buf = (opus_buffer_t *)malloc(sizeof(opus_buffer_t));
    if (!buf) {
        ESP_LOGE(TAG, "缓冲区句柄分配失败");
        return NULL;
    }
    memset(buf, 0, sizeof(opus_buffer_t));
    
    // 计算缓冲区大小：容量 × (头大小 + 数据大小)
    buf->capacity = config->capacity;
    buf->max_packet_size = config->max_packet_size;
    buf->buffer_size = config->capacity * (sizeof(opus_packet_header_t) + config->max_packet_size);
    
    // 分配缓冲区（PSRAM）
    buf->buffer = (uint8_t *)heap_caps_malloc(buf->buffer_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf->buffer) {
        ESP_LOGE(TAG, "缓冲区内存分配失败: %d bytes", (int)buf->buffer_size);
        free(buf);
        return NULL;
    }
    
    // 创建互斥锁
    buf->mutex = xSemaphoreCreateMutex();
    if (!buf->mutex) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        heap_caps_free(buf->buffer);
        free(buf);
        return NULL;
    }
    
    // 创建信号量
    buf->data_sem = xSemaphoreCreateBinary();
    if (!buf->data_sem) {
        ESP_LOGE(TAG, "信号量创建失败");
        vSemaphoreDelete(buf->mutex);
        heap_caps_free(buf->buffer);
        free(buf);
        return NULL;
    }
    
    ESP_LOGI(TAG, "✅ Opus缓冲区创建成功");
    ESP_LOGI(TAG, "  容量: %d 包", (int)buf->capacity);
    ESP_LOGI(TAG, "  单包最大: %d 字节", (int)buf->max_packet_size);
    ESP_LOGI(TAG, "  总大小: %.1f KB (PSRAM)", buf->buffer_size / 1024.0f);
    
    return buf;
}

void opus_buffer_destroy(opus_buffer_handle_t buffer)
{
    if (!buffer) return;
    
    if (buffer->mutex) {
        vSemaphoreDelete(buffer->mutex);
    }
    if (buffer->data_sem) {
        vSemaphoreDelete(buffer->data_sem);
    }
    if (buffer->buffer) {
        heap_caps_free(buffer->buffer);
    }
    free(buffer);
    
    ESP_LOGI(TAG, "Opus缓冲区已销毁");
}

esp_err_t opus_buffer_write(opus_buffer_handle_t buffer, const uint8_t *data, size_t len)
{
    if (!buffer || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (len > buffer->max_packet_size) {
        ESP_LOGE(TAG, "包大小超过限制: %d > %d", (int)len, (int)buffer->max_packet_size);
        return ESP_ERR_INVALID_SIZE;
    }
    
    if (xSemaphoreTake(buffer->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 检查是否有空间
    if (buffer->count >= buffer->capacity) {
        xSemaphoreGive(buffer->mutex);
        return ESP_ERR_NO_MEM;  // 缓冲区满
    }
    
    // 写入包头（大小）
    opus_packet_header_t header = { .size = (uint16_t)len };
    size_t header_size = sizeof(opus_packet_header_t);
    
    // 检查是否需要环绕
    size_t packet_total_size = header_size + len;
    if (buffer->write_pos + packet_total_size > buffer->buffer_size) {
        // 环绕到开头
        buffer->write_pos = 0;
    }
    
    // 写入头
    memcpy(buffer->buffer + buffer->write_pos, &header, header_size);
    buffer->write_pos += header_size;
    
    // 写入数据
    memcpy(buffer->buffer + buffer->write_pos, data, len);
    buffer->write_pos += len;
    
    // 更新计数
    buffer->count++;
    
    xSemaphoreGive(buffer->mutex);
    
    // 通知有数据可读
    xSemaphoreGive(buffer->data_sem);
    
    return ESP_OK;
}

esp_err_t opus_buffer_read(opus_buffer_handle_t buffer, 
                           uint8_t *out, 
                           size_t max_len,
                           size_t *actual_len,
                           uint32_t timeout_ms)
{
    if (!buffer || !out || !actual_len) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 等待数据（如果缓冲区为空）
    if (buffer->count == 0 && timeout_ms > 0) {
        if (xSemaphoreTake(buffer->data_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            return ESP_ERR_TIMEOUT;
        }
    }
    
    if (xSemaphoreTake(buffer->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    // 检查是否有数据
    if (buffer->count == 0) {
        xSemaphoreGive(buffer->mutex);
        return ESP_ERR_NOT_FOUND;
    }
    
    // 读取包头
    opus_packet_header_t header;
    size_t header_size = sizeof(opus_packet_header_t);
    
    // 检查是否需要环绕
    if (buffer->read_pos + header_size > buffer->buffer_size) {
        buffer->read_pos = 0;
    }
    
    memcpy(&header, buffer->buffer + buffer->read_pos, header_size);
    buffer->read_pos += header_size;
    
    // 检查输出缓冲区大小
    if (header.size > max_len) {
        ESP_LOGE(TAG, "输出缓冲区太小: %d > %d", header.size, (int)max_len);
        xSemaphoreGive(buffer->mutex);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // 检查是否需要环绕
    if (buffer->read_pos + header.size > buffer->buffer_size) {
        buffer->read_pos = 0;
    }
    
    // 读取数据
    memcpy(out, buffer->buffer + buffer->read_pos, header.size);
    buffer->read_pos += header.size;
    
    *actual_len = header.size;
    buffer->count--;
    
    xSemaphoreGive(buffer->mutex);
    
    return ESP_OK;
}

size_t opus_buffer_get_count(opus_buffer_handle_t buffer)
{
    if (!buffer) {
        return 0;
    }
    
    size_t count = 0;
    if (xSemaphoreTake(buffer->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        count = buffer->count;
        xSemaphoreGive(buffer->mutex);
    }
    
    return count;
}

esp_err_t opus_buffer_clear(opus_buffer_handle_t buffer)
{
    if (!buffer) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (xSemaphoreTake(buffer->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    
    buffer->read_pos = 0;
    buffer->write_pos = 0;
    buffer->count = 0;
    
    xSemaphoreGive(buffer->mutex);
    
    return ESP_OK;
}

