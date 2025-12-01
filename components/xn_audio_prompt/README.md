# Audio Prompt 音效播放模块

基于预加载缓存的PCM音效播放系统，专为ESP32-S3设计。

## 📋 功能特点

- ✅ **预加载缓存**：启动时加载到PSRAM，播放无延迟
- ✅ **内存优化**：使用PSRAM，不占用宝贵的IRAM
- ✅ **格式固定**：16kHz, 16bit, 单声道PCM（与系统一致）
- ✅ **易于扩展**：支持自定义音效文件
- ✅ **容错设计**：个别音效加载失败不影响系统

## 🚀 快速开始

### 1. 生成音效文件

```bash
cd tools
python generate_audio_prompts.py
```

这会在 `spiffs_data/` 目录生成以下音效：
- `beep.pcm` - 短促蜂鸣（100ms）
- `success.pcm` - 成功提示音（双音）
- `error.pcm` - 错误提示音（低频）
- `wakeup.pcm` - 唤醒提示音（上升音调）
- `thinking.pcm` - 思考提示音（双音节）

### 2. 在主程序中初始化

```c
#include "audio_prompt.h"

void app_main()
{
    // ... 其他初始化 ...
    
    // 初始化音频管理器
    audio_manager_init(config);
    
    // 初始化音效模块（会预加载所有音效）
    audio_prompt_init();
    
    // ... 其他代码 ...
}
```

### 3. 播放音效

```c
// 播放预定义音效（从缓存，无延迟）
audio_prompt_play(AUDIO_PROMPT_WAKEUP);

// 播放自定义PCM文件（每次加载）
audio_prompt_play_file("/spiffs/custom.pcm");

// 停止播放
audio_prompt_stop();
```

## 📊 音效类型

| 类型 | 枚举值 | 文件名 | 用途 |
|------|--------|--------|------|
| 蜂鸣 | `AUDIO_PROMPT_BEEP` | beep.pcm | 通用提示 |
| 成功 | `AUDIO_PROMPT_SUCCESS` | success.pcm | 操作成功 |
| 错误 | `AUDIO_PROMPT_ERROR` | error.pcm | 操作失败 |
| 唤醒 | `AUDIO_PROMPT_WAKEUP` | wakeup.pcm | 唤醒确认 |
| 思考 | `AUDIO_PROMPT_THINKING` | thinking.pcm | 处理中 |

## 🎵 自定义音效

### 方法1：使用生成工具（推荐）

修改 `tools/generate_audio_prompts.py`，添加新的生成函数。

### 方法2：转换WAV文件

使用 ffmpeg 或 SoX 转换：

```bash
# 使用 ffmpeg
ffmpeg -i input.wav -f s16le -acodec pcm_s16le -ac 1 -ar 16000 output.pcm

# 使用 SoX
sox input.wav -r 16000 -c 1 -b 16 -e signed-integer output.pcm
```

### 方法3：在线生成

使用在线工具（如 https://www.audiocheck.net/audiofrequencysignalgenerator_sinetone.php）
生成WAV，然后转换为PCM。

## 🔧 扩展音效

编辑 `audio_prompt.h` 和 `audio_prompt.c`：

```c
// 1. 在 audio_prompt.h 中添加枚举
typedef enum {
    // ... 现有的 ...
    AUDIO_PROMPT_GOODBYE,   // 新增：再见
    AUDIO_PROMPT_MAX
} audio_prompt_type_t;

// 2. 在 audio_prompt.c 中添加文件映射
static prompt_info_t s_prompts[AUDIO_PROMPT_MAX] = {
    // ... 现有的 ...
    {"/spiffs/goodbye.pcm", NULL, 0, false},
};

// 3. 准备对应的PCM文件并放入 spiffs_data/
```

## 📈 性能指标

| 指标 | 值 |
|------|-----|
| **预加载时间** | ~50-100ms (5个音效) |
| **播放延迟** | <1ms (已缓存) |
| **内存占用** | ~100KB (PSRAM) |
| **音效时长** | 80-300ms/个 |

## ⚠️ 注意事项

1. **必须先初始化 audio_manager**，再初始化 audio_prompt
2. **音效文件必须存在于SPIFFS**，否则跳过加载
3. **播放前会自动启动播放任务**，无需手动调用
4. **音效格式严格**：16kHz, 16bit, 单声道, RAW PCM（无头）
5. **内存分配在PSRAM**，不影响IRAM性能

## 📝 API示例

```c
// 检查音效是否已加载
if (audio_prompt_is_loaded(AUDIO_PROMPT_WAKEUP)) {
    audio_prompt_play(AUDIO_PROMPT_WAKEUP);
}

// 获取音效信息
size_t samples;
uint32_t duration_ms;
audio_prompt_get_info(AUDIO_PROMPT_SUCCESS, &samples, &duration_ms);
ESP_LOGI(TAG, "音效时长: %d ms", duration_ms);
```

