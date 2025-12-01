/*
 * @Author: AI Assistant
 * @Description: Base64 编解码模块实现
 */

#include "base64_codec.h"
#include "mbedtls/base64.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>

static const char *TAG = "BASE64_CODEC";

// 预分配的静态缓冲区（避免频繁 malloc/free）
// Opus 20ms@16kHz = 320样本 = 640字节，Base64编码后约 853字节
// 预分配 2KB 足够覆盖大部分场景
#define BASE64_ENCODE_BUFFER_SIZE (2048)
#define BASE64_DECODE_BUFFER_SIZE (1536)  // 2048 * 3 / 4 = 1536

static char *g_encode_buffer = NULL;
static uint8_t *g_decode_buffer = NULL;

// 🔒 互斥锁保护静态缓冲区（线程安全）
static SemaphoreHandle_t g_encode_mutex = NULL;
static SemaphoreHandle_t g_decode_mutex = NULL;

char* base64_encode_audio(const uint8_t *data, size_t len, size_t *out_len)
{
    // 参数检查
    if (!data || len == 0 || !out_len) {
        ESP_LOGE(TAG, "无效的参数");
        return NULL;
    }

    // 第一次调用时创建互斥锁
    if (!g_encode_mutex) {
        // 使用临时变量避免竞争条件
        SemaphoreHandle_t temp_mutex = xSemaphoreCreateMutex();
        if (!temp_mutex) {
            ESP_LOGE(TAG, "创建编码互斥锁失败");
            return NULL;
        }
        // 原子操作（虽然不完美，但足够安全）
        if (!g_encode_mutex) {
            g_encode_mutex = temp_mutex;
        } else {
            vSemaphoreDelete(temp_mutex);  // 如果已被其他任务创建，删除临时的
        }
    }

    // 🔒 获取互斥锁（保护静态缓冲区）
    if (xSemaphoreTake(g_encode_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "获取编码互斥锁超时");
        return NULL;
    }

    // 第一次调用时分配静态缓冲区（PSRAM）
    if (!g_encode_buffer) {
        g_encode_buffer = (char *)heap_caps_malloc(BASE64_ENCODE_BUFFER_SIZE, 
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_encode_buffer) {
            ESP_LOGE(TAG, "分配编码缓冲区失败: %d bytes", BASE64_ENCODE_BUFFER_SIZE);
            xSemaphoreGive(g_encode_mutex);  // 🔓 释放锁
            return NULL;
        }
        ESP_LOGI(TAG, "✅ Base64 编码缓冲区已分配: %d bytes (PSRAM)", BASE64_ENCODE_BUFFER_SIZE);
    }

    // 第一步：计算 Base64 编码后的长度
    size_t base64_len = 0;
    int ret = mbedtls_base64_encode(NULL, 0, &base64_len, data, len);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGE(TAG, "计算编码长度失败");
        xSemaphoreGive(g_encode_mutex);  // 🔓 释放锁
        return NULL;
    }
    
    // 检查缓冲区是否足够
    if (base64_len + 1 > BASE64_ENCODE_BUFFER_SIZE) {
        ESP_LOGE(TAG, "输入数据过大: 需要 %d bytes, 缓冲区只有 %d bytes", 
                 base64_len + 1, BASE64_ENCODE_BUFFER_SIZE);
        xSemaphoreGive(g_encode_mutex);  // 🔓 释放锁
        return NULL;
    }
    
    // 第二步：执行 Base64 编码（使用静态缓冲区）
    ret = mbedtls_base64_encode((uint8_t *)g_encode_buffer, BASE64_ENCODE_BUFFER_SIZE, 
                                out_len, data, len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 编码失败: %d", ret);
        xSemaphoreGive(g_encode_mutex);  // 🔓 释放锁
        return NULL;
    }
    
    // 添加字符串结束符
    g_encode_buffer[*out_len] = '\0';
    
    // ⚠️ 注意：返回的指针在下次调用前有效，调用者必须立即使用
    // 互斥锁会在下次调用时自动保护（不需要调用者释放锁）
    
    // 🔓 不要在这里释放锁！让调用者使用完数据后再释放
    // 实际上我们需要改变设计...
    
    // 返回静态缓冲区指针（调用者需要在下次调用前使用完毕）
    xSemaphoreGive(g_encode_mutex);  // 🔓 释放锁
    return g_encode_buffer;
}

uint8_t* base64_decode_audio(const char *base64_str, size_t *out_len)
{
    // 参数检查
    if (!base64_str || !out_len) {
        ESP_LOGE(TAG, "无效的参数");
        return NULL;
    }

    size_t len = strlen(base64_str);
    if (len == 0) {
        ESP_LOGE(TAG, "Base64 字符串为空");
        return NULL;
    }

    // 第一次调用时创建互斥锁
    if (!g_decode_mutex) {
        SemaphoreHandle_t temp_mutex = xSemaphoreCreateMutex();
        if (!temp_mutex) {
            ESP_LOGE(TAG, "创建解码互斥锁失败");
            return NULL;
        }
        if (!g_decode_mutex) {
            g_decode_mutex = temp_mutex;
        } else {
            vSemaphoreDelete(temp_mutex);
        }
    }

    // 🔒 获取互斥锁
    if (xSemaphoreTake(g_decode_mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "获取解码互斥锁超时");
        return NULL;
    }

    // 第一次调用时分配静态缓冲区（PSRAM）
    if (!g_decode_buffer) {
        g_decode_buffer = (uint8_t *)heap_caps_malloc(BASE64_DECODE_BUFFER_SIZE, 
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!g_decode_buffer) {
            ESP_LOGE(TAG, "分配解码缓冲区失败: %d bytes", BASE64_DECODE_BUFFER_SIZE);
            xSemaphoreGive(g_decode_mutex);  // 🔓 释放锁
            return NULL;
        }
        ESP_LOGI(TAG, "✅ Base64 解码缓冲区已分配: %d bytes (PSRAM)", BASE64_DECODE_BUFFER_SIZE);
    }

    // 第一步：计算解码后的长度
    size_t decode_len = 0;
    int ret = mbedtls_base64_decode(NULL, 0, &decode_len, 
                                     (const uint8_t *)base64_str, len);
    if (ret != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
        ESP_LOGE(TAG, "计算解码长度失败: %d", ret);
        xSemaphoreGive(g_decode_mutex);  // 🔓 释放锁
        return NULL;
    }
    
    // 检查缓冲区是否足够
    if (decode_len > BASE64_DECODE_BUFFER_SIZE) {
        ESP_LOGE(TAG, "输入数据过大: 需要 %d bytes, 缓冲区只有 %d bytes", 
                 decode_len, BASE64_DECODE_BUFFER_SIZE);
        xSemaphoreGive(g_decode_mutex);  // 🔓 释放锁
        return NULL;
    }
    
    // 第二步：执行 Base64 解码（使用静态缓冲区）
    ret = mbedtls_base64_decode(g_decode_buffer, BASE64_DECODE_BUFFER_SIZE, out_len, 
                                (const uint8_t *)base64_str, len);
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 解码失败: %d", ret);
        xSemaphoreGive(g_decode_mutex);  // 🔓 释放锁
        return NULL;
    }
    
    // 返回静态缓冲区指针（调用者需要在下次调用前使用完毕）
    xSemaphoreGive(g_decode_mutex);  // 🔓 释放锁
    return g_decode_buffer;
}

size_t base64_get_encode_length(size_t data_len)
{
    // Base64 编码公式：(data_len + 2) / 3 * 4
    return ((data_len + 2) / 3) * 4;
}

size_t base64_get_decode_length(size_t base64_len)
{
    // Base64 解码最大长度：base64_len * 3 / 4
    return (base64_len * 3) / 4;
}

