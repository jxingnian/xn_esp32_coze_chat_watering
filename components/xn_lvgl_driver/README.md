# LVGL 驱动适配层

本组件提供LVGL 9.2.2与硬件BSP层的适配，负责显示刷新、触摸输入和任务管理。

## 功能

- **显示驱动**: 适配LVGL显示接口到硬件LCD
- **触摸驱动**: 适配LVGL输入设备到硬件触摸屏
- **任务管理**: 创建LVGL定时器任务，处理UI更新
- **缓冲管理**: 管理双缓冲显示内存（PSRAM）

## 配置

### 显示缓冲区
```c
// 缓冲区大小：屏幕的1/20
#define LVGL_BUFFER_SIZE (EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT / 20)
```

### 定时器周期
```c
// LVGL Tick周期：100ms
#define LVGL_TICK_PERIOD_MS 100
```

### 任务配置
- **栈大小**: 64KB (PSRAM)
- **优先级**: 7
- **运行核心**: Core 1

## API接口

### 初始化
```c
// 初始化LVGL驱动（包括硬件初始化）
esp_err_t lvgl_driver_init(void);

// 反初始化
void lvgl_driver_deinit(void);
```

### 回调函数
```c
// 显示刷新回调
void lvgl_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map);

// 触摸读取回调
void lvgl_touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data);

// Tick增加回调
void lvgl_tick_inc_cb(void *arg);
```

## 依赖

- `lvgl/lvgl`: LVGL图形库 (^9.2.0)
- `xn_bsp_spd2010`: 硬件BSP驱动层

## 移植说明

如果更换了BSP层（如从SPD2010换到ST7789），需要：

1. 更新 `CMakeLists.txt` 中的依赖：
   ```cmake
   REQUIRES
       lvgl
       xn_bsp_st7789  # 改为新的BSP组件
       esp_timer
       freertos
   ```

2. 更新 `xn_lvgl.h` 中的头文件引用：
   ```c
   #include "bsp_panel_st7789.h"  // 改为新的面板头文件
   #include "bsp_touch_xxx.h"     // 改为新的触摸头文件
   ```

3. 如果新BSP的API不同，需要在 `xn_lvgl.c` 中适配：
   ```c
   // 示例：如果面板句柄获取函数名不同
   esp_lcd_panel_handle_t official_panel = ST7789_Get_Panel_Handle();
   ```

## 特性

- **硬件加速**: 使用SPI DMA传输，支持硬件完成回调
- **双缓冲**: 减少撕裂，提高显示流畅度
- **动态延时**: 根据LVGL定时器状态动态调整任务延时
- **错误处理**: SPI传输失败时自动通知LVGL，避免死锁
- **4字节对齐**: 自动处理SPD2010的对齐要求

## 性能优化

1. **缓冲区大小**: 设置为屏幕的1/20，平衡内存和性能
2. **任务优先级**: 设置为7，与音频任务同级
3. **PSRAM栈**: 使用外部PSRAM作为任务栈，节省内部RAM
4. **RGB565字节序交换**: 在刷新回调中进行，适配大端序显示屏
