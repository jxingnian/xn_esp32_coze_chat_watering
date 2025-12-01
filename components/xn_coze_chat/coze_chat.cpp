/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-10-24
 * @Description: Coze Chat组件实现 - 支持WiFi和4G（重构版本）
 * 
 * 改进：
 * 1. 集成环形缓冲区（防止JSON分割）
 * 2. 集成Opus解码器
 * 3. 修复Coze协议事件类型
 * 4. 添加VAD转检测支持
 * 5. 独立解析任务
 */

#include "coze_chat.h"
#include "coze_websocket.h"
#include "base64_codec.h"
#include "audio_uplink.h"
#include "audio_downlink.h"
#include "simple_ring_buffer.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>
#include <string>
#include <memory>

static const char *TAG = "COZE_CHAT";

// Coze WebSocket服务器地址（双向流式语音对话）
#define COZE_WEBSOCKET_URL "wss://ws.coze.cn/v1/chat"

/**
 * @brief Coze聊天内部结构
 * 
 * 该结构体封装了Coze Chat组件的所有内部状态和资源：
 * - 网络连接（WiFi或4G）
 * - WebSocket通信接口
 * - 音频编解码器
 * - JSON消息解析
 * - 用户回调函数
 */
struct coze_chat_t {
    // WebSocket客户端（统一使用标准TCP/IP栈，USB RNDIS使4G也走相同路径）
    std::unique_ptr<CozeWebSocket> websocket;
    
    // 音频上行模块（负责编码和发送）
    audio_uplink_handle_t audio_uplink;
    
    // 音频下行模块（负责解码和回调）
    audio_downlink_handle_t audio_downlink;
    
    // JSON解析任务句柄
    TaskHandle_t parser_task;
    // JSON解析任务运行标志
    bool parser_running;
    
    // WebSocket数据接收环形缓冲区（替代队列，零拷贝架构）
    simple_ring_buffer_handle_t ws_ring_buffer;
    
    // 配置参数（从用户传入的config复制）
    coze_chat_config_t config;
    
    // 连接状态
    bool connected;              // WebSocket是否已连接
    bool session_created;        // 会话是否已创建
    char session_id[64];         // 会话ID
    char conversation_id[64];    // 对话ID
    
    // 回调函数
    coze_audio_callback_t audio_callback;      // 音频数据回调
    coze_event_callback_t event_callback;       // 事件回调
    coze_ws_event_callback_t ws_event_callback; // WebSocket事件回调
};

// ============ 内部辅助函数 ============

/**
 * @brief 处理Coze服务器消息
 * 
 * 解析并处理从Coze服务器接收到的JSON消息，根据event_type字段分发到不同的处理逻辑。
 * 
 * @param handle Coze Chat句柄
 * @param message 接收到的JSON消息字符串
 */
static void handle_coze_message(coze_chat_handle_t handle, const std::string &message)
{
    // ⚠️ 屏蔽高频日志：每个JSON包都打印会导致UART溢出
    // ESP_LOGI(TAG, "📨 收到消息: %.*s", message.length() > 200 ? 200 : (int)message.length(), message.c_str());
    
    // 解析JSON
    cJSON *root = cJSON_Parse(message.c_str());
    if (!root) {
        // 理论上不应该再出现JSON解析失败（已修复消息分片问题）
        ESP_LOGE(TAG, "❌ JSON解析失败 (长度: %d)", (int)message.length());
        ESP_LOGD(TAG, "数据前缀: %.*s...", 100, message.c_str());
        return;
    }
    
    // 获取事件类型（Coze使用 "event_type" 字段）
    cJSON *event_type_item = cJSON_GetObjectItem(root, "event_type");
    if (!event_type_item || !cJSON_IsString(event_type_item)) {
        cJSON_Delete(root);
        return;
    }
    
    std::string event_type = event_type_item->valuestring;
    
    // 只对非 delta 事件打印事件类型（避免刷屏）
    if (event_type != "conversation.message.delta" && 
        event_type != "conversation.audio.delta" &&
        event_type != "conversation.audio_transcript.update") {
        ESP_LOGI(TAG, "📩 事件类型: %s", event_type.c_str());
    }
    
    // 处理不同类型的事件
    if (event_type == "chat.created") {
        // 对话连接成功
        ESP_LOGI(TAG, "✅ 对话连接成功");
        handle->session_created = true;
        
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_CREATE, NULL, NULL);
        }
    }
    else if (event_type == "chat.updated") {
        // 对话配置成功
        ESP_LOGI(TAG, "✅ 对话配置成功");
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_UPDATE, NULL, NULL);
        }
    }
    else if (event_type == "conversation.chat.created") {
        // 对话开始
        ESP_LOGI(TAG, "✅ 对话开始");
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_CREATE, NULL, NULL);
        }
    }
    else if (event_type == "conversation.audio.delta") {
        // 增量音频数据（Opus编码，Base64）
        // ⚠️ 屏蔽高频日志：每个音频包（60ms）打印会导致UART溢出
        
        cJSON *data_item = cJSON_GetObjectItem(root, "data");
        if (!data_item) {
            ESP_LOGW(TAG, "⚠️ 音频事件缺少data字段");
            return;
        }
        
        cJSON *content = cJSON_GetObjectItem(data_item, "content");
        if (!content || !cJSON_IsString(content)) {
            ESP_LOGW(TAG, "⚠️ 音频事件缺少content字段");
            return;
        }
        
        const char *audio_base64 = content->valuestring;
        
        // 使用音频下行模块处理（Base64解码 → Opus解码 → PCM回调）
        if (handle->audio_downlink) {
            audio_downlink_process(handle->audio_downlink, audio_base64);
        }
    }
    else if (event_type == "input_audio_buffer.speech_started") {
        // 用户开始说话（server_vad模式）
        ESP_LOGI(TAG, "🗣️  用户开始说话");
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_SPEECH_STARTED, NULL, NULL);
        }
    }
    else if (event_type == "input_audio_buffer.speech_stopped") {
        // 用户结束说话（server_vad模式）
        ESP_LOGI(TAG, "🔇 用户结束说话");
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_SPEECH_STOPED, NULL, NULL);
        }
    }
    else if (event_type == "input_audio_buffer.completed") {
        // input_audio_buffer 提交成功
        ESP_LOGI(TAG, "✅ 音频提交成功");
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_INPUT_AUDIO_BUFFER_COMPLETED, NULL, NULL);
        }
    }
    else if (event_type == "conversation.message.delta") {
        // 增量消息（文本）- Coze流式返回的文本片段
        cJSON *data_item = cJSON_GetObjectItem(root, "data");
        if (data_item) {
            cJSON *delta = cJSON_GetObjectItem(data_item, "delta");
            if (delta && cJSON_IsString(delta)) {
                // 打印文本内容（不换行，模拟流式输出效果）
                printf("%s", delta->valuestring);
                fflush(stdout);
            }
        }
    }
    else if (event_type == "conversation.message.completed") {
        // 消息完成 - 换行结束流式输出
        printf("\n");
        ESP_LOGI(TAG, "✅ 消息完成");
    }
    else if (event_type == "conversation.audio.completed") {
        // 语音回复完成
        ESP_LOGI(TAG, "✅ 语音回复完成");
    }
    else if (event_type == "conversation.chat.completed") {
        // 对话完成
        ESP_LOGI(TAG, "✅ 对话完成");
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_COMPLETED, NULL, NULL);
        }
    }
    else if (event_type == "conversation.chat.failed") {
        // 对话失败
        ESP_LOGE(TAG, "❌ 对话失败");
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_ERROR, NULL, NULL);
        }
    }
    else if (event_type == "conversation.audio.sentence_start") {
        // 增量语音字幕
        cJSON *data_item = cJSON_GetObjectItem(root, "data");
        if (data_item && handle->config.enable_subtitle) {
            cJSON *text = cJSON_GetObjectItem(data_item, "text");
            if (text && cJSON_IsString(text) && handle->event_callback) {
                ESP_LOGI(TAG, "📝 字幕: %s", text->valuestring);
                handle->event_callback(COZE_CHAT_EVENT_CHAT_SUBTITLE_EVENT, text->valuestring, NULL);
            }
        }
    }
    else if (event_type == "conversation.audio_transcript.update") {
        // 用户语音识别字幕（中间值）- 实时显示识别结果
        cJSON *data_item = cJSON_GetObjectItem(root, "data");
        if (data_item) {
            cJSON *transcript = cJSON_GetObjectItem(data_item, "transcript");
            if (transcript && cJSON_IsString(transcript)) {
                ESP_LOGI(TAG, "🎤 识别中: %s", transcript->valuestring);
            }
        }
    }
    else if (event_type == "conversation.audio_transcript.completed") {
        // 用户语音识别完成
        ESP_LOGI(TAG, "✅ 用户语音识别完成");
        
        // 打印识别结果的详细信息
        cJSON *data_item = cJSON_GetObjectItem(root, "data");
        cJSON *detail_item = cJSON_GetObjectItem(root, "detail");
        
        if (data_item) {
            cJSON *content = cJSON_GetObjectItem(data_item, "content");
            if (content && cJSON_IsString(content)) {
                ESP_LOGI(TAG, "📝 识别内容: %s", content->valuestring);
            }
        }
        
        if (detail_item) {
            cJSON *logid = cJSON_GetObjectItem(detail_item, "logid");
            if (logid && cJSON_IsString(logid)) {
                ESP_LOGI(TAG, "🔑 logid: %s", logid->valuestring);
            }
        }
    }
    else if (event_type == "conversation.chat.canceled") {
        // 智能体输出中断
        ESP_LOGI(TAG, "⚠️  对话已中断");
    }
    else if (event_type == "input_audio_buffer.cleared") {
        // input_audio_buffer 清除成功
        ESP_LOGI(TAG, "✅ 音频缓冲区已清除");
    }
    else if (event_type == "conversation.cleared") {
        // 上下文清除完成
        ESP_LOGI(TAG, "✅ 上下文已清除");
    }
    else if (event_type == "error") {
        // 错误事件 - 打印详细错误信息
        ESP_LOGE(TAG, "❌ 收到错误事件");
        
        cJSON *error = cJSON_GetObjectItem(root, "error");
        if (error) {
            // 打印完整的error对象
            char *error_str = cJSON_Print(error);
            if (error_str) {
                ESP_LOGE(TAG, "错误详情: %s", error_str);
                free(error_str);
            }
            
            // 提取常见字段
            cJSON *code = cJSON_GetObjectItem(error, "code");
            cJSON *message = cJSON_GetObjectItem(error, "message");
            cJSON *type = cJSON_GetObjectItem(error, "type");
            
            if (code && cJSON_IsString(code)) {
                ESP_LOGE(TAG, "错误代码: %s", code->valuestring);
            }
            if (message && cJSON_IsString(message)) {
                ESP_LOGE(TAG, "错误消息: %s", message->valuestring);
            }
            if (type && cJSON_IsString(type)) {
                ESP_LOGE(TAG, "错误类型: %s", type->valuestring);
            }
        } else {
            // 打印整个消息
            ESP_LOGE(TAG, "完整错误消息: %s", message.c_str());
        }
        
        if (handle->event_callback) {
            handle->event_callback(COZE_CHAT_EVENT_CHAT_ERROR, NULL, NULL);
        }
    }
    else {
        ESP_LOGI(TAG, "未处理的事件类型: %s", event_type.c_str());
    }
    
    cJSON_Delete(root);
}

/**
 * @brief WebSocket 发送回调（给 audio_uplink 使用）
 * 
 * @param json_str JSON 字符串
 * @param user_ctx 用户上下文（coze_chat_handle_t）
 * @return true 发送成功
 */
static bool websocket_send_callback(const char *json_str, void *user_ctx)
{
    coze_chat_handle_t handle = (coze_chat_handle_t)user_ctx;
    
    if (!handle || !handle->websocket) {
        return false;
    }
    
    return handle->websocket->Send(json_str);
}


/**
 * @brief JSON解析任务（环形缓冲区架构）
 * 
 * 从环形缓冲区读取JSON消息并解析。
 * 消息格式：[长度2字节][JSON数据][长度2字节][JSON数据]...
 * 
 * @param param 任务参数（coze_chat_handle_t）
 */
static void json_parser_task(void *param)
{
    coze_chat_handle_t handle = (coze_chat_handle_t)param;
    
    ESP_LOGI(TAG, "🚀🚀🚀 JSON解析任务启动（环形缓冲区架构）🚀🚀🚀");
    
    uint32_t packet_count = 0;
    
    // 预分配临时缓冲区（最大支持32KB的JSON消息）
    const size_t MAX_JSON_SIZE = 32 * 1024;
    uint8_t *json_buffer = (uint8_t *)heap_caps_malloc(MAX_JSON_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!json_buffer) {
        ESP_LOGE(TAG, "❌ JSON临时缓冲区分配失败");
        vTaskDelete(NULL);
        return;
    }
    
    while (handle->parser_running) {
        // ✅ 步骤1：读取消息长度（2字节，阻塞等待）
        uint16_t msg_len = 0;
        size_t got = simple_ring_buffer_read(
            handle->ws_ring_buffer,
            (uint8_t *)&msg_len,
            sizeof(uint16_t),
            portMAX_DELAY  // 阻塞等待
        );
        
        if (got != sizeof(uint16_t)) {
            // ESP_LOGW(TAG, "⚠️ 读取消息长度失败");
            continue;
        }
        
        // 检查长度合法性
        if (msg_len == 0 || msg_len > MAX_JSON_SIZE) {
            ESP_LOGE(TAG, "❌ 非法的消息长度: %d", msg_len);
            continue;
        }
        
        // ✅ 步骤2：读取JSON数据（阻塞等待）
        got = simple_ring_buffer_read(
            handle->ws_ring_buffer,
            json_buffer,
            msg_len,
            portMAX_DELAY  // 阻塞等待
        );
        
        if (got != msg_len) {
            ESP_LOGW(TAG, "⚠️ JSON数据读取不完整: %d/%d", got, msg_len);
            continue;
        }
        
        packet_count++;
        
        // ✅ 步骤3：解析JSON
        std::string message((char *)json_buffer, msg_len);
        handle_coze_message(handle, message);
        
        // 每100包打印统计（避免刷屏）
        if (packet_count % 100 == 0) {
            size_t available = simple_ring_buffer_available(handle->ws_ring_buffer);
            ESP_LOGI(TAG, "📊 已处理 %lu 包，缓冲区剩余: %d 字节", 
                     packet_count, available);
        }
    }
    
    // 清理
    heap_caps_free(json_buffer);
    
    ESP_LOGI(TAG, "JSON解析任务退出");
    vTaskDelete(NULL);
}

/**
 * @brief 构建chat.update事件（包含所有高级配置）
 * 
 * 根据用户配置构建完整的chat.update JSON消息，包括：
 * - 对话配置（用户ID、会话ID、元数据等）
 * - 输入/输出音频格式
 * - VAD转检测配置
 * - ASR配置（热词、语言、敏感词过滤等）
 * - 语音处理配置（ANS、PDNS）
 * - 声纹识别配置
 * 
 * @param config 用户配置
 * @return 构建好的JSON字符串
 */
static std::string build_chat_update_event(const coze_chat_config_t *config)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "id", "event_init_001");
    cJSON_AddStringToObject(root, "event_type", "chat.update");
    
    cJSON *data = cJSON_CreateObject();
    
    // ========== 对话配置 ==========
    cJSON *chat_config = cJSON_CreateObject();
    cJSON_AddStringToObject(chat_config, "user_id", config->user_id);
    if (config->conversation_id) {
        cJSON_AddStringToObject(chat_config, "conversation_id", config->conversation_id);
    }
    cJSON_AddBoolToObject(chat_config, "auto_save_history", config->auto_save_history);
    
    // 元数据
    if (config->meta_data_json) {
        cJSON *meta_data = cJSON_Parse(config->meta_data_json);
        if (meta_data) {
            cJSON_AddItemToObject(chat_config, "meta_data", meta_data);
        }
    }
    
    // 自定义变量
    if (config->custom_variables_json) {
        cJSON *custom_vars = cJSON_Parse(config->custom_variables_json);
        if (custom_vars) {
            cJSON_AddItemToObject(chat_config, "custom_variables", custom_vars);
        }
    }
    
    // 额外参数
    if (config->extra_params_json) {
        cJSON *extra_params = cJSON_Parse(config->extra_params_json);
        if (extra_params) {
            cJSON_AddItemToObject(chat_config, "extra_params", extra_params);
        }
    }
    
    // 对话流自定义参数
    if (config->parameters_json) {
        cJSON *parameters = cJSON_Parse(config->parameters_json);
        if (parameters) {
            cJSON_AddItemToObject(chat_config, "parameters", parameters);
        }
    }
    
    cJSON_AddItemToObject(data, "chat_config", chat_config);
    
    // ========== 输入音频格式 ==========
    cJSON *input_audio = cJSON_CreateObject();
    
    // 根据上行音频类型设置格式
    if (config->uplink_audio_type == COZE_CHAT_AUDIO_TYPE_OPUS) {
        // Opus编码：format=pcm（容器格式），codec=opus（编码格式）
        cJSON_AddStringToObject(input_audio, "format", "pcm");
        cJSON_AddStringToObject(input_audio, "codec", "opus");
        cJSON_AddNumberToObject(input_audio, "sample_rate", config->input_sample_rate);
        cJSON_AddNumberToObject(input_audio, "channel", config->input_channel);
    } else {
        // PCM编码：format=pcm，codec=pcm
        cJSON_AddStringToObject(input_audio, "format", "pcm");
        cJSON_AddStringToObject(input_audio, "codec", "pcm");
        cJSON_AddNumberToObject(input_audio, "sample_rate", config->input_sample_rate);
        cJSON_AddNumberToObject(input_audio, "channel", config->input_channel);
        cJSON_AddNumberToObject(input_audio, "bit_depth", config->input_bit_depth);
    }
    
    cJSON_AddItemToObject(data, "input_audio", input_audio);
    
    // ========== 输出音频格式 ==========
    cJSON *output_audio = cJSON_CreateObject();
    if (config->downlink_audio_type == COZE_CHAT_AUDIO_TYPE_OPUS) {
        cJSON_AddStringToObject(output_audio, "codec", "opus");
        cJSON *opus_config = cJSON_CreateObject();
        cJSON_AddNumberToObject(opus_config, "bitrate", config->opus_bitrate);
        cJSON_AddNumberToObject(opus_config, "sample_rate", config->output_sample_rate);
        cJSON_AddNumberToObject(opus_config, "frame_size_ms", config->opus_frame_size_ms);
        if (config->opus_use_cbr) {
            cJSON_AddBoolToObject(opus_config, "use_cbr", true);
        }
        cJSON_AddItemToObject(output_audio, "opus_config", opus_config);
    } else {
        cJSON_AddStringToObject(output_audio, "codec", "pcm");
        cJSON *pcm_config = cJSON_CreateObject();
        cJSON_AddNumberToObject(pcm_config, "sample_rate", config->output_sample_rate);
        cJSON_AddNumberToObject(pcm_config, "frame_size_ms", config->pcm_frame_size_ms);
        cJSON_AddItemToObject(output_audio, "pcm_config", pcm_config);
    }
    
    // 语速
    if (config->speech_rate != 0) {
        cJSON_AddNumberToObject(output_audio, "speech_rate", config->speech_rate);
    }
    
    // 音量
    if (config->loudness_rate != 0) {
        cJSON_AddNumberToObject(output_audio, "loudness_rate", config->loudness_rate);
    }
    
    // 音色
    if (config->voice_id) {
        cJSON_AddStringToObject(output_audio, "voice_id", config->voice_id);
    }
    
    // 情感配置（仅当使用非默认情感或非默认强度时发送）
    if (config->emotion_type != COZE_EMOTION_NEUTRAL || config->emotion_scale != 4.0f) {
        cJSON *emotion_config = cJSON_CreateObject();
        const char *emotion_names[] = {
            "happy", "sad", "angry", "surprised", "fear", 
            "hate", "excited", "coldness", "neutral"
        };
        cJSON_AddStringToObject(emotion_config, "emotion", emotion_names[config->emotion_type]);
        cJSON_AddNumberToObject(emotion_config, "emotion_scale", config->emotion_scale);
        cJSON_AddItemToObject(output_audio, "emotion_config", emotion_config);
    }
    
    cJSON_AddItemToObject(data, "output_audio", output_audio);
    
    // ========== 语音处理配置 ==========
    if (config->voice_processing_enable_ans || config->voice_processing_enable_pdns) {
        cJSON *voice_processing = cJSON_CreateObject();
        if (config->voice_processing_enable_ans) {
            cJSON_AddBoolToObject(voice_processing, "enable_ans", true);
        }
        if (config->voice_processing_enable_pdns) {
            cJSON_AddBoolToObject(voice_processing, "enable_pdns", true);
            if (config->voice_print_feature_id) {
                cJSON_AddStringToObject(voice_processing, "voice_print_feature_id", config->voice_print_feature_id);
            }
        }
        cJSON_AddItemToObject(data, "voice_processing_config", voice_processing);
    }
    
    // ========== VAD转检测配置 ==========
    cJSON *turn_detection = cJSON_CreateObject();
    if (config->turn_detection_type == COZE_TURN_DETECTION_SERVER_VAD) {
        cJSON_AddStringToObject(turn_detection, "type", "server_vad");
        cJSON_AddNumberToObject(turn_detection, "prefix_padding_ms", config->vad_prefix_padding_ms);
        cJSON_AddNumberToObject(turn_detection, "silence_duration_ms", config->vad_silence_duration_ms);
        
        // 打断配置
        if (config->interrupt_keywords && config->interrupt_keyword_count > 0) {
            cJSON *interrupt_config = cJSON_CreateObject();
            const char *mode = (config->interrupt_mode == COZE_INTERRUPT_MODE_PREFIX) ? 
                              "keyword_prefix" : "keyword_contains";
            cJSON_AddStringToObject(interrupt_config, "mode", mode);
            cJSON *keywords = cJSON_CreateArray();
            for (int i = 0; i < config->interrupt_keyword_count; i++) {
                cJSON_AddItemToArray(keywords, cJSON_CreateString(config->interrupt_keywords[i]));
            }
            cJSON_AddItemToObject(interrupt_config, "keywords", keywords);
            cJSON_AddItemToObject(turn_detection, "interrupt_config", interrupt_config);
        }
    } else if (config->turn_detection_type == COZE_TURN_DETECTION_CLIENT_INTERRUPT) {
        cJSON_AddStringToObject(turn_detection, "type", "client_interrupt");
    } else {
        // semantic_vad模式
        cJSON_AddStringToObject(turn_detection, "type", "semantic_vad");
        cJSON *semantic_config = cJSON_CreateObject();
        cJSON_AddNumberToObject(semantic_config, "silence_threshold_ms", 
                               config->semantic_vad_silence_threshold_ms);
        cJSON_AddNumberToObject(semantic_config, "semantic_unfinished_wait_time_ms", 
                               config->semantic_vad_unfinished_wait_time_ms);
        cJSON_AddItemToObject(turn_detection, "semantic_vad_config", semantic_config);
    }
    cJSON_AddItemToObject(data, "turn_detection", turn_detection);
    
    // ========== ASR配置 ==========
    cJSON *asr_config = cJSON_CreateObject();
    
    // 热词
    if (config->asr_hot_words && config->asr_hot_word_count > 0) {
        cJSON *hot_words = cJSON_CreateArray();
        for (int i = 0; i < config->asr_hot_word_count; i++) {
            cJSON_AddItemToArray(hot_words, cJSON_CreateString(config->asr_hot_words[i]));
        }
        cJSON_AddItemToObject(asr_config, "hot_words", hot_words);
    }
    
    // 上下文
    if (config->asr_context) {
        cJSON_AddStringToObject(asr_config, "context", config->asr_context);
    }
    
    // 语言（官方标准格式）
    const char *lang_names[] = {
        "common",   // COZE_USER_LANG_COMMON
        "en-US",    // COZE_USER_LANG_EN_US
        "ja-JP",    // COZE_USER_LANG_JA_JP
        "id-ID",    // COZE_USER_LANG_ID_ID
        "es-MX",    // COZE_USER_LANG_ES_MX
        "pt-BR",    // COZE_USER_LANG_PT_BR
        "de-DE",    // COZE_USER_LANG_DE_DE
        "fr-FR",    // COZE_USER_LANG_FR_FR
        "ko-KR",    // COZE_USER_LANG_KO_KR
        "fil-PH",   // COZE_USER_LANG_FIL_PH
        "ms-MY",    // COZE_USER_LANG_MS_MY
        "th-TH",    // COZE_USER_LANG_TH_TH
        "ar-SA"     // COZE_USER_LANG_AR_SA
    };
    cJSON_AddStringToObject(asr_config, "user_language", lang_names[config->asr_language]);
    
    // ASR选项
    cJSON_AddBoolToObject(asr_config, "enable_ddc", config->asr_enable_ddc);
    cJSON_AddBoolToObject(asr_config, "enable_itn", config->asr_enable_itn);
    cJSON_AddBoolToObject(asr_config, "enable_punc", config->asr_enable_punc);
    
    // ASR模式
    if (config->asr_stream_mode) {
        cJSON_AddStringToObject(asr_config, "stream_mode", config->asr_stream_mode);
    }
    
    // 二次识别
    if (config->asr_enable_nostream) {
        cJSON_AddBoolToObject(asr_config, "enable_nostream", true);
    }
    
    // 情绪识别
    if (config->asr_enable_emotion) {
        cJSON_AddBoolToObject(asr_config, "enable_emotion", true);
    }
    
    // 性别识别
    if (config->asr_enable_gender) {
        cJSON_AddBoolToObject(asr_config, "enable_gender", true);
    }
    
    // 敏感词过滤
    if (config->asr_system_reserved_filter || 
        (config->asr_filter_with_empty && config->asr_filter_with_empty_count > 0) ||
        (config->asr_filter_with_signed && config->asr_filter_with_signed_count > 0)) {
        cJSON *sensitive_filter = cJSON_CreateObject();
        
        if (config->asr_system_reserved_filter) {
            cJSON_AddBoolToObject(sensitive_filter, "system_reserved_filter", true);
        }
        
        if (config->asr_filter_with_empty && config->asr_filter_with_empty_count > 0) {
            cJSON *empty_arr = cJSON_CreateArray();
            for (int i = 0; i < config->asr_filter_with_empty_count; i++) {
                cJSON_AddItemToArray(empty_arr, cJSON_CreateString(config->asr_filter_with_empty[i]));
            }
            cJSON_AddItemToObject(sensitive_filter, "filter_with_empty", empty_arr);
        }
        
        if (config->asr_filter_with_signed && config->asr_filter_with_signed_count > 0) {
            cJSON *signed_arr = cJSON_CreateArray();
            for (int i = 0; i < config->asr_filter_with_signed_count; i++) {
                cJSON_AddItemToArray(signed_arr, cJSON_CreateString(config->asr_filter_with_signed[i]));
            }
            cJSON_AddItemToObject(sensitive_filter, "filter_with_signed", signed_arr);
        }
        
        cJSON_AddItemToObject(asr_config, "sensitive_words_filter", sensitive_filter);
    }
    
    cJSON_AddItemToObject(data, "asr_config", asr_config);
    
    // ========== 开场白配置 ==========
    if (config->need_play_prologue) {
        cJSON_AddBoolToObject(data, "need_play_prologue", true);
        if (config->prologue_content) {
            cJSON_AddStringToObject(data, "prologue_content", config->prologue_content);
        }
    }
    
    // ========== 声纹识别配置 ==========
    if (config->voice_print_group_id) {
        cJSON *voice_print = cJSON_CreateObject();
        cJSON_AddStringToObject(voice_print, "group_id", config->voice_print_group_id);
        cJSON_AddNumberToObject(voice_print, "score", config->voice_print_score);
        cJSON_AddBoolToObject(voice_print, "reuse_voice_info", config->voice_print_reuse_info);
        cJSON_AddItemToObject(data, "voice_print_config", voice_print);
    }
    
    cJSON_AddItemToObject(root, "data", data);
    
    char *json_str = cJSON_PrintUnformatted(root);
    std::string result(json_str);
    
    free(json_str);
    cJSON_Delete(root);
    
    return result;
}

// ============ 公共API实现 ============

/**
 * @brief 初始化Coze Chat组件
 * 
 * 创建并初始化Coze Chat组件，包括：
 * - 分配内存
 * - 创建环形缓冲区
 * - 创建Opus解码器（如果配置了Opus音频）
 * - 初始化4G模组（如果是4G模式）
 * 
 * @param config 配置参数
 * @param handle 输出参数，返回的句柄
 * @return ESP_OK成功，其他值表示失败
 */
extern "C" esp_err_t coze_chat_init(const coze_chat_config_t *config, coze_chat_handle_t *handle)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG, "config is NULL");
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(config->bot_id != NULL, ESP_ERR_INVALID_ARG, TAG, "bot_id is NULL");
    ESP_RETURN_ON_FALSE(config->access_token != NULL, ESP_ERR_INVALID_ARG, TAG, "access_token is NULL");
    ESP_RETURN_ON_FALSE(config->user_id != NULL, ESP_ERR_INVALID_ARG, TAG, "user_id is NULL");
    
    ESP_LOGI(TAG, "========== 初始化Coze Chat组件 ==========");
    ESP_LOGI(TAG, "VAD模式: %s (%dms静音)", 
             config->turn_detection_type == COZE_TURN_DETECTION_SERVER_VAD ? "服务器VAD" : "客户端打断",
             config->vad_silence_duration_ms);
    ESP_LOGI(TAG, "音频格式:");
    ESP_LOGI(TAG, "  上行: %s, %dHz, %dbit, %d声道", 
             config->uplink_audio_type == COZE_CHAT_AUDIO_TYPE_OPUS ? "Opus" : "PCM",
             config->input_sample_rate, config->input_bit_depth, config->input_channel);
    ESP_LOGI(TAG, "  下行: %s, %dHz, 比特率=%d", 
             config->downlink_audio_type == COZE_CHAT_AUDIO_TYPE_OPUS ? "Opus" : "PCM",
             config->output_sample_rate, config->opus_bitrate);
    if (config->speech_rate != 0) {
        ESP_LOGI(TAG, "语速: %+d", config->speech_rate);
    }
    if (config->asr_hot_word_count > 0) {
        ESP_LOGI(TAG, "ASR热词数量: %d", config->asr_hot_word_count);
    }
    
    // 分配句柄
    coze_chat_handle_t h = new coze_chat_t();
    if (!h) {
        ESP_LOGE(TAG, "内存分配失败");
        return ESP_ERR_NO_MEM;
    }
    
    // 复制配置
    memcpy(&h->config, config, sizeof(coze_chat_config_t));
    h->audio_callback = config->audio_callback;
    h->event_callback = config->event_callback;
    h->ws_event_callback = config->ws_event_callback;
    
    // 初始化状态
    h->connected = false;
    h->session_created = false;
    h->session_id[0] = '\0';
    h->conversation_id[0] = '\0';
    h->parser_task = NULL;
    h->parser_running = false;
    h->audio_uplink = NULL;
    h->ws_ring_buffer = NULL;
    
    // ========== 1. 创建音频模块 ==========
    
    // 创建音频上行模块（编码和发送）
    audio_uplink_config_t uplink_cfg = {
        .format = (config->uplink_audio_type == COZE_CHAT_AUDIO_TYPE_OPUS) ? 
                  AUDIO_UPLINK_FORMAT_OPUS : AUDIO_UPLINK_FORMAT_PCM,
        .sample_rate = config->input_sample_rate,
        .channels = config->input_channel,
        .bit_depth = config->input_bit_depth,
        .opus_bitrate = 16000,
        .send_callback = websocket_send_callback,
        .send_callback_ctx = h,
    };
    
    h->audio_uplink = audio_uplink_create(&uplink_cfg);
    if (!h->audio_uplink) {
        ESP_LOGE(TAG, "创建音频上行模块失败");
        delete h;
        return ESP_ERR_NO_MEM;
    }
    
    // 创建音频下行模块（解码和回调）
    audio_downlink_config_t downlink_cfg = {
        .sample_rate = config->output_sample_rate,
        .channels = 1,  // 单声道
        .callback = [](const int16_t *pcm, size_t samples, void *ctx) {
            // PCM回调：转发给用户的音频回调
            coze_chat_handle_t h = (coze_chat_handle_t)ctx;
            if (h && h->audio_callback) {
                h->audio_callback((char *)pcm, samples * sizeof(int16_t), NULL);
            }
        },
        .callback_ctx = h,
    };
    
    h->audio_downlink = audio_downlink_create(&downlink_cfg);
    if (!h->audio_downlink) {
        ESP_LOGE(TAG, "创建音频下行模块失败");
        audio_uplink_destroy(h->audio_uplink);
        delete h;
        return ESP_ERR_NO_MEM;
    }
    
    // 统一网络架构：4G通过USB RNDIS虚拟网卡，与WiFi使用相同的网络栈
    ESP_LOGI(TAG, "✅ 网络初始化成功（统一使用标准TCP/IP栈）");
    
    *handle = h;
    ESP_LOGI(TAG, "========================================");
    
    return ESP_OK;
}

/**
 * @brief 启动Coze WebSocket连接
 * 
 * 创建WebSocket连接并启动JSON解析任务：
 * - 根据网络模式创建对应的WebSocket实现
 * - 设置WebSocket回调（连接、数据、断开、错误）
 * - 连接到Coze服务器
 * - 启动JSON解析任务
 * 
 * @param handle Coze Chat句柄
 * @return ESP_OK成功，其他值表示失败
 */
extern "C" esp_err_t coze_chat_start(coze_chat_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    
    ESP_LOGI(TAG, "启动Coze WebSocket连接...");
    
    // ========== 步骤1：创建环形缓冲区和任务（在设置回调之前）==========
    
    // ✅ 创建JSON消息环形缓冲区（256KB，PSRAM）
    // 容量分析：平均JSON ~500字节，256KB可存512个包（约30秒音频）
    handle->ws_ring_buffer = simple_ring_buffer_create(256 * 1024);
    if (!handle->ws_ring_buffer) {
        ESP_LOGE(TAG, "❌ 创建WebSocket环形缓冲区失败");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "✅ WebSocket环形缓冲区创建成功（256KB PSRAM）");
    
    // 启动JSON解析任务（栈使用PSRAM，优先级提高到6确保快速处理）
    handle->parser_running = true;
    
    StaticTask_t *parser_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    StackType_t *parser_stack = (StackType_t *)heap_caps_malloc(handle->config.pull_task_stack_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    
    if (!parser_tcb || !parser_stack) {
        ESP_LOGE(TAG, "❌ JSON解析任务内存分配失败");
        if (parser_tcb) heap_caps_free(parser_tcb);
        if (parser_stack) heap_caps_free(parser_stack);
        simple_ring_buffer_destroy(handle->ws_ring_buffer);
        handle->ws_ring_buffer = NULL;
        return ESP_FAIL;
    }
    
    handle->parser_task = xTaskCreateStaticPinnedToCore(
        json_parser_task,
        "coze_parser",
        handle->config.pull_task_stack_size / sizeof(StackType_t),
        handle,
        6,              // 优先级6（高优先级，确保快速消费队列）
        parser_stack,   // PSRAM栈
        parser_tcb,     // 内部RAM TCB
        0               // Core 0
    );
    
    if (!handle->parser_task) {
        ESP_LOGE(TAG, "❌ 创建JSON解析任务失败");
        heap_caps_free(parser_tcb);
        heap_caps_free(parser_stack);
        simple_ring_buffer_destroy(handle->ws_ring_buffer);
        handle->ws_ring_buffer = NULL;
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "✅ JSON解析任务创建成功（栈%dKB在PSRAM，优先级6）", handle->config.pull_task_stack_size / 1024);
    
    // ========== 步骤2：创建WebSocket实现 ==========
    
    // 统一网络架构：4G通过USB RNDIS虚拟网卡，与WiFi使用相同的网络栈
    ESP_LOGI(TAG, "✅ 网络初始化成功（统一使用标准TCP/IP栈）");
    
    // 创建WebSocket客户端（WiFi和4G统一使用）
    handle->websocket = std::make_unique<CozeWebSocket>();
    
    if (!handle->websocket) {
        ESP_LOGE(TAG, "创建WebSocket失败");
        
        // 清理已创建的资源
        handle->parser_running = false;
        vTaskDelete(handle->parser_task);
        simple_ring_buffer_destroy(handle->ws_ring_buffer);
        handle->ws_ring_buffer = NULL;
        return ESP_FAIL;
    }
    
    // 设置请求头
    std::string auth_header = "Bearer " + std::string(handle->config.access_token);
    handle->websocket->SetHeader("Authorization", auth_header.c_str());
    handle->websocket->SetHeader("User-Agent", "ESP32-Coze/1.0");
    
    // ========== 步骤3：设置WebSocket回调 ==========
    
    handle->websocket->OnConnected([handle]() {
        ESP_LOGI(TAG, "✅ WebSocket已连接");
        handle->connected = true;
        
        if (handle->ws_event_callback) {
            coze_ws_event_t evt = {.handle = handle, .event_id = COZE_WS_EVENT_CONNECTED};
            handle->ws_event_callback(&evt);
        }
        
        // 发送chat.update配置
        std::string config_json = build_chat_update_event(&handle->config);
        ESP_LOGI(TAG, "📤 发送chat.update配置");
        ESP_LOGI(TAG, "配置内容: %s", config_json.c_str());  // 暂时用INFO级别，方便调试
        handle->websocket->Send(config_json);
    });
    
    handle->websocket->OnData([handle](const char *data, size_t length, bool binary) {
        // 只处理文本数据（JSON消息）
        if (!binary && data && length > 0) {
            // ✅ 防御性检查：确保环形缓冲区已创建
            if (!handle->ws_ring_buffer) {
                ESP_LOGW(TAG, "⚠️ 环形缓冲区未初始化，丢弃数据 %d bytes", (int)length);
                return;
            }
            
            // 检查消息长度（最大支持64KB）
            if (length > 65535) {
                ESP_LOGE(TAG, "❌ JSON消息过大: %d bytes", (int)length);
                return;
            }
            
            // ✅ 零拷贝写入环形缓冲区（格式：[长度2字节][数据]）
            uint16_t msg_len = (uint16_t)length;
            
            // 写入长度（2字节）
            esp_err_t ret = simple_ring_buffer_write(
                handle->ws_ring_buffer,
                (const uint8_t *)&msg_len,
                sizeof(uint16_t)
            );
            
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "⚠️ 写入消息长度失败（缓冲区满？）");
                return;
            }
            
            // 写入JSON数据
            ret = simple_ring_buffer_write(
                handle->ws_ring_buffer,
                (const uint8_t *)data,
                length
            );
            
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "⚠️ 写入JSON数据失败（缓冲区满？）");
                // ⚠️ 这里如果失败，会导致长度和数据不匹配
                // 需要清空缓冲区避免数据混乱
                simple_ring_buffer_clear(handle->ws_ring_buffer);
                ESP_LOGE(TAG, "❌ 缓冲区已清空以避免数据混乱");
                return;
            }
            
            // 注意：不打印日志，避免高频刷屏
        }
    });
    
    handle->websocket->OnDisconnected([handle]() {
        ESP_LOGW(TAG, "WebSocket已断开");
        handle->connected = false;
        
        if (handle->ws_event_callback) {
            coze_ws_event_t evt = {.handle = handle, .event_id = COZE_WS_EVENT_DISCONNECTED};
            handle->ws_event_callback(&evt);
        }
    });
    
    handle->websocket->OnError([handle](int error) {
        ESP_LOGE(TAG, "WebSocket错误: %d", error);
        
        if (handle->ws_event_callback) {
            coze_ws_event_t evt = {.handle = handle, .event_id = COZE_WS_EVENT_ERROR};
            handle->ws_event_callback(&evt);
        }
    });
    
    // ========== 步骤4：连接到Coze服务器 ==========
    
    // 连接到Coze服务器（URL中必须包含 bot_id 和 device_id）
    char url_buffer[512];
    snprintf(url_buffer, sizeof(url_buffer), "%s?bot_id=%s&device_id=%s",
             COZE_WEBSOCKET_URL,
             handle->config.bot_id,
             handle->config.user_id);
    std::string url = url_buffer;
    
    ESP_LOGI(TAG, "连接到: %s", url.c_str());
    
    if (!handle->websocket->Connect(url)) {
        ESP_LOGE(TAG, "WebSocket连接失败");
        
        // 清理已创建的资源
        handle->parser_running = false;
        vTaskDelete(handle->parser_task);
        simple_ring_buffer_destroy(handle->ws_ring_buffer);
        handle->ws_ring_buffer = NULL;
        return ESP_FAIL;
    }
    
    // ========== 步骤5：启动音频上行任务 ==========
    
    esp_err_t ret = audio_uplink_start(handle->audio_uplink);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "❌ 启动音频上行任务失败");
        return ret;
    }
    
    ESP_LOGI(TAG, "✅ WebSocket连接已启动");
    return ESP_OK;
}

/**
 * @brief 停止Coze WebSocket连接
 * 
 * 停止JSON解析任务并关闭WebSocket连接。
 * 
 * @param handle Coze Chat句柄
 * @return ESP_OK成功，其他值表示失败
 */
extern "C" esp_err_t coze_chat_stop(coze_chat_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    
    // 停止音频上行任务
    if (handle->audio_uplink) {
        audio_uplink_stop(handle->audio_uplink);
    }
    
    // 停止JSON解析任务
    if (handle->parser_task) {
        handle->parser_running = false;
        vTaskDelay(pdMS_TO_TICKS(100)); // 等待任务退出
        handle->parser_task = NULL;
    }
    
    // ✅ 销毁WebSocket环形缓冲区
    if (handle->ws_ring_buffer) {
        simple_ring_buffer_destroy(handle->ws_ring_buffer);
        handle->ws_ring_buffer = NULL;
        ESP_LOGI(TAG, "环形缓冲区已销毁");
    }
    
    // 关闭WebSocket
    if (handle->websocket) {
        handle->websocket->Close();
        handle->websocket.reset();
    }
    
    handle->connected = false;
    ESP_LOGI(TAG, "Coze WebSocket已停止");
    
    return ESP_OK;
}

/**
 * @brief 反初始化Coze Chat组件
 * 
 * 释放所有资源并删除句柄：
 * - 停止WebSocket连接
 * - 释放环形缓冲区
 * - 释放Opus解码器
 * - 释放4G模组
 * - 删除句柄
 * 
 * @param handle Coze Chat句柄
 * @return ESP_OK成功，其他值表示失败
 */
extern "C" esp_err_t coze_chat_deinit(coze_chat_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    
    // 停止WebSocket
    coze_chat_stop(handle);
    
    // 释放资源
    
    // 销毁音频上行模块
    if (handle->audio_uplink) {
        audio_uplink_destroy(handle->audio_uplink);
        handle->audio_uplink = NULL;
    }
    
    // 销毁音频下行模块
    if (handle->audio_downlink) {
        audio_downlink_destroy(handle->audio_downlink);
        handle->audio_downlink = NULL;
    }
    
    // 注意：USB RNDIS统一网络架构下，不再需要modem对象
    
    // 释放句柄
    delete handle;
    
    ESP_LOGI(TAG, "Coze Chat组件已反初始化");
    return ESP_OK;
}

/**
 * @brief 发送音频数据（异步）
 * 
 * 将PCM音频数据放入队列，由专门的发送任务处理。
 * 这样可以避免阻塞录音回调，提高系统实时性（尤其是4G网络）。
 * 
 * @param handle Coze Chat句柄
 * @param audio_data PCM音频数据
 * @param len 数据长度（字节）
 * @return ESP_OK成功，其他值表示失败
 */
extern "C" esp_err_t coze_chat_send_audio_data(coze_chat_handle_t handle, char *audio_data, int len)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(audio_data != NULL, ESP_ERR_INVALID_ARG, TAG, "audio_data is NULL");
    ESP_RETURN_ON_FALSE(len > 0, ESP_ERR_INVALID_ARG, TAG, "len <= 0");
    ESP_RETURN_ON_FALSE(handle->connected, ESP_FAIL, TAG, "WebSocket未连接");
    ESP_RETURN_ON_FALSE(handle->audio_uplink != NULL, ESP_FAIL, TAG, "音频上行模块未初始化");
    
    // 直接写入环形缓冲区（零拷贝）
    return audio_uplink_write(handle->audio_uplink, (const uint8_t *)audio_data, len);
}

/**
 * @brief 发送音频完成信号
 * 
 * 通知Coze服务器用户已停止说话，可以开始处理音频。
 * 注意：在VAD模式下，服务器会自动检测静音，无需手动发送此信号。
 * 
 * @param handle Coze Chat句柄
 * @return ESP_OK成功，其他值表示失败
 */
extern "C" esp_err_t coze_chat_send_audio_complete(coze_chat_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(handle->connected, ESP_FAIL, TAG, "WebSocket未连接");
    
    // VAD模式下无需手动发送完成信号
    if (handle->config.turn_detection_type == COZE_TURN_DETECTION_SERVER_VAD) {
        ESP_LOGI(TAG, "VAD模式，跳过手动完成信号");
        return ESP_OK;
    }
    
    // 构建JSON消息（按官方文档格式）
    cJSON *root = cJSON_CreateObject();
    
    // 生成唯一事件ID
    char event_id[64];
    snprintf(event_id, sizeof(event_id), "complete_%lld", esp_timer_get_time() / 1000);
    
    cJSON_AddStringToObject(root, "id", event_id);
    cJSON_AddStringToObject(root, "event_type", "input_audio_buffer.complete");
    
    char *json_str = cJSON_PrintUnformatted(root);
    bool success = handle->websocket->Send(json_str);
    
    ESP_LOGI(TAG, "📤 已发送音频完成信号");
    
    free(json_str);
    cJSON_Delete(root);
    
    return success ? ESP_OK : ESP_FAIL;
}

/**
 * @brief 发送音频取消信号
 * 
 * 通知Coze服务器取消当前音频输入，清空音频缓冲区。
 * 
 * @param handle Coze Chat句柄
 * @return ESP_OK成功，其他值表示失败
 */
extern "C" esp_err_t coze_chat_send_audio_cancel(coze_chat_handle_t handle)
{
    ESP_RETURN_ON_FALSE(handle != NULL, ESP_ERR_INVALID_ARG, TAG, "handle is NULL");
    ESP_RETURN_ON_FALSE(handle->connected, ESP_FAIL, TAG, "WebSocket未连接");
    
    // 构建JSON消息（按官方文档格式）
    cJSON *root = cJSON_CreateObject();
    
    // 生成唯一事件ID
    char event_id[64];
    snprintf(event_id, sizeof(event_id), "clear_%lld", esp_timer_get_time() / 1000);
    
    cJSON_AddStringToObject(root, "id", event_id);
    cJSON_AddStringToObject(root, "event_type", "input_audio_buffer.clear");
    
    char *json_str = cJSON_PrintUnformatted(root);
    bool success = handle->websocket->Send(json_str);
    
    ESP_LOGI(TAG, "📤 已发送音频取消信号");
    
    free(json_str);
    cJSON_Delete(root);
    
    return success ? ESP_OK : ESP_FAIL;
}
