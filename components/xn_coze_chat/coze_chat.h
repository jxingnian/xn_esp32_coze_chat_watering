/*
 * @file coze_chat.h
 * @author xingnian j_xingnian@163.com
 * @date 2025-10-24
 * @brief Coze Chat组件 - 支持WiFi和4G的Coze实时语音通信
 * 
 * @details 本组件提供了与Coze平台进行实时语音通信的完整功能，支持以下特性：
 *          - 双网络模式：WiFi（基于lwIP和esp_websocket_client）和4G（基于ML307模组）
 *          - 多种音频格式：PCM和Opus编码
 *          - 智能语音检测：VAD（语音活动检测）和语义VAD
 *          - 丰富的ASR配置：热词、语言识别、情绪识别等
 *          - 灵活的TTS配置：语速、情感、音量调节
 *          - 声纹识别和降噪功能
 *          - 完整的回调机制：音频数据、事件、WebSocket事件
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 网络模式枚举
 * 
 * @details 定义组件支持的两种网络连接方式
 */
typedef enum {
    COZE_NETWORK_WIFI = 0,        ///< WiFi模式：使用lwIP协议栈和esp_websocket_client库进行WebSocket通信
    COZE_NETWORK_4G = 1,          ///< 4G模式：使用ML307模组通过UART进行AT指令通信
} coze_network_mode_t;

/**
 * @brief Coze聊天模式枚举
 * 
 * @details 定义两种不同的语音交互模式
 */
typedef enum {
    COZE_CHAT_NORMAL_MODE = 0,    ///< 按键模式：需要用户手动调用coze_chat_send_audio_complete()发送音频完成信号
    COZE_CHAT_VAD_MODE = 1,       ///< VAD模式：自动检测语音结束，无需手动发送完成信号
} coze_chat_mode_t;

/**
 * @brief VAD转检测类型枚举
 * 
 * @details 定义语音转检测的不同策略
 */
typedef enum {
    COZE_TURN_DETECTION_SERVER_VAD = 0,      ///< 服务器端VAD：由Coze服务器进行语音活动检测
    COZE_TURN_DETECTION_CLIENT_INTERRUPT,    ///< 客户端打断：通过关键词匹配实现打断
    COZE_TURN_DETECTION_SEMANTIC_VAD,        ///< 语义VAD：企业版功能，基于语义理解进行转检测
} coze_turn_detection_type_t;

/**
 * @brief 用户语言枚举（官方标准格式）
 * 
 * @details 支持Coze平台官方定义的所有语言类型
 */
typedef enum {
    COZE_USER_LANG_COMMON = 0,  ///< common - 大模型语音识别，可自动识别中英粤等多种语言
    COZE_USER_LANG_EN_US,       ///< en-US - 英语（美式）
    COZE_USER_LANG_JA_JP,       ///< ja-JP - 日语
    COZE_USER_LANG_ID_ID,       ///< id-ID - 印尼语
    COZE_USER_LANG_ES_MX,       ///< es-MX - 西班牙语（墨西哥）
    COZE_USER_LANG_PT_BR,       ///< pt-BR - 葡萄牙语（巴西）
    COZE_USER_LANG_DE_DE,       ///< de-DE - 德语
    COZE_USER_LANG_FR_FR,       ///< fr-FR - 法语
    COZE_USER_LANG_KO_KR,       ///< ko-KR - 韩语
    COZE_USER_LANG_FIL_PH,      ///< fil-PH - 菲律宾语
    COZE_USER_LANG_MS_MY,       ///< ms-MY - 马来语
    COZE_USER_LANG_TH_TH,       ///< th-TH - 泰语
    COZE_USER_LANG_AR_SA,       ///< ar-SA - 阿拉伯语（沙特）
} coze_user_language_t;

/**
 * @brief 打断模式类型枚举
 * 
 * @details 定义关键词匹配的两种模式
 */
typedef enum {
    COZE_INTERRUPT_MODE_CONTAINS = 0,  ///< keyword_contains - 包含关键词即打断：只要识别结果包含关键词就触发打断
    COZE_INTERRUPT_MODE_PREFIX,        ///< keyword_prefix - 前缀匹配才打断：只有识别结果以关键词开头才触发打断
} coze_interrupt_mode_t;

/**
 * @brief 情感类型枚举
 * 
 * @details 定义TTS语音合成支持的情感类型
 */
typedef enum {
    COZE_EMOTION_HAPPY = 0,     ///< 开心：表达愉悦、快乐的情绪
    COZE_EMOTION_SAD,           ///< 悲伤：表达难过、沮丧的情绪
    COZE_EMOTION_ANGRY,         ///< 生气：表达愤怒、不满的情绪
    COZE_EMOTION_SURPRISED,     ///< 惊讶：表达意外、震惊的情绪
    COZE_EMOTION_FEAR,          ///< 恐惧：表达害怕、担忧的情绪
    COZE_EMOTION_HATE,          ///< 厌恶：表达反感、讨厌的情绪
    COZE_EMOTION_EXCITED,       ///< 激动：表达兴奋、激动的情绪
    COZE_EMOTION_COLDNESS,      ///< 冷漠：表达冷淡、漠不关心的情绪
    COZE_EMOTION_NEUTRAL        ///< 中性：无特殊情感，正常语调
} coze_emotion_type_t;

/**
 * @brief Coze音频类型枚举
 * 
 * @details 定义上行和下行音频的编码格式
 */
typedef enum {
    COZE_CHAT_AUDIO_TYPE_PCM = 0,     ///< PCM格式：未压缩音频，支持16kHz采样率，16bit位深，单声道
    COZE_CHAT_AUDIO_TYPE_OPUS = 1,    ///< Opus格式：压缩音频，需要配置比特率和帧长
} coze_chat_audio_type_t;

/**
 * @brief Coze聊天事件类型枚举
 * 
 * @details 定义组件生命周期和交互过程中的所有事件类型
 */
typedef enum {
    COZE_CHAT_EVENT_CHAT_CREATE = 0,                  ///< 会话创建：WebSocket连接成功，会话已创建
    COZE_CHAT_EVENT_CHAT_UPDATE,                      ///< 会话更新：会话状态发生变化
    COZE_CHAT_EVENT_CHAT_COMPLETED,                   ///< 会话完成：会话正常结束
    COZE_CHAT_EVENT_CHAT_SPEECH_STARTED,              ///< 开始说话：TTS开始播放语音
    COZE_CHAT_EVENT_CHAT_SPEECH_STOPED,               ///< 停止说话：TTS停止播放语音
    COZE_CHAT_EVENT_CHAT_ERROR,                       ///< 错误：发生错误，data字段包含错误信息
    COZE_CHAT_EVENT_INPUT_AUDIO_BUFFER_COMPLETED,     ///< 音频缓冲区处理完成：上行音频缓冲区处理完成
    COZE_CHAT_EVENT_CHAT_SUBTITLE_EVENT,              ///< 字幕事件：收到字幕数据，data字段包含字幕内容
    COZE_CHAT_EVENT_CHAT_CUSTOMER_DATA,               ///< 自定义数据：收到自定义数据，data字段包含数据内容
} coze_chat_event_t;

/**
 * @brief WebSocket事件ID枚举
 * 
 * @details 定义WebSocket连接状态事件
 */
typedef enum {
    COZE_WS_EVENT_CONNECTED = 0,      ///< 已连接：WebSocket连接成功
    COZE_WS_EVENT_DISCONNECTED,       ///< 已断开：WebSocket连接断开
    COZE_WS_EVENT_DATA,               ///< 数据接收：收到WebSocket数据
    COZE_WS_EVENT_ERROR,              ///< 错误：WebSocket发生错误
} coze_ws_event_id_t;

/**
 * @brief WebSocket事件结构体
 * 
 * @details 封装WebSocket事件信息
 */
typedef struct {
    void *handle;                     ///< WebSocket句柄
    coze_ws_event_id_t event_id;      ///< 事件ID
} coze_ws_event_t;

/**
 * @brief Coze聊天句柄类型
 * 
 * @details 指向内部coze_chat_t结构体的不透明指针
 */
typedef struct coze_chat_t *coze_chat_handle_t;

/**
 * @brief 音频数据回调函数类型
 * 
 * @details 当收到下行音频数据时调用此回调
 * 
 * @param data 音频数据（Opus格式）
 * @param len 数据长度（字节）
 * @param ctx 用户上下文指针
 */
typedef void (*coze_audio_callback_t)(char *data, int len, void *ctx);

/**
 * @brief 事件回调函数类型
 * 
 * @details 当发生聊天事件时调用此回调
 * 
 * @param event 事件类型
 * @param data 事件数据（可能为NULL），JSON格式字符串
 * @param ctx 用户上下文指针
 */
typedef void (*coze_event_callback_t)(coze_chat_event_t event, char *data, void *ctx);

/**
 * @brief WebSocket事件回调函数类型
 * 
 * @details 当发生WebSocket事件时调用此回调
 * 
 * @param event WebSocket事件指针
 */
typedef void (*coze_ws_event_callback_t)(coze_ws_event_t *event);

/**
 * @brief Coze聊天配置结构体
 * 
 * @details 包含所有可配置的参数，用于初始化Coze聊天组件
 */
typedef struct {
    // ========== 网络模式 ==========
    coze_network_mode_t network_mode;  ///< 网络模式：选择WiFi或4G连接方式

    // ========== ML307 UART配置（仅4G模式需要）==========
    int at_uart_num;                ///< UART端口号：通常使用UART1（值为1）
    int at_tx_pin;                  ///< TX引脚：ML307模组的发送引脚
    int at_rx_pin;                  ///< RX引脚：ML307模组的接收引脚
    int at_pwr_pin;                 ///< 电源使能引脚：ML307模组的电源控制引脚，通常为GPIO12
    int at_baud_rate;               ///< 波特率：UART通信速率，默认115200

    // ========== Coze基本配置 ==========
    const char *bot_id;             ///< Bot ID：Coze平台分配的机器人ID
    const char *access_token;       ///< 访问Token：用于身份验证的访问令牌
    const char *user_id;            ///< 用户ID：标识当前用户
    const char *voice_id;           ///< 语音ID：指定TTS使用的语音类型
    const char *conversation_id;    ///< 会话ID：可选，用于恢复历史会话

    // ========== 音频格式 ==========
    coze_chat_audio_type_t uplink_audio_type;    ///< 上行音频格式：发送到服务器的音频编码格式（PCM）
    coze_chat_audio_type_t downlink_audio_type;  ///< 下行音频格式：从服务器接收的音频编码格式（Opus）
    int input_sample_rate;          ///< 输入采样率：麦克风采集的音频采样率，支持16000Hz
    int input_channel;              ///< 输入声道数：音频声道数，默认1（单声道）
    int input_bit_depth;            ///< 输入位深：音频位深，默认16bit
    int output_sample_rate;         ///< 输出采样率：扬声器播放的音频采样率，默认16000Hz

    // ========== Opus高级配置 ==========
    int opus_bitrate;               ///< Opus比特率：音频压缩比特率，默认16000bps
    float opus_frame_size_ms;       ///< Opus帧长：每帧音频的时长，默认60ms
    bool opus_use_cbr;              ///< Opus是否使用CBR：固定比特率模式，默认false（使用VBR）

    // ========== PCM高级配置 ==========
    float pcm_frame_size_ms;        ///< PCM帧长：每帧音频的时长，默认20ms

    // ========== TTS配置 ==========
    int speech_rate;                ///< 语速：-50~50，0为正常速度，负值变慢，正值变快
    coze_emotion_type_t emotion_type;     ///< 情感类型：TTS语音的情感表达，默认中性
    float emotion_scale;            ///< 情感值：1.0~5.0，默认4.0，值越高情感越强烈

    // ========== 工作模式 ==========
    coze_chat_mode_t mode;          ///< 聊天模式：按键模式或VAD模式
    bool enable_subtitle;           ///< 是否启用字幕：是否接收并显示字幕
    bool auto_save_history;         ///< 是否自动保存对话历史：是否保存对话记录
    bool need_play_prologue;        ///< 是否播放开场白：是否在会话开始时播放开场白
    const char *prologue_content;   ///< 自定义开场白内容：如果need_play_prologue为true，使用此内容

    // ========== VAD配置 ==========
    coze_turn_detection_type_t turn_detection_type;  ///< 转检测类型：选择VAD检测策略
    int vad_silence_duration_ms;    ///< VAD静音持续时间：检测到静音后持续多长时间才认为语音结束，默认500ms
    int vad_prefix_padding_ms;      ///< VAD前填充时间：在检测到语音开始前保留的音频时长，默认300ms

    // ========== 打断配置 ==========
    coze_interrupt_mode_t interrupt_mode;  ///< 打断模式：关键词匹配模式（contains或prefix）
    const char **interrupt_keywords; ///< 打断关键词数组：用于触发打断的关键词列表
    int interrupt_keyword_count;    ///< 打断关键词数量：关键词数组的长度

    // ========== 语义VAD配置 ==========
    int semantic_vad_silence_threshold_ms;    ///< 语义VAD静音阈值：语义VAD模式下，静音多长时间触发转检测，默认300ms
    int semantic_vad_unfinished_wait_time_ms; ///< 语义未完成等待时间：语义未完成时等待的时间，默认500ms

    // ========== ASR配置 ==========
    const char **asr_hot_words;     ///< ASR热词列表：提高特定词汇识别准确率的词表
    int asr_hot_word_count;         ///< ASR热词数量：热词列表的长度
    const char *asr_context;        ///< ASR上下文信息：提供上下文信息以提高识别准确率
    coze_user_language_t asr_language;  ///< ASR识别语言：指定语音识别的语言类型
    bool asr_enable_ddc;            ///< 是否启用语义顺滑：自动修正识别错误
    bool asr_enable_itn;            ///< 是否开启文本规范化：将数字、日期等转换为标准格式
    bool asr_enable_punc;           ///< 是否添加标点符号：自动添加标点符号
    bool asr_enable_nostream;       ///< 是否开启二次识别模式：对识别结果进行二次确认
    bool asr_enable_emotion;        ///< 是否识别情绪：识别说话人的情绪
    bool asr_enable_gender;         ///< 是否识别性别：识别说话人的性别
    const char *asr_stream_mode;    ///< ASR模式："output_no_stream"或"bidirectional_stream"

    // ========== ASR敏感词过滤 ==========
    bool asr_system_reserved_filter;        ///< 是否过滤系统敏感词：是否启用系统内置敏感词过滤
    const char **asr_filter_with_empty;     ///< 替换为空的敏感词列表：这些词将被替换为空字符串
    int asr_filter_with_empty_count;        ///< 替换为空的敏感词数量：列表长度
    const char **asr_filter_with_signed;    ///< 替换为*的敏感词列表：这些词将被替换为星号
    int asr_filter_with_signed_count;       ///< 替换为*的敏感词数量：列表长度

    // ========== 元数据和自定义参数 ==========
    const char *meta_data_json;     ///< 附加元数据：JSON格式的元数据字符串
    const char *custom_variables_json; ///< 自定义变量：JSON格式的自定义变量字符串
    const char *extra_params_json;  ///< 额外参数：JSON格式的额外参数字符串
    const char *parameters_json;    ///< 对话流自定义参数：JSON格式的对话流参数字符串

    // ========== TTS音量配置 ==========
    int loudness_rate;              ///< 音量：-50~100，0为正常音量，负值降低音量，正值提高音量

    // ========== 语音处理配置 ==========
    bool voice_processing_enable_ans;  ///< 是否启用主动噪声抑制：自动消除背景噪声
    bool voice_processing_enable_pdns; ///< 是否启用声纹降噪：基于声纹特征进行降噪
    const char *voice_print_feature_id; ///< 声纹特征ID：用于降噪的声纹特征标识

    // ========== 声纹识别配置 ==========
    const char *voice_print_group_id;  ///< 声纹组ID：声纹识别组标识
    int voice_print_score;             ///< 声纹匹配阈值：0~100，默认40，值越高匹配要求越严格
    bool voice_print_reuse_info;       ///< 未命中时是否沿用历史声纹：是否在未匹配时使用历史声纹信息

    // ========== 回调函数 ==========
    coze_audio_callback_t audio_callback;        ///< 音频数据回调：接收下行音频数据时调用
    coze_event_callback_t event_callback;         ///< 事件回调：发生聊天事件时调用
    coze_ws_event_callback_t ws_event_callback;   ///< WebSocket事件回调：发生WebSocket事件时调用

    // ========== 任务栈配置 ==========
    int pull_task_stack_size;       ///< WebSocket接收任务栈大小：默认16384字节
    int push_task_stack_size;       ///< WebSocket发送任务栈大小：默认8192字节
    int pull_task_caps;             ///< 接收任务内存分配属性：0表示使用默认属性
    int push_task_caps;             ///< 发送任务内存分配属性：0表示使用默认属性

    // ========== 缓冲区配置 ==========
    int websocket_buffer_size;      ///< WebSocket缓冲区大小：默认8192字节
    int ring_buffer_size;           ///< 环形缓冲区大小：默认2MB，用于音频数据缓冲
} coze_chat_config_t;

/**
 * @brief Coze聊天默认配置（WiFi模式）
 * 
 * @details 提供WiFi模式下的默认配置，用户可以直接使用或在此基础上修改
 * 
 * @note WiFi模式使用esp_websocket_client库，无需配置UART参数
 *       所有UART相关参数（at_uart_num、at_tx_pin等）在WiFi模式下被忽略
 */
#define COZE_CHAT_DEFAULT_CONFIG_WIFI() {               \
        /* ========== 网络模式配置 ========== */            \
        .network_mode = COZE_NETWORK_WIFI,                  \
        /* ========== UART配置（WiFi模式下忽略） ========== */ \
        .at_uart_num = 0,                                   \
        .at_tx_pin = 0,                                     \
        .at_rx_pin = 0,                                     \
        .at_pwr_pin = 0,                                    \
        .at_baud_rate = 0,                                  \
        /* ========== 认证配置（必填） ========== */         \
        .bot_id = NULL,                                     \
        .access_token = NULL,                               \
        .user_id = NULL,                                    \
        .voice_id = NULL,                                   \
        .conversation_id = NULL,                            \
        /* ========== 音频格式配置 ========== */            \
        .uplink_audio_type = COZE_CHAT_AUDIO_TYPE_PCM,      \
        .downlink_audio_type = COZE_CHAT_AUDIO_TYPE_OPUS,   \
        /* ========== 输入音频参数 ========== */            \
        .input_sample_rate = 16000,                         \
        .input_channel = 1,                                 \
        .input_bit_depth = 16,                              \
        /* ========== 输出音频参数 ========== */            \
        .output_sample_rate = 16000,                        \
        /* ========== Opus编码配置 ========== */            \
        .opus_bitrate = 16000,                              \
        .opus_frame_size_ms = 60.0f,                        \
        .opus_use_cbr = false,                              \
        /* ========== PCM帧配置 ========== */               \
        .pcm_frame_size_ms = 20.0f,                         \
        /* ========== TTS语音配置 ========== */             \
        .speech_rate = 0,                                   \
        .emotion_type = COZE_EMOTION_NEUTRAL,               \
        .emotion_scale = 4.0f,                              \
        /* ========== 交互模式配置 ========== */            \
        .mode = COZE_CHAT_VAD_MODE,                         \
        /* ========== 字幕和历史配置 ========== */          \
        .enable_subtitle = true,                            \
        .auto_save_history = true,                          \
        /* ========== 开场白配置 ========== */              \
        .need_play_prologue = false,                        \
        .prologue_content = NULL,                           \
        /* ========== 语音检测配置 ========== */            \
        .turn_detection_type = COZE_TURN_DETECTION_SERVER_VAD, \
        .vad_silence_duration_ms = 500,                     \
        .vad_prefix_padding_ms = 300,                       \
        /* ========== 打断配置 ========== */                \
        .interrupt_mode = COZE_INTERRUPT_MODE_CONTAINS,     \
        .interrupt_keywords = NULL,                         \
        .interrupt_keyword_count = 0,                       \
        /* ========== 语义VAD配置 ========== */             \
        .semantic_vad_silence_threshold_ms = 300,           \
        .semantic_vad_unfinished_wait_time_ms = 500,        \
        /* ========== ASR热词配置 ========== */             \
        .asr_hot_words = NULL,                              \
        .asr_hot_word_count = 0,                            \
        /* ========== ASR上下文配置 ========== */          \
        .asr_context = NULL,                                \
        /* ========== ASR语言配置 ========== */             \
        .asr_language = COZE_USER_LANG_COMMON,              \
        /* ========== ASR功能开关 ========== */             \
        .asr_enable_ddc = true,                             \
        .asr_enable_itn = true,                             \
        .asr_enable_punc = true,                            \
        .asr_enable_nostream = false,                       \
        .asr_enable_emotion = false,                        \
        .asr_enable_gender = false,                         \
        /* ========== ASR流模式配置 ========== */          \
        .asr_stream_mode = "bidirectional_stream",          \
        /* ========== ASR敏感词过滤 ========== */           \
        .asr_system_reserved_filter = false,                \
        .asr_filter_with_empty = NULL,                      \
        .asr_filter_with_empty_count = 0,                   \
        .asr_filter_with_signed = NULL,                     \
        .asr_filter_with_signed_count = 0,                  \
        /* ========== 元数据和自定义参数 ========== */     \
        .meta_data_json = NULL,                             \
        .custom_variables_json = NULL,                      \
        .extra_params_json = NULL,                          \
        .parameters_json = NULL,                            \
        /* ========== TTS音量配置 ========== */             \
        .loudness_rate = 0,                                 \
        /* ========== 语音处理配置 ========== */            \
        .voice_processing_enable_ans = false,               \
        .voice_processing_enable_pdns = false,              \
        .voice_print_feature_id = NULL,                     \
        /* ========== 声纹识别配置 ========== */            \
        .voice_print_group_id = NULL,                       \
        .voice_print_score = 40,                            \
        .voice_print_reuse_info = false,                    \
        /* ========== 回调函数 ========== */                \
        .audio_callback = NULL,                             \
        .event_callback = NULL,                             \
        .ws_event_callback = NULL,                          \
        /* ========== 任务栈配置 ========== */              \
        .pull_task_stack_size = 16384,                      \
        .push_task_stack_size = 8192,                       \
        .pull_task_caps = 0,                                \
        .push_task_caps = 0,                                \
        /* ========== 缓冲区配置 ========== */              \
        .websocket_buffer_size = 8192,                      \
        .ring_buffer_size = 2 * 1024 * 1024,                \
    }

/**
 * @brief Coze聊天默认配置（4G模式）
 * 
 * @details 提供4G模式下的默认配置，包含ML307模组的UART配置
 * 
 * @note 4G模式使用ML307模组，需要配置UART参数
 *       UART配置：UART1，TX=GPIO13，RX=GPIO14，PWR=GPIO12，波特率115200
 *       这些参数需要根据实际硬件连接进行调整
 */
#define COZE_CHAT_DEFAULT_CONFIG_4G() {                 \
        /* ========== 网络模式配置 ========== */            \
        .network_mode = COZE_NETWORK_4G,                    \
        /* ========== UART配置（ML307模组） ========== */   \
        .at_uart_num = 1,                                   \
        .at_tx_pin = 13,                                    \
        .at_rx_pin = 14,                                    \
        .at_pwr_pin = 12,                                   \
        .at_baud_rate = 115200,                             \
        /* ========== 认证配置（必填） ========== */         \
        .bot_id = NULL,                                     \
        .access_token = NULL,                               \
        .user_id = NULL,                                    \
        .voice_id = NULL,                                   \
        .conversation_id = NULL,                            \
        /* ========== 音频格式配置 ========== */            \
        .uplink_audio_type = COZE_CHAT_AUDIO_TYPE_PCM,      \
        .downlink_audio_type = COZE_CHAT_AUDIO_TYPE_OPUS,   \
        /* ========== 输入音频参数 ========== */            \
        .input_sample_rate = 16000,                         \
        .input_channel = 1,                                 \
        .input_bit_depth = 16,                              \
        /* ========== 输出音频参数 ========== */            \
        .output_sample_rate = 16000,                        \
        /* ========== Opus编码配置 ========== */            \
        .opus_bitrate = 16000,                              \
        .opus_frame_size_ms = 60.0f,                        \
        .opus_use_cbr = false,                              \
        /* ========== PCM帧配置 ========== */               \
        .pcm_frame_size_ms = 20.0f,                         \
        /* ========== TTS语音配置 ========== */             \
        .speech_rate = 0,                                   \
        .emotion_type = COZE_EMOTION_NEUTRAL,               \
        .emotion_scale = 4.0f,                              \
        /* ========== 交互模式配置 ========== */            \
        .mode = COZE_CHAT_VAD_MODE,                         \
        /* ========== 字幕和历史配置 ========== */          \
        .enable_subtitle = true,                            \
        .auto_save_history = true,                          \
        /* ========== 开场白配置 ========== */              \
        .need_play_prologue = false,                        \
        .prologue_content = NULL,                           \
        /* ========== 语音检测配置 ========== */            \
        .turn_detection_type = COZE_TURN_DETECTION_SERVER_VAD, \
        .vad_silence_duration_ms = 500,                     \
        .vad_prefix_padding_ms = 300,                       \
        /* ========== 打断配置 ========== */                \
        .interrupt_mode = COZE_INTERRUPT_MODE_CONTAINS,     \
        .interrupt_keywords = NULL,                         \
        .interrupt_keyword_count = 0,                       \
        /* ========== 语义VAD配置 ========== */             \
        .semantic_vad_silence_threshold_ms = 300,           \
        .semantic_vad_unfinished_wait_time_ms = 500,        \
        /* ========== ASR热词配置 ========== */             \
        .asr_hot_words = NULL,                              \
        .asr_hot_word_count = 0,                            \
        /* ========== ASR上下文配置 ========== */          \
        .asr_context = NULL,                                \
        /* ========== ASR语言配置 ========== */             \
        .asr_language = COZE_USER_LANG_COMMON,              \
        /* ========== ASR功能开关 ========== */             \
        .asr_enable_ddc = true,                             \
        .asr_enable_itn = true,                             \
        .asr_enable_punc = true,                            \
        .asr_enable_nostream = false,                       \
        .asr_enable_emotion = false,                        \
        .asr_enable_gender = false,                         \
        /* ========== ASR流模式配置 ========== */          \
        .asr_stream_mode = "bidirectional_stream",          \
        /* ========== ASR敏感词过滤 ========== */           \
        .asr_system_reserved_filter = false,                \
        .asr_filter_with_empty = NULL,                      \
        .asr_filter_with_empty_count = 0,                   \
        .asr_filter_with_signed = NULL,                     \
        .asr_filter_with_signed_count = 0,                  \
        /* ========== 元数据和自定义参数 ========== */     \
        .meta_data_json = NULL,                             \
        .custom_variables_json = NULL,                      \
        .extra_params_json = NULL,                          \
        .parameters_json = NULL,                            \
        /* ========== TTS音量配置 ========== */             \
        .loudness_rate = 0,                                 \
        /* ========== 语音处理配置 ========== */            \
        .voice_processing_enable_ans = false,               \
        .voice_processing_enable_pdns = false,              \
        .voice_print_feature_id = NULL,                     \
        /* ========== 声纹识别配置 ========== */            \
        .voice_print_group_id = NULL,                       \
        .voice_print_score = 40,                            \
        .voice_print_reuse_info = false,                    \
        /* ========== 回调函数 ========== */                \
        .audio_callback = NULL,                             \
        .event_callback = NULL,                             \
        .ws_event_callback = NULL,                          \
        /* ========== 任务栈配置 ========== */              \
        .pull_task_stack_size = 16384,                      \
        .push_task_stack_size = 8192,                       \
        .pull_task_caps = 0,                                \
        .push_task_caps = 0,                                \
        /* ========== 缓冲区配置 ========== */              \
        .websocket_buffer_size = 8192,                      \
        .ring_buffer_size = 2 * 1024 * 1024,                \
    }

// 默认配置（WiFi模式）
#define COZE_CHAT_DEFAULT_CONFIG COZE_CHAT_DEFAULT_CONFIG_WIFI

/**
 * @brief 初始化Coze聊天组件
 *
 * @details 根据配置参数初始化Coze聊天组件，分配必要的资源
 *
 * @param config 配置参数指针，包含所有初始化参数
 * @param handle 返回的句柄指针，用于后续操作
 * @return esp_err_t
 *         - ESP_OK: 初始化成功
 *         - ESP_ERR_INVALID_ARG: 参数无效（如必填参数为空）
 *         - ESP_ERR_NO_MEM: 内存不足
 *         - ESP_FAIL: 其他错误（如网络初始化失败）
 */
esp_err_t coze_chat_init(const coze_chat_config_t *config, coze_chat_handle_t *handle);

/**
 * @brief 启动Coze聊天（连接WebSocket）
 *
 * @details 建立与Coze服务器的WebSocket连接，开始语音通信
 *
 * @param handle Coze聊天句柄
 * @return esp_err_t
 *         - ESP_OK: 连接成功
 *         - ESP_ERR_INVALID_ARG: 参数无效（handle为NULL）
 *         - ESP_FAIL: 连接失败（网络问题或服务器拒绝）
 */
esp_err_t coze_chat_start(coze_chat_handle_t handle);

/**
 * @brief 停止Coze聊天（断开WebSocket）
 *
 * @details 断开与Coze服务器的WebSocket连接，停止语音通信
 *
 * @param handle Coze聊天句柄
 * @return esp_err_t
 *         - ESP_OK: 断开成功
 *         - ESP_ERR_INVALID_ARG: 参数无效（handle为NULL）
 */
esp_err_t coze_chat_stop(coze_chat_handle_t handle);

/**
 * @brief 反初始化Coze聊天（释放资源）
 *
 * @details 释放Coze聊天组件占用的所有资源，包括内存、任务等
 *
 * @param handle Coze聊天句柄
 * @return esp_err_t
 *         - ESP_OK: 反初始化成功
 *         - ESP_ERR_INVALID_ARG: 参数无效（handle为NULL）
 */
esp_err_t coze_chat_deinit(coze_chat_handle_t handle);

/**
 * @brief 发送音频数据到Coze
 *
 * @details 将麦克风采集的音频数据发送到Coze服务器进行语音识别
 *
 * @param handle Coze聊天句柄
 * @param data 音频数据指针（PCM格式：16bit, mono, 16kHz）
 * @param len 数据长度（字节）
 * @return esp_err_t
 *         - ESP_OK: 发送成功
 *         - ESP_ERR_INVALID_ARG: 参数无效（handle为NULL或data为NULL）
 *         - ESP_FAIL: 发送失败（网络问题或缓冲区满）
 */
esp_err_t coze_chat_send_audio_data(coze_chat_handle_t handle, char *data, int len);

/**
 * @brief 发送音频完成信号（NORMAL_MODE必须）
 *
 * @details 在NORMAL_MODE模式下，用户必须调用此函数通知服务器音频发送完成
 *
 * @param handle Coze聊天句柄
 * @return esp_err_t
 *         - ESP_OK: 发送成功
 *         - ESP_ERR_INVALID_ARG: 参数无效（handle为NULL）
 */
esp_err_t coze_chat_send_audio_complete(coze_chat_handle_t handle);

/**
 * @brief 发送音频取消信号（打断）
 *
 * @details 取消当前正在发送的音频，用于实现打断功能
 *
 * @param handle Coze聊天句柄
 * @return esp_err_t
 *         - ESP_OK: 发送成功
 *         - ESP_ERR_INVALID_ARG: 参数无效（handle为NULL）
 */
esp_err_t coze_chat_send_audio_cancel(coze_chat_handle_t handle);

/**
 * @brief 获取ML307 modem句柄（用于OTA等其他功能）
 *
 * @details 返回内部的AtModem指针，仅在4G模式下有效
 *
 * @param handle Coze聊天句柄
 * @return void* modem句柄，WiFi模式或失败返回NULL
 * 
 * @note 返回的是C++对象指针(AtModem*)，使用时需要注意类型转换
 */
void *coze_chat_get_modem(coze_chat_handle_t handle);

#ifdef __cplusplus
}
#endif
