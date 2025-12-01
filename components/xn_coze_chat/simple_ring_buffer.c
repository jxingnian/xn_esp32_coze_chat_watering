/*
 * @Author: AI Assistant
 * @Description: 简单环形缓冲区实现
 */

#include "simple_ring_buffer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "SIMPLE_RB";

/**
 * @brief 环形缓冲区结构体
 */
typedef struct simple_ring_buffer_s {
    uint8_t *buffer;           ///< 缓冲区（PSRAM）
    size_t size;               ///< 缓冲区总大小
    size_t write_pos;          ///< 写指针
    size_t read_pos;           ///< 读指针
    SemaphoreHandle_t mutex;   ///< 互斥锁
    SemaphoreHandle_t data_sem; ///< 数据信号量（用于阻塞读取）
} simple_ring_buffer_t;

simple_ring_buffer_handle_t simple_ring_buffer_create(size_t size)
{
    if (size == 0) {
        ESP_LOGE(TAG, "缓冲区大小不能为0");
        return NULL;
    }

    simple_ring_buffer_t *rb = (simple_ring_buffer_t *)malloc(sizeof(simple_ring_buffer_t));
    if (!rb) {
        ESP_LOGE(TAG, "分配结构体失败");
        return NULL;
    }
    memset(rb, 0, sizeof(simple_ring_buffer_t));

    // 分配缓冲区（PSRAM）
    rb->buffer = (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!rb->buffer) {
        ESP_LOGE(TAG, "分配缓冲区失败: %d bytes", size);
        free(rb);
        return NULL;
    }

    rb->size = size;
    rb->write_pos = 0;
    rb->read_pos = 0;

    // 创建互斥锁
    rb->mutex = xSemaphoreCreateMutex();
    if (!rb->mutex) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        free(rb->buffer);
        free(rb);
        return NULL;
    }

    // 创建数据信号量
    rb->data_sem = xSemaphoreCreateBinary();
    if (!rb->data_sem) {
        ESP_LOGE(TAG, "创建信号量失败");
        vSemaphoreDelete(rb->mutex);
        free(rb->buffer);
        free(rb);
        return NULL;
    }

    ESP_LOGI(TAG, "环形缓冲区创建成功: %d bytes (PSRAM)", size);
    return rb;
}

void simple_ring_buffer_destroy(simple_ring_buffer_handle_t rb)
{
    if (!rb) return;

    if (rb->mutex) {
        vSemaphoreDelete(rb->mutex);
    }
    if (rb->data_sem) {
        vSemaphoreDelete(rb->data_sem);
    }
    if (rb->buffer) {
        free(rb->buffer);
    }
    free(rb);
}

esp_err_t simple_ring_buffer_write(simple_ring_buffer_handle_t rb, 
                                    const uint8_t *data, size_t len)
{
    if (!rb || !data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // 写入数据（环形，自动覆盖）
    for (size_t i = 0; i < len; i++) {
        rb->buffer[rb->write_pos] = data[i];
        rb->write_pos = (rb->write_pos + 1) % rb->size;

        // 如果写指针追上读指针，强制移动读指针（覆盖旧数据）
        if (rb->write_pos == rb->read_pos) {
            rb->read_pos = (rb->read_pos + 1) % rb->size;
        }
    }

    xSemaphoreGive(rb->mutex);
    
    // 通知有新数据
    xSemaphoreGive(rb->data_sem);
    
    return ESP_OK;
}

size_t simple_ring_buffer_read(simple_ring_buffer_handle_t rb, 
                                uint8_t *out, size_t len, uint32_t timeout_ms)
{
    if (!rb || !out || len == 0) {
        return 0;
    }

    // 如果无数据且允许等待，则等待信号量
    if (rb->read_pos == rb->write_pos && timeout_ms > 0) {
        if (xSemaphoreTake(rb->data_sem, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) {
            return 0;  // 超时
        }
    }

    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
        return 0;
    }

    // 计算可用数据量
    size_t available;
    if (rb->write_pos >= rb->read_pos) {
        available = rb->write_pos - rb->read_pos;
    } else {
        available = rb->size - rb->read_pos + rb->write_pos;
    }

    // 读取数据（不超过请求长度和可用长度）
    size_t to_read = (len < available) ? len : available;
    for (size_t i = 0; i < to_read; i++) {
        out[i] = rb->buffer[rb->read_pos];
        rb->read_pos = (rb->read_pos + 1) % rb->size;
    }

    xSemaphoreGive(rb->mutex);
    
    return to_read;
}

size_t simple_ring_buffer_available(simple_ring_buffer_handle_t rb)
{
    if (!rb) {
        return 0;
    }

    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        return 0;
    }

    size_t available;
    if (rb->write_pos >= rb->read_pos) {
        available = rb->write_pos - rb->read_pos;
    } else {
        available = rb->size - rb->read_pos + rb->write_pos;
    }

    xSemaphoreGive(rb->mutex);
    return available;
}

void simple_ring_buffer_clear(simple_ring_buffer_handle_t rb)
{
    if (!rb) {
        return;
    }

    if (xSemaphoreTake(rb->mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        rb->read_pos = rb->write_pos;
        xSemaphoreGive(rb->mutex);
    }
}

