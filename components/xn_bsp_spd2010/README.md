# BSP SPD2010 硬件驱动组件

本组件提供ESP32-S3 + SPD2010显示屏的底层硬件驱动。

## 硬件配置

- **显示屏**: SPD2010 (412x412, RGB565, QSPI接口)
- **触摸屏**: SPD2010 (I2C接口)
- **IO扩展**: TCA9554 (I2C接口)
- **I2C总线**: 400kHz

## 引脚定义

### SPI (QSPI模式)
- SCK: GPIO40
- D0: GPIO46
- D1: GPIO45
- D2: GPIO42
- D3: GPIO41
- CS: GPIO21

### I2C
- SDA: GPIO11
- SCL: GPIO10

### 其他
- 背光PWM: GPIO5
- 复位: 通过TCA9554 EXIO2控制

## API接口

### 初始化
```c
// 初始化LCD（包括I2C、显示面板、触摸屏）
void LCD_Init_Official(void);

// 单独初始化I2C
esp_err_t I2C_Init(void);
```

### 显示面板
```c
// 获取面板句柄
esp_lcd_panel_handle_t SPD2010_Get_Panel_Handle(void);

// 注册LVGL回调
esp_err_t SPD2010_Register_LVGL_Callback(lv_display_t *display);

// 设置背光亮度 (0-100)
void Set_Backlight_Official(uint8_t Light);
```

### 触摸屏
```c
// 读取触摸数据
bool Touch_Get_xy_Official(uint16_t *touch_x, uint16_t *touch_y, 
                           uint16_t *strength, uint8_t *touch_count, 
                           uint8_t max_points);

// 获取触摸句柄
esp_lcd_touch_handle_t Touch_Get_Handle_Official(void);
```

### IO扩展
```c
// 设置EXIO引脚电平
void Set_EXIO(uint8_t Pin, bool State);

// 读取EXIO引脚电平
uint8_t Read_EXIO(uint8_t Pin);
```

## 移植到其他硬件

如果要移植到其他显示屏（如ST7789、ILI9341等），请：

1. 复制本组件并重命名（如 `xn_bsp_st7789`）
2. 修改 `bsp_panel_xxx.c/h` 文件，适配新的显示驱动
3. 修改 `bsp_touch_xxx.c/h` 文件，适配新的触摸驱动
4. 保持相同的API接口名称，或在LVGL驱动层做适配
5. 更新 `CMakeLists.txt` 和 `idf_component.yml` 中的依赖

## 依赖

- `espressif/esp_lcd_spd2010`: SPD2010显示驱动
- `espressif/esp_lcd_touch_spd2010`: SPD2010触摸驱动
- ESP-IDF driver组件 (I2C, SPI, GPIO, LEDC)
