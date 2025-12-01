/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-31 23:19:25
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-25 17:13:18
 * @FilePath: \xn_esp32_lottie\components\xn_lottie_manager\src\xn_lottie_manager.c
 * @Description: 简单的Lottie动画管理器实现
 */

 #include "xn_lottie_manager.h"
 #include "xn_lvgl.h"
 #include "esp_log.h"
 #include "esp_heap_caps.h"
 #include "esp_task_wdt.h"
 #include "freertos/FreeRTOS.h"
 #include "freertos/task.h"
 #include "freertos/queue.h"
 #include "esp_spiffs.h"
 #include <string.h>
 #include <stdio.h>
 
 static const char *TAG = "LOTTIE_MANAGER";
 
 // 动画命令类型
 typedef enum {
     LOTTIE_CMD_PLAY,
     LOTTIE_CMD_PLAY_AT_POS,
     LOTTIE_CMD_STOP,
     LOTTIE_CMD_HIDE,
     LOTTIE_CMD_SHOW,
     LOTTIE_CMD_SET_POS,
     LOTTIE_CMD_CENTER,
     LOTTIE_CMD_SHOW_IMAGE,
     LOTTIE_CMD_HIDE_IMAGE
 } lottie_cmd_type_t;
 
 // 动画命令结构
 typedef struct {
     lottie_cmd_type_t type;
     union {
         struct {
             int anim_type;
         } play;
         struct {
             int anim_type;
             int16_t x;
             int16_t y;
         } play_at_pos;
         struct {
             int anim_type;
         } stop;
         struct {
             int16_t x;
             int16_t y;
         } pos;
         struct {
             char path[64];
             uint16_t width;
             uint16_t height;
         } image;
     } data;
 } lottie_cmd_t;
 
 // 动画配置结构
 typedef struct {
     const char *file_path;
     uint16_t width;
     uint16_t height;
 } lottie_anim_config_t;
 
 // 动画配置表 - 全屏显示配置（屏幕尺寸：412x412）
 static const lottie_anim_config_t anim_configs[] = {
     [LOTTIE_ANIM_WIFI]    = {"/lottie/loading.json",        256, 256},  // WiFi加载
     [LOTTIE_ANIM_MIC]     = {"/lottie/emoji_kaixin.json",   128, 128},  // mic
     [LOTTIE_ANIM_SPEAK]   = {"/lottie/speak.json",          400, 277},  // 说话
     [LOTTIE_ANIM_THINK]   = {"/lottie/emoji_think.json",    400, 400},  // 思考
     [LOTTIE_ANIM_COOL]    = {"/lottie/emoji_cool.json",     400, 400},  // 酷
     [LOTTIE_ANIM_LOADING] = {"/lottie/loading.json",        200, 200},  // 通用加载
     [LOTTIE_ANIM_OTA]     = {"/lottie/loading.json",        400, 400},  // OTA升级动画
     // 可以继续添加更多动画配置...
 };
 
 #define ANIM_CONFIG_COUNT (sizeof(anim_configs) / sizeof(anim_configs[0]))
 
 // 静态任务相关 - 参考main.c的实现
 #define LOTTIE_TASK_STACK_SIZE (1024*350/sizeof(StackType_t))  // 8KB栈
 static EXT_RAM_BSS_ATTR StackType_t lottie_task_stack[LOTTIE_TASK_STACK_SIZE];  // PSRAM栈
 static StaticTask_t lottie_task_buffer;  // 内部RAM控制块
 
 // 全局变量管理
 static lv_obj_t *g_lottie_obj = NULL;
 static uint8_t *g_lottie_buffer = NULL;
 static bool g_initialized = false;
 static int g_current_anim_type = -1;  // 当前播放的动画类型
 static QueueHandle_t g_cmd_queue = NULL;
 static TaskHandle_t g_anim_task = NULL;
 static SemaphoreHandle_t g_anim_mutex = NULL;  // 动画操作互斥锁
 static volatile bool g_anim_busy = false;      // 动画是否正在操作中
 static lv_obj_t *g_image_obj = NULL;          // 图片对象
 
 // 实际执行动画播放的内部函数
 static bool _lottie_play_internal(int anim_type)
 {
     if (anim_type < 0 || anim_type >= ANIM_CONFIG_COUNT) {
         ESP_LOGE(TAG, "无效的动画类型: %d", anim_type);
         return false;
     }
 
     const lottie_anim_config_t *config = &anim_configs[anim_type];
     if (!config->file_path) {
         ESP_LOGE(TAG, "动画类型 %d 未配置", anim_type);
         return false;
     }
 
     ESP_LOGI(TAG, "播放动画类型: %d", anim_type);
 
     // 直接使用现有的lottie_manager_play函数
     bool result = lottie_manager_play(config->file_path, config->width, config->height);
     if (result) {
         g_current_anim_type = anim_type;
     }
 
     return result;
 }
 
 // 实际执行动画播放并设置位置的内部函数
 static bool _lottie_play_at_pos_internal(int anim_type, int16_t x, int16_t y)
 {
     if (anim_type < 0 || anim_type >= ANIM_CONFIG_COUNT) {
         ESP_LOGE(TAG, "无效的动画类型: %d", anim_type);
         return false;
     }
 
     const lottie_anim_config_t *config = &anim_configs[anim_type];
     if (!config->file_path) {
         ESP_LOGE(TAG, "动画类型 %d 未配置", anim_type);
         return false;
     }
 
     ESP_LOGI(TAG, "播放动画类型: %d，中心偏移: (%d, %d)", anim_type, x, y);
 
     // 直接使用新的lottie_manager_play_at_pos函数
     bool result = lottie_manager_play_at_pos(config->file_path, config->width, config->height, x, y);
     if (result) {
         g_current_anim_type = anim_type;
     }
 
     return result;
 }
 
 // 停止动画的内部函数
 static void _lottie_stop_internal(int anim_type)
 {
     // -1 表示停止所有动画，或者检查是否是当前播放的动画类型
     if (anim_type == -1 || anim_type == g_current_anim_type) {
         ESP_LOGI(TAG, "停止动画类型: %d (当前: %d)", anim_type, g_current_anim_type);
 
         // 直接使用现有的lottie_manager_stop函数
         lottie_manager_stop();
         g_current_anim_type = -1;
     } else {
         ESP_LOGW(TAG, "动画类型 %d 未在播放中，当前播放: %d", anim_type, g_current_anim_type);
     }
 }
 
 // 动画处理静态任务 - 参考main.c的lvgl_timer_task
 static void lottie_task(void *pvParameters)
 {
     lottie_cmd_t cmd;
 
     ESP_LOGI(TAG, "动画处理任务启动");
 
     while (1) {
         if (xQueueReceive(g_cmd_queue, &cmd, portMAX_DELAY) == pdTRUE) {
             switch (cmd.type) {
             case LOTTIE_CMD_PLAY:
                 _lottie_play_internal(cmd.data.play.anim_type);
                 break;
 
             case LOTTIE_CMD_PLAY_AT_POS:
                 _lottie_play_at_pos_internal(cmd.data.play_at_pos.anim_type,
                                              cmd.data.play_at_pos.x,
                                              cmd.data.play_at_pos.y);
                 break;
 
             case LOTTIE_CMD_STOP:
                 _lottie_stop_internal(cmd.data.stop.anim_type);
                 break;
 
             case LOTTIE_CMD_HIDE:
                 if (g_lottie_obj) {
                     lv_lock();
                     lv_obj_add_flag(g_lottie_obj, LV_OBJ_FLAG_HIDDEN);
                     lv_unlock();
                 }
                 break;
 
             case LOTTIE_CMD_SHOW:
                 if (g_lottie_obj) {
                     lv_lock();
                     lv_obj_clear_flag(g_lottie_obj, LV_OBJ_FLAG_HIDDEN);
                     lv_unlock();
                 }
                 break;
 
             case LOTTIE_CMD_SET_POS:
                 if (g_lottie_obj) {
                     lv_lock();
                     lv_obj_set_pos(g_lottie_obj, cmd.data.pos.x, cmd.data.pos.y);
                     lv_unlock();
                 }
                 break;
 
             case LOTTIE_CMD_CENTER:
                 if (g_lottie_obj) {
                     lv_lock();
                     lv_obj_center(g_lottie_obj);
                     lv_unlock();
                 }
                 break;
 
             case LOTTIE_CMD_SHOW_IMAGE:
                 ESP_LOGI(TAG, "处理显示图片命令: %s (%dx%d)", 
                          cmd.data.image.path, cmd.data.image.width, cmd.data.image.height);
                 
                 lv_lock();
                 if (g_lottie_obj) {
                     lv_obj_add_flag(g_lottie_obj, LV_OBJ_FLAG_HIDDEN);
                     ESP_LOGI(TAG, "已隐藏Lottie动画");
                 }
                 if (g_image_obj) {
                     lv_obj_delete(g_image_obj);
                     g_image_obj = NULL;
                     ESP_LOGI(TAG, "已删除旧图片");
                 }
                 
                 g_image_obj = lv_image_create(lv_screen_active());
                 if (g_image_obj) {
                     ESP_LOGI(TAG, "图片对象创建成功");
                     lv_image_set_src(g_image_obj, cmd.data.image.path);
                     if (cmd.data.image.width > 0 && cmd.data.image.height > 0) {
                         lv_obj_set_size(g_image_obj, cmd.data.image.width, cmd.data.image.height);
                     }
                     lv_obj_center(g_image_obj);
                     ESP_LOGI(TAG, "✅ 图片显示成功: %s", cmd.data.image.path);
                 } else {
                     ESP_LOGE(TAG, "❌ 创建图片对象失败");
                 }
                 lv_unlock();
                 break;
 
             case LOTTIE_CMD_HIDE_IMAGE:
                 ESP_LOGI(TAG, "处理隐藏图片命令");
                 lv_lock();
                 if (g_image_obj) {
                     lv_obj_delete(g_image_obj);
                     g_image_obj = NULL;
                     ESP_LOGI(TAG, "图片已删除");
                 }
                 if (g_lottie_obj) {
                     lv_obj_clear_flag(g_lottie_obj, LV_OBJ_FLAG_HIDDEN);
                     ESP_LOGI(TAG, "已恢复Lottie动画");
                 }
                 lv_unlock();
                 break;
 
             default:
                 ESP_LOGW(TAG, "未知命令类型: %d", cmd.type);
                 break;
             }
         }
     }
 }
 
 static bool lottie_manager_init(void)
 {
     if (g_initialized) {
         ESP_LOGW(TAG, "已经初始化过了");
         return true;
     }
 
     ESP_LOGI(TAG, "初始化 Lottie 管理器");
 
     // 检查屏幕是否有效
     lv_obj_t *screen = lv_screen_active();
     if (screen == NULL) {
         ESP_LOGE(TAG, "无法获取活动屏幕");
         return false;
     }
 
     // 创建互斥锁
     g_anim_mutex = xSemaphoreCreateMutex();
     if (!g_anim_mutex) {
         ESP_LOGE(TAG, "创建互斥锁失败");
         return false;
     }
 
     // 创建命令队列
     g_cmd_queue = xQueueCreate(10, sizeof(lottie_cmd_t));
     if (!g_cmd_queue) {
         ESP_LOGE(TAG, "创建命令队列失败");
         vSemaphoreDelete(g_anim_mutex);
         return false;
     }
 
     // 创建动画处理静态任务 - 参考main.c的实现
     g_anim_task = xTaskCreateStatic(
                       lottie_task,                // 任务函数
                       "lottie_task",              // 任务名称
                       LOTTIE_TASK_STACK_SIZE,     // 栈大小
                       NULL,                       // 任务参数
                       3,                          // 优先级3（动画管理，可被抢占）
                       lottie_task_stack,          // 栈数组(PSRAM)
                       &lottie_task_buffer         // 任务控制块(内部RAM)
                   );
 
     if (g_anim_task == NULL) {
         ESP_LOGE(TAG, "创建动画处理任务失败");
         vQueueDelete(g_cmd_queue);
         g_cmd_queue = NULL;
         return false;
     }
 
     g_initialized = true;
     ESP_LOGI(TAG, "Lottie 管理器初始化完成，动画任务已启动");
     return true;
 }
 
 bool lottie_manager_play(const char *file_path, uint16_t width, uint16_t height)
 {
     if (!g_initialized) {
         ESP_LOGE(TAG, "管理器未初始化");
         return false;
     }
 
     if (!file_path) {
         ESP_LOGE(TAG, "文件路径无效");
         return false;
     }
 
     // 获取互斥锁，确保同一时间只有一个动画操作
     if (xSemaphoreTake(g_anim_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
         ESP_LOGE(TAG, "获取互斥锁超时");
         return false;
     }
 
     // 等待之前的操作完全完成
     uint32_t wait_count = 0;
     while (g_anim_busy && wait_count < 100) {
         vTaskDelay(pdMS_TO_TICKS(10));
         wait_count++;
     }
 
     if (g_anim_busy) {
         ESP_LOGE(TAG, "等待动画操作完成超时");
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     g_anim_busy = true;
 
     ESP_LOGI(TAG, "播放动画: %s (%dx%d), 当前动画: %d", file_path, width, height, g_current_anim_type);
 
     // 先清理之前的动画（包含同步等待）
     lottie_manager_stop();
 
     // 等待确保LVGL完全处理完删除操作和内存释放
     // 增加延迟以避免SPI传输队列冲突
     vTaskDelay(pdMS_TO_TICKS(100));
 
     // 第一步：在锁外读取文件到内存（耗时操作）
     FILE *fp = fopen(file_path, "rb");
     if (!fp) {
         ESP_LOGE(TAG, "无法打开文件: %s", file_path);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     fseek(fp, 0, SEEK_END);
     size_t file_size = ftell(fp);
     fseek(fp, 0, SEEK_SET);

    ESP_LOGI(TAG, "Lottie JSON 文件: %s, 大小: %u 字节", file_path, (unsigned)file_size);
     uint8_t *file_data = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
     if (!file_data) {
         ESP_LOGE(TAG, "文件缓冲区分配失败 (需要 %zu 字节)", file_size);
         fclose(fp);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     size_t read_size = fread(file_data, 1, file_size, fp);
     fclose(fp);
 
     if (read_size != file_size) {
         ESP_LOGE(TAG, "文件读取失败");
         heap_caps_free(file_data);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     // 第二步：在锁内操作LVGL对象（快速操作）
     lv_lock();
 
     g_lottie_obj = lv_lottie_create(lv_screen_active());
     if (!g_lottie_obj) {
         lv_unlock();
         ESP_LOGE(TAG, "创建 Lottie 对象失败");
         heap_caps_free(file_data);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     // 分配渲染缓冲区
     size_t buffer_size = width * height * 4; // ARGB8888
     g_lottie_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
     if (!g_lottie_buffer) {
         ESP_LOGE(TAG, "PSRAM缓冲区分配失败 (需要 %zu 字节)", buffer_size);
         lv_obj_del(g_lottie_obj);
         lv_unlock();
         heap_caps_free(file_data);
         g_lottie_obj = NULL;
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     // 设置缓冲区和数据源（使用内存数据，避免文件IO）
     lv_lottie_set_buffer(g_lottie_obj, width, height, g_lottie_buffer);
     lv_lottie_set_src_data(g_lottie_obj, file_data, file_size);
     lv_obj_center(g_lottie_obj);
 
     lv_unlock();
 
     // 释放文件数据（已被ThorVG解析）
     heap_caps_free(file_data);
 
     ESP_LOGI(TAG, "动画播放成功");
 
     g_anim_busy = false;
     xSemaphoreGive(g_anim_mutex);
     return true;
 }
 
 bool lottie_manager_play_at_pos(const char *file_path, uint16_t width, uint16_t height, int16_t x, int16_t y)
 {
     if (!g_initialized) {
         ESP_LOGE(TAG, "管理器未初始化");
         return false;
     }
 
     if (!file_path) {
         ESP_LOGE(TAG, "文件路径无效");
         return false;
     }
 
     // 获取互斥锁，确保同一时间只有一个动画操作
     if (xSemaphoreTake(g_anim_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
         ESP_LOGE(TAG, "获取互斥锁超时");
         return false;
     }
 
     // 等待之前的操作完全完成
     uint32_t wait_count = 0;
     while (g_anim_busy && wait_count < 100) {
         vTaskDelay(pdMS_TO_TICKS(10));
         wait_count++;
     }
 
     if (g_anim_busy) {
         ESP_LOGE(TAG, "等待动画操作完成超时");
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     g_anim_busy = true;
 
     ESP_LOGI(TAG, "播放动画: %s (%dx%d) 中心偏移: (%d, %d), 当前动画: %d",
              file_path, width, height, x, y, g_current_anim_type);
 
     // 先清理之前的动画（包含同步等待）
     lottie_manager_stop();
 
     // 等待确保LVGL完全处理完删除操作和内存释放
     // 增加延迟以避免SPI传输队列冲突
     vTaskDelay(pdMS_TO_TICKS(100));
 
     // 第一步：在锁外读取文件到内存（耗时操作）
     FILE *fp = fopen(file_path, "rb");
     if (!fp) {
         ESP_LOGE(TAG, "无法打开文件: %s", file_path);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     fseek(fp, 0, SEEK_END);
     size_t file_size = ftell(fp);
     fseek(fp, 0, SEEK_SET);
 
     uint8_t *file_data = (uint8_t *)heap_caps_malloc(file_size, MALLOC_CAP_SPIRAM);
     if (!file_data) {
         ESP_LOGE(TAG, "文件缓冲区分配失败 (需要 %zu 字节)", file_size);
         fclose(fp);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     size_t read_size = fread(file_data, 1, file_size, fp);
     fclose(fp);
 
     if (read_size != file_size) {
         ESP_LOGE(TAG, "文件读取失败");
         heap_caps_free(file_data);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     // 第二步：在锁内操作LVGL对象（快速操作）
     lv_lock();
 
     g_lottie_obj = lv_lottie_create(lv_screen_active());
     if (!g_lottie_obj) {
         lv_unlock();
         ESP_LOGE(TAG, "创建 Lottie 对象失败");
         heap_caps_free(file_data);
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     // 分配渲染缓冲区
     size_t buffer_size = width * height * 4; // ARGB8888
     g_lottie_buffer = heap_caps_malloc(buffer_size, MALLOC_CAP_SPIRAM);
     if (!g_lottie_buffer) {
         ESP_LOGE(TAG, "PSRAM缓冲区分配失败 (需要 %zu 字节)", buffer_size);
         lv_obj_del(g_lottie_obj);
         lv_unlock();
         heap_caps_free(file_data);
         g_lottie_obj = NULL;
         g_anim_busy = false;
         xSemaphoreGive(g_anim_mutex);
         return false;
     }
 
     // 设置缓冲区和数据源（使用内存数据，避免文件IO）
     lv_lottie_set_buffer(g_lottie_obj, width, height, g_lottie_buffer);
     lv_lottie_set_src_data(g_lottie_obj, file_data, file_size);
     lv_obj_align(g_lottie_obj, LV_ALIGN_CENTER, x, y);
 
     lv_unlock();
 
     // 释放文件数据（已被ThorVG解析）
     heap_caps_free(file_data);
 
     ESP_LOGI(TAG, "动画播放成功，中心对齐偏移: (%d, %d)", x, y);
 
     g_anim_busy = false;
     xSemaphoreGive(g_anim_mutex);
     return true;
 }
 
 void lottie_manager_stop(void)
 {
     if (g_lottie_obj) {
         ESP_LOGI(TAG, "停止动画");
 
         // 先隐藏对象，避免在删除过程中继续渲染
         lv_lock();
         lv_obj_add_flag(g_lottie_obj, LV_OBJ_FLAG_HIDDEN);
         lv_obj_invalidate(g_lottie_obj);  // 标记需要重绘
         lv_unlock();
 
         // 【关键修复】等待所有LVGL刷新和DMA传输完成
         // 这样可以确保在删除对象前，所有屏幕刷新的DMA都已完成
         lv_display_t *disp = lv_display_get_default();
         if (disp) {
             uint32_t wait_count = 0;
             const uint32_t max_wait_ms = 500;  // 最多等待500ms
             const uint32_t check_interval_ms = 10;
 
             // 循环等待直到显示器空闲（无刷新活动）
             while (wait_count < max_wait_ms) {
                 vTaskDelay(pdMS_TO_TICKS(check_interval_ms));
 
                 uint32_t inactive_time = lv_display_get_inactive_time(disp);
                 // ⚠️ 增加空闲阈值到500ms，更保守地删除对象
                 if (inactive_time > 500) {
                     ESP_LOGI(TAG, "LVGL显示已空闲 %lu ms，安全删除", inactive_time);
                     break;
                 }
 
                 wait_count += check_interval_ms;
             }
 
             if (wait_count >= max_wait_ms) {
                 ESP_LOGW(TAG, "等待LVGL空闲超时，强制删除");
             }
         } else {
             // 后备方案：如果无法获取display，使用固定延迟
             vTaskDelay(pdMS_TO_TICKS(200));
         }
 
         // 安全删除LVGL对象（增加延迟避免释放后使用）
         lv_lock();
         lv_obj_del(g_lottie_obj);
         lv_unlock();
         g_lottie_obj = NULL;
 
         // 增加等待时间，确保LVGL主循环完全处理完删除操作
         vTaskDelay(pdMS_TO_TICKS(300));
     }
 
     if (g_lottie_buffer) {
         heap_caps_free(g_lottie_buffer);
         g_lottie_buffer = NULL;
 
         // 等待内存完全释放
         vTaskDelay(pdMS_TO_TICKS(50));
     }
 }
 
 void lottie_manager_hide(void)
 {
     if (g_lottie_obj) {
         lv_lock();
         lv_obj_add_flag(g_lottie_obj, LV_OBJ_FLAG_HIDDEN);
         lv_unlock();
     }
 }
 
 void lottie_manager_show(void)
 {
     if (g_lottie_obj) {
         lv_lock();
         lv_obj_clear_flag(g_lottie_obj, LV_OBJ_FLAG_HIDDEN);
         lv_unlock();
     }
 }
 
 void lottie_manager_set_pos(int16_t x, int16_t y)
 {
     if (g_lottie_obj) {
         lv_lock();
         lv_obj_set_pos(g_lottie_obj, x, y);
         lv_unlock();
     }
 }
 
 void lottie_manager_center(void)
 {
     if (g_lottie_obj) {
         lv_lock();
         lv_obj_center(g_lottie_obj);
         lv_unlock();
     }
 }
 
 bool lottie_manager_play_anim(int anim_type)
 {
     if (!g_initialized || !g_cmd_queue) {
         ESP_LOGE(TAG, "管理器未初始化");
         return false;
     }
 
     if (anim_type < 0 || anim_type >= ANIM_CONFIG_COUNT) {
         ESP_LOGE(TAG, "无效的动画类型: %d", anim_type);
         return false;
     }
 
     lottie_cmd_t cmd;
     cmd.type = LOTTIE_CMD_PLAY;
     cmd.data.play.anim_type = anim_type;
 
     if (xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
         ESP_LOGE(TAG, "发送播放命令失败，动画类型: %d", anim_type);
         return false;
     }
 
     ESP_LOGI(TAG, "播放命令已发送，动画类型: %d", anim_type);
     return true;
 }
 
 bool lottie_manager_play_anim_at_pos(int anim_type, int16_t x, int16_t y)
 {
     if (!g_initialized || !g_cmd_queue) {
         ESP_LOGE(TAG, "管理器未初始化");
         return false;
     }
 
     if (anim_type < 0 || anim_type >= ANIM_CONFIG_COUNT) {
         ESP_LOGE(TAG, "无效的动画类型: %d", anim_type);
         return false;
     }
 
     lottie_cmd_t cmd;
     cmd.type = LOTTIE_CMD_PLAY_AT_POS;
     cmd.data.play_at_pos.anim_type = anim_type;
     cmd.data.play_at_pos.x = x;
     cmd.data.play_at_pos.y = y;
 
     if (xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
         ESP_LOGE(TAG, "发送播放命令失败，动画类型: %d，位置: (%d, %d)", anim_type, x, y);
         return false;
     }
 
     ESP_LOGI(TAG, "播放命令已发送，动画类型: %d，位置: (%d, %d)", anim_type, x, y);
     return true;
 }
 
 void lottie_manager_stop_anim(int anim_type)
 {
     if (!g_initialized || !g_cmd_queue) {
         ESP_LOGW(TAG, "管理器未初始化");
         return;
     }
 
     lottie_cmd_t cmd;
     cmd.type = LOTTIE_CMD_STOP;
     cmd.data.stop.anim_type = anim_type;
 
     if (xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(100)) != pdTRUE) {
         ESP_LOGE(TAG, "发送停止命令失败，动画类型: %d", anim_type);
     } else {
         ESP_LOGI(TAG, "停止命令已发送，动画类型: %d", anim_type);
     }
 }
 
 bool lottie_manager_show_image(const char *img_path, uint16_t width, uint16_t height)
 {
     if (!g_initialized) {
         ESP_LOGE(TAG, "管理器未初始化");
         return false;
     }
     
     if (!img_path) {
         ESP_LOGE(TAG, "图片路径为空");
         return false;
     }
 
     ESP_LOGI(TAG, "发送显示图片命令: %s (%dx%d)", img_path, width, height);
 
     lottie_cmd_t cmd;
     cmd.type = LOTTIE_CMD_SHOW_IMAGE;
     snprintf(cmd.data.image.path, sizeof(cmd.data.image.path), "%s", img_path);
     cmd.data.image.width = width;
     cmd.data.image.height = height;
 
     if (xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
         ESP_LOGI(TAG, "图片命令已发送到队列");
         return true;
     } else {
         ESP_LOGE(TAG, "发送图片命令到队列失败");
         return false;
     }
 }
 
 void lottie_manager_hide_image(void)
 {
     if (!g_initialized) {
         ESP_LOGW(TAG, "管理器未初始化");
         return;
     }
 
     ESP_LOGI(TAG, "发送隐藏图片命令");
 
     lottie_cmd_t cmd;
     cmd.type = LOTTIE_CMD_HIDE_IMAGE;
     
     if (xQueueSend(g_cmd_queue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE) {
         ESP_LOGI(TAG, "隐藏图片命令已发送");
     } else {
         ESP_LOGE(TAG, "发送隐藏图片命令失败");
     }
 }

// ---------------- Lottie 应用初始化封装 ----------------

static esp_err_t xn_lottie_mount_spiffs(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/lottie",
        .partition_label = "lottie_spiffs",
        .max_files = 8,
        .format_if_mount_failed = true,
    };

    esp_err_t ret = esp_vfs_spiffs_register(&conf);
    if (ret == ESP_ERR_INVALID_STATE) {
        // 已经挂载，视为成功
        return ESP_OK;
    }
    return ret;
}

esp_err_t xn_lottie_manager_init(const xn_lottie_app_config_t *cfg)
{
    (void)cfg; // 目前暂未使用，预留给多屏等扩展

    esp_err_t ret = xn_lottie_mount_spiffs();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "挂载 Lottie SPIFFS 失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化 LVGL + 显示 / 触摸驱动
    ret = lvgl_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LVGL 驱动初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化 Lottie 管理器本身
    if (!lottie_manager_init()) {
        ESP_LOGE(TAG, "Lottie 管理器初始化失败");
        return ESP_FAIL;
    }

    // 这里可以根据需要启动一个默认动画，例如加载动画
    // lottie_manager_play_anim(LOTTIE_ANIM_LOADING);

    return ESP_OK;
}