/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-24
 * @Description: WebSocket客户端（基于esp_websocket_client）
 * 
 * 统一网络架构：WiFi和4G（通过USB RNDIS）都使用此实现
 */

#include "coze_websocket.h"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "COZE_WS";

CozeWebSocket::CozeWebSocket()
    : client_(nullptr)
{
}

CozeWebSocket::~CozeWebSocket()
{
    Close();
}

void CozeWebSocket::SetHeader(const char* key, const char* value)
{
    headers_[key] = value;
}

bool CozeWebSocket::Connect(const std::string& url)
{
    if (client_) {
        ESP_LOGW(TAG, "WebSocket已连接，先关闭旧连接");
        Close();
    }
    
    // 配置WebSocket客户端（参考ESP-IDF官方文档）
    esp_websocket_client_config_t ws_cfg = {};
    ws_cfg.uri = url.c_str();
    
    // 缓冲区配置
    ws_cfg.buffer_size = 16384;                     // 接收缓冲区16KB
    
    // WebSocket心跳配置（官方推荐）
    ws_cfg.ping_interval_sec = 10;                  // 每10秒发送Ping（防止服务器超时断开）
    ws_cfg.disable_pingpong_discon = false;         // Ping超时后自动断开重连
    
    // 网络超时配置
    ws_cfg.network_timeout_ms = 10000;              // 网络操作超时10秒
    ws_cfg.reconnect_timeout_ms = 10000;            // 重连间隔10秒
    
    // TCP KeepAlive配置（底层TCP连接保活）
    ws_cfg.keep_alive_enable = true;                // 启用TCP KeepAlive
    ws_cfg.keep_alive_idle = 5;                     // 空闲5秒开始探测
    ws_cfg.keep_alive_interval = 5;                 // 探测间隔5秒
    ws_cfg.keep_alive_count = 3;                    // 3次失败后断开
    
    // 创建客户端
    client_ = esp_websocket_client_init(&ws_cfg);
    if (!client_) {
        ESP_LOGE(TAG, "WebSocket客户端初始化失败");
        return false;
    }
    
    // 注册事件处理器
    esp_websocket_register_events(client_, WEBSOCKET_EVENT_ANY, 
                                  websocket_event_handler, this);
    
    // 设置请求头
    for (const auto& header : headers_) {
        esp_websocket_client_append_header(client_, header.first.c_str(), header.second.c_str());
    }
    
    // 启动连接
    esp_err_t ret = esp_websocket_client_start(client_);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WebSocket启动失败: %s", esp_err_to_name(ret));
        esp_websocket_client_destroy(client_);
        client_ = nullptr;
        return false;
    }
    
    ESP_LOGI(TAG, "WebSocket连接成功 (Ping=%ds, KeepAlive=%d/%d/%d)", 
             ws_cfg.ping_interval_sec, ws_cfg.keep_alive_idle, 
             ws_cfg.keep_alive_interval, ws_cfg.keep_alive_count);
    return true;
}

bool CozeWebSocket::Send(const std::string& message)
{
    if (!client_ || !esp_websocket_client_is_connected(client_)) {
        ESP_LOGE(TAG, "WebSocket未连接");
        return false;
    }
    
    int ret = esp_websocket_client_send_text(client_, message.c_str(), 
                                             message.length(), portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "发送消息失败");
        return false;
    }
    
    // ESP_LOGI(TAG, "发送消息成功: %d字节", ret);
    return true;
}

void CozeWebSocket::Close()
{
    if (client_) {
        esp_websocket_client_close(client_, portMAX_DELAY);
        esp_websocket_client_destroy(client_);
        client_ = nullptr;
        ESP_LOGI(TAG, "WebSocket已关闭");
    }
}

void CozeWebSocket::OnConnected(std::function<void()> callback)
{
    on_connected_ = callback;
}

void CozeWebSocket::OnDisconnected(std::function<void()> callback)
{
    on_disconnected_ = callback;
}

void CozeWebSocket::OnData(std::function<void(const char*, size_t, bool)> callback)
{
    on_data_ = callback;
}

void CozeWebSocket::OnError(std::function<void(int)> callback)
{
    on_error_ = callback;
}

void CozeWebSocket::websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                                int32_t event_id, void *event_data)
{
    CozeWebSocket *self = static_cast<CozeWebSocket*>(handler_args);
    esp_websocket_event_data_t *data = static_cast<esp_websocket_event_data_t*>(event_data);
    
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "✅ WebSocket已连接");
            if (self->on_connected_) {
                self->on_connected_();
            }
            break;
            
        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WebSocket已断开");
            if (self->on_disconnected_) {
                self->on_disconnected_();
            }
            break;
            
        case WEBSOCKET_EVENT_DATA:
            if (data && data->data_ptr && data->data_len > 0) {
                bool is_binary = (data->op_code == 0x02);
                
                // 🔧 处理WebSocket消息分片
                // ESP-IDF会将大消息分片传递（16KB缓冲区），需要累积到完整消息
                if (data->payload_offset == 0) {
                    // 新消息开始
                    self->fragment_buffer_.clear();
                    self->fragment_buffer_.reserve(data->payload_len);  // 预分配空间
                    
                    // 调试信息：如果消息需要分片
                    if (data->payload_len > 16384) {
                        ESP_LOGD(TAG, "📦 大消息开始: %d字节 (需要%d个分片)", 
                                (int)data->payload_len, (int)((data->payload_len + 16383) / 16384));
                    }
                }
                
                // 累积当前分片
                self->fragment_buffer_.append(static_cast<const char*>(data->data_ptr), data->data_len);
                
                // 检查是否收到完整消息
                if (self->fragment_buffer_.length() >= data->payload_len) {
                    // 完整消息已接收，触发回调
                    if (self->on_data_) {
                        self->on_data_(self->fragment_buffer_.c_str(), 
                                      self->fragment_buffer_.length(), is_binary);
                    }
                    self->fragment_buffer_.clear();  // 清空缓冲区准备下一条消息
                } else {
                    // 调试信息：等待更多分片
                    ESP_LOGV(TAG, "📦 累积分片: %d/%d 字节", 
                            (int)self->fragment_buffer_.length(), (int)data->payload_len);
                }
            }
            break;
            
        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket错误");
            if (self->on_error_) {
                self->on_error_(-1);
            }
            break;
            
        default:
            break;
    }
}


