/*
 * @Author: AI Assistant
 * @Description: Opus数据缓冲区 - 使用环形缓冲区存储Opus包
 * 
 * 功能：
 * - 缓冲压缩的Opus数据包（节省内存）
 * - 提供生产者-消费者模式
 * - 自动内存管理
 */

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opus缓冲区句柄（不透明类型）
 */
typedef struct opus_buffer_s *opus_buffer_handle_t;

/**
 * @brief Opus缓冲区配置
 */
typedef struct {
    size_t capacity;        ///< 缓冲区容量（包数）
    size_t max_packet_size; ///< 单个包的最大大小（字节）
} opus_buffer_config_t;

/**
 * @brief 创建Opus缓冲区
 * 
 * @param config 配置参数
 * @return opus_buffer_handle_t 缓冲区句柄，失败返回NULL
 */
opus_buffer_handle_t opus_buffer_create(const opus_buffer_config_t *config);

/**
 * @brief 销毁Opus缓冲区
 * 
 * @param buffer 缓冲区句柄
 */
void opus_buffer_destroy(opus_buffer_handle_t buffer);

/**
 * @brief 写入Opus包到缓冲区
 * 
 * @param buffer 缓冲区句柄
 * @param data Opus数据
 * @param len 数据长度
 * @return esp_err_t ESP_OK成功，ESP_ERR_NO_MEM缓冲区满
 * 
 * @note 内部会复制数据，调用者可以在返回后释放原数据
 */
esp_err_t opus_buffer_write(opus_buffer_handle_t buffer, const uint8_t *data, size_t len);

/**
 * @brief 从缓冲区读取Opus包
 * 
 * @param buffer 缓冲区句柄
 * @param out 输出缓冲区
 * @param max_len 输出缓冲区大小
 * @param actual_len 实际读取的数据长度
 * @param timeout_ms 超时时间（毫秒），0表示不阻塞
 * @return esp_err_t ESP_OK成功，ESP_ERR_TIMEOUT超时
 */
esp_err_t opus_buffer_read(opus_buffer_handle_t buffer, 
                           uint8_t *out, 
                           size_t max_len,
                           size_t *actual_len,
                           uint32_t timeout_ms);

/**
 * @brief 获取缓冲区中的包数量
 * 
 * @param buffer 缓冲区句柄
 * @return size_t 包数量
 */
size_t opus_buffer_get_count(opus_buffer_handle_t buffer);

/**
 * @brief 清空缓冲区
 * 
 * @param buffer 缓冲区句柄
 * @return esp_err_t ESP_OK成功
 */
esp_err_t opus_buffer_clear(opus_buffer_handle_t buffer);

#ifdef __cplusplus
}
#endif

