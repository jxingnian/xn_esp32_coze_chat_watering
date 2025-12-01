/*
 * @Author: AI Assistant
 * @Description: 简单环形缓冲区 - 用于音频流式传输
 * 
 * 特点：
 * - 预分配固定大小（避免 malloc/free）
 * - 线程安全（互斥锁保护）
 * - 自动覆盖旧数据（环形特性）
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 环形缓冲区句柄（不透明类型）
 */
typedef struct simple_ring_buffer_s *simple_ring_buffer_handle_t;

/**
 * @brief 创建环形缓冲区
 * 
 * @param size 缓冲区大小（字节），建议 8KB-16KB
 * @return simple_ring_buffer_handle_t 缓冲区句柄，失败返回 NULL
 * 
 * @note 缓冲区在 PSRAM 中分配
 */
simple_ring_buffer_handle_t simple_ring_buffer_create(size_t size);

/**
 * @brief 销毁环形缓冲区
 * 
 * @param rb 缓冲区句柄
 */
void simple_ring_buffer_destroy(simple_ring_buffer_handle_t rb);

/**
 * @brief 写入数据到环形缓冲区
 * 
 * @param rb 缓冲区句柄
 * @param data 数据指针
 * @param len 数据长度
 * @return esp_err_t ESP_OK 成功
 * 
 * @note 如果空间不足会自动覆盖旧数据
 */
esp_err_t simple_ring_buffer_write(simple_ring_buffer_handle_t rb, 
                                    const uint8_t *data, size_t len);

/**
 * @brief 从环形缓冲区读取数据
 * 
 * @param rb 缓冲区句柄
 * @param out 输出缓冲区
 * @param len 期望读取的长度
 * @param timeout_ms 超时时间（毫秒），0 表示不等待
 * @return size_t 实际读取的字节数，0 表示无数据或超时
 */
size_t simple_ring_buffer_read(simple_ring_buffer_handle_t rb, 
                                uint8_t *out, size_t len, uint32_t timeout_ms);

/**
 * @brief 获取可用数据量
 * 
 * @param rb 缓冲区句柄
 * @return size_t 可读取的字节数
 */
size_t simple_ring_buffer_available(simple_ring_buffer_handle_t rb);

/**
 * @brief 清空缓冲区
 * 
 * @param rb 缓冲区句柄
 */
void simple_ring_buffer_clear(simple_ring_buffer_handle_t rb);

#ifdef __cplusplus
}
#endif

