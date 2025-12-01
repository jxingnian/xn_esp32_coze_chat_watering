/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-28 21:58:13
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-10-28 22:04:51
 * @FilePath: \ESP_ChunFeng\components\coze_chat\base64_codec.h
 * @Description: 这是默认设置,请设置`customMade`, 打开koroFileHeader查看配置 进行设置: https://github.com/OBKoro1/koro1FileHeader/wiki/%E9%85%8D%E7%BD%AE
 */
/*
 * @Author: AI Assistant
 * @Description: Base64 编解码模块 - 用于音频数据的 Base64 转换
 * 
 * 本模块提供 Base64 编码和解码功能，专门用于音频数据的传输。
 * 使用 mbedtls 库实现，支持 PSRAM 分配，适合嵌入式环境。
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Base64 编码音频数据
 * 
 * 将原始二进制音频数据编码为 Base64 字符串格式，用于通过文本协议传输。
 * 
 * @param data 原始音频数据指针
 * @param len 数据长度（字节）
 * @param out_len 输出参数，返回编码后的 Base64 字符串长度
 * @return char* Base64 编码后的字符串（以 '\0' 结尾），失败返回 NULL
 * 
 * @note 返回的字符串使用 PSRAM 分配，调用者需要使用 heap_caps_free() 释放
 * @note 编码公式：输出长度 ≈ (输入长度 * 4 / 3) 向上取整到4的倍数
 */
char* base64_encode_audio(const uint8_t *data, size_t len, size_t *out_len);

/**
 * @brief Base64 解码音频数据
 * 
 * 将 Base64 编码的字符串解码为原始二进制音频数据。
 * 
 * @param base64_str Base64 编码的字符串（以 '\0' 结尾）
 * @param out_len 输出参数，返回解码后的数据长度
 * @return uint8_t* 解码后的音频数据，失败返回 NULL
 * 
 * @note 返回的数据使用 PSRAM 分配，调用者需要使用 heap_caps_free() 释放
 * @note 解码公式：输出长度 ≈ (输入长度 * 3 / 4)
 */
uint8_t* base64_decode_audio(const char *base64_str, size_t *out_len);

/**
 * @brief 计算 Base64 编码后的长度（不执行实际编码）
 * 
 * @param data_len 原始数据长度
 * @return size_t Base64 编码后的长度（不包含 '\0'）
 */
size_t base64_get_encode_length(size_t data_len);

/**
 * @brief 计算 Base64 解码后的最大长度（不执行实际解码）
 * 
 * @param base64_len Base64 字符串长度
 * @return size_t 解码后的最大数据长度
 */
size_t base64_get_decode_length(size_t base64_len);

#ifdef __cplusplus
}
#endif

