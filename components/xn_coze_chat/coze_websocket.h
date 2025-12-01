/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-24
 * @Description: WebSocket客户端（基于esp_websocket_client）
 * 
 * 统一网络架构：WiFi和4G（通过USB RNDIS）都使用此实现
 */
#pragma once

#include "esp_websocket_client.h"
#include <functional>
#include <map>
#include <string>

/**
 * @brief WebSocket 客户端
 */
class CozeWebSocket
{
public:
    CozeWebSocket();
    ~CozeWebSocket();

    void SetHeader(const char *key, const char *value);
    bool Connect(const std::string &url);
    bool Send(const std::string &message);
    void Close();

    void OnConnected(std::function<void()> callback);
    void OnDisconnected(std::function<void()> callback);
    void OnData(std::function<void(const char *, size_t, bool binary)> callback);
    void OnError(std::function<void(int)> callback);

private:
    esp_websocket_client_handle_t client_;
    std::map<std::string, std::string> headers_;

    std::function<void()> on_connected_;
    std::function<void()> on_disconnected_;
    std::function<void(const char *, size_t, bool)> on_data_;
    std::function<void(int)> on_error_;

    // 消息分片缓冲区（用于拼接分片消息）
    std::string fragment_buffer_;

    static void websocket_event_handler(void *handler_args, esp_event_base_t base,
                                        int32_t event_id, void *event_data);
};

