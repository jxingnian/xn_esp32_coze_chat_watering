/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-30 16:00:00
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-10-10 23:58:09
 * @FilePath: \esp-chunfeng\main\LCD_Driver\Display_SPD2010_Official.c
 * @Description: 使用官方SPD2010组件的LCD驱动实现
 */

#include "bsp_panel_spd2010.h"
#include "bsp_i2c_driver.h"

static const char *TAG = "SPD2010_OFFICIAL";

// LCD面板句柄，用于操作LCD面板
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL;

// LCD背光亮度全局变量，默认70%
uint8_t LCD_Backlight = 70;

// LEDC通道配置结构体
static ledc_channel_config_t ledc_channel;

/**
 * @brief SPD2010复位函数
 * 通过控制EXIO2引脚实现SPD2010的硬件复位
 */
void SPD2010_Reset_Official()
{
    Set_EXIO(TCA9554_EXIO2, false);       // 拉低复位引脚
    vTaskDelay(pdMS_TO_TICKS(100));       // 延时100ms
    Set_EXIO(TCA9554_EXIO2, true);        // 拉高复位引脚
    vTaskDelay(pdMS_TO_TICKS(100));       // 延时100ms
}

/**
 * @brief 背光初始化函数
 * 配置GPIO和LEDC定时器，初始化PWM背光控制
 */
void Backlight_Init_Official(void)
{
    // 配置LEDC定时器
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_LS_MODE,
        .timer_num        = LEDC_HS_TIMER,
        .duty_resolution  = LEDC_ResolutionRatio,
        .freq_hz          = 1000,  // 设置输出频率为1kHz
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // 配置LEDC通道
    ledc_channel.channel    = LEDC_HS_CH0_CHANNEL;
    ledc_channel.duty       = 0;
    ledc_channel.gpio_num   = LEDC_HS_CH0_GPIO;
    ledc_channel.speed_mode = LEDC_LS_MODE;
    ledc_channel.hpoint     = 0;
    ledc_channel.timer_sel  = LEDC_HS_TIMER;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));

    // 设置初始背光亮度
    Set_Backlight_Official(LCD_Backlight);

    ESP_LOGI(TAG, "背光初始化完成，当前亮度: %d%%", LCD_Backlight);
}

/**
 * @brief 设置LCD背光亮度
 * @param Light 背光亮度值，范围0-100
 */
void Set_Backlight_Official(uint8_t Light)
{
    if (Light > Backlight_MAX) {
        Light = Backlight_MAX;
    }

    LCD_Backlight = Light;

    // 计算PWM占空比：亮度百分比转换为PWM占空比
    uint32_t duty = (Light * LEDC_MAX_Duty) / Backlight_MAX;

    // 设置PWM占空比
    ESP_ERROR_CHECK(ledc_set_duty(ledc_channel.speed_mode, ledc_channel.channel, duty));
    ESP_ERROR_CHECK(ledc_update_duty(ledc_channel.speed_mode, ledc_channel.channel));

    ESP_LOGI(TAG, "背光亮度设置为: %d%% (PWM占空比: %lu)", Light, duty);
}

/**
 * @brief 初始化SPD2010 QSPI接口
 * @return esp_err_t 初始化结果
 */
esp_err_t SPD2010_QSPI_Init_Official(void)
{
    ESP_LOGI(TAG, "初始化SPI总线");

    // 配置SPI总线 - 使用官方宏定义
    const spi_bus_config_t buscfg = SPD2010_PANEL_BUS_QSPI_CONFIG(
                                        ESP_PANEL_LCD_SPI_IO_SCK,    // SCLK
                                        ESP_PANEL_LCD_SPI_IO_DATA0,  // D0
                                        ESP_PANEL_LCD_SPI_IO_DATA1,  // D1
                                        ESP_PANEL_LCD_SPI_IO_DATA2,  // D2
                                        ESP_PANEL_LCD_SPI_IO_DATA3,  // D3
                                        EXAMPLE_LCD_WIDTH * EXAMPLE_LCD_HEIGHT * 2  // 最大传输大小
                                    );

    esp_err_t ret = spi_bus_initialize(ESP_PANEL_HOST_SPI_ID_DEFAULT, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI总线初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPI总线初始化成功");

    ESP_LOGI(TAG, "安装面板IO");

    // 配置面板IO - 基于官方宏，增大队列深度防止溢出
    esp_lcd_panel_io_spi_config_t io_config = SPD2010_PANEL_IO_QSPI_CONFIG(
                                                  ESP_PANEL_LCD_SPI_IO_CS,     // CS引脚
                                                  NULL,                        // 回调函数（暂时为NULL，后续设置）
                                                  NULL                         // 用户上下文（暂时为NULL，后续设置）
                                              );

    // 优化1：增大SPI传输队列深度：默认10 → 50
    // 原因：Lottie动画渲染速度快，需要大队列缓冲
    io_config.trans_queue_depth = 50;

    // 优化2：提高SPI时钟频率：20MHz → 40MHz
    // ESP32-S3在QSPI模式下支持最高80MHz，40MHz是安全的折中值
    // 好处：传输速度翻倍，单帧时间从30ms降到15ms
    io_config.pclk_hz = 60 * 1000 * 1000;

    ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)ESP_PANEL_HOST_SPI_ID_DEFAULT, &io_config, &io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "面板IO创建失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "面板IO创建成功");

    ESP_LOGI(TAG, "安装SPD2010面板驱动");

    // SPD2010厂商特定配置
    spd2010_vendor_config_t vendor_config = {
        .init_cmds = NULL,
        .init_cmds_size = 0,
        .flags = {
            .use_qspi_interface = 1,  // 使用QSPI接口
        },
    };

    // LCD面板设备配置
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_LCD_PIN_NUM_RST,      // 复位引脚（-1表示不使用）
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,     // RGB像素顺序
        .bits_per_pixel = EXAMPLE_LCD_COLOR_BITS,       // 每像素位数
        .flags = {
            .reset_active_high = 0,                       // 复位低电平有效
        },
        .vendor_config = &vendor_config,                // 厂商特定配置
    };

    ret = esp_lcd_new_panel_spd2010(io_handle, &panel_config, &panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPD2010面板创建失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "SPD2010面板创建成功");

    // 复位LCD面板
    ret = esp_lcd_panel_reset(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "面板复位失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 初始化LCD面板
    ret = esp_lcd_panel_init(panel_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "面板初始化失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 打开显示
    ret = esp_lcd_panel_disp_on_off(panel_handle, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "打开显示失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPD2010初始化完成");
    return ESP_OK;
}

/**
 * @brief SPD2010初始化函数
 */
void SPD2010_Init_Official(void)
{
    SPD2010_Reset_Official();  // 执行硬件复位

    if (SPD2010_QSPI_Init_Official() != ESP_OK) {
        ESP_LOGE(TAG, "SPD2010初始化失败");
    } else {
        ESP_LOGI(TAG, "SPD2010初始化成功");
    }
}

/**
 * @brief LCD初始化主函数
 */
void LCD_Init_Official(void)
{
    // 初始化I2C总线（触摸屏需要）
    esp_err_t ret = I2C_Init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C初始化失败: %s", esp_err_to_name(ret));
        return;
    }

    SPD2010_Init_Official();   // 初始化SPD2010显示驱动
    Backlight_Init_Official();  // 初始化背光控制
    Touch_Init_Official();      // 初始化触摸屏（官方组件版本）

    ESP_LOGI(TAG, "LCD初始化完成");
}

/**
 * @brief LVGL flush完成回调函数
 */
static bool notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    // 注意：这是在SPI中断上下文中执行，不能调用可能阻塞的函数（如ESP_LOGI）
    // 如需调试，可使用 ESP_EARLY_LOGI（无锁，但功能简陋）

    lv_display_t *disp = (lv_display_t *)user_ctx;
    lv_display_flush_ready(disp);
    return false;
}

/**
 * @brief 注册LVGL flush完成回调
 * @param display LVGL显示对象指针
 * @return esp_err_t 注册结果
 */
esp_err_t SPD2010_Register_LVGL_Callback(lv_display_t *display)
{
    if (!io_handle || !display) {
        ESP_LOGE(TAG, "IO句柄或显示对象为空");
        return ESP_ERR_INVALID_ARG;
    }

    // 注册IO面板事件回调
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = notify_lvgl_flush_ready,
    };

    esp_err_t ret = esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, display);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册LVGL回调失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "LVGL flush回调注册成功");
    return ESP_OK;
}

/**
 * @brief 获取面板句柄
 * @return esp_lcd_panel_handle_t 面板句柄
 */
esp_lcd_panel_handle_t SPD2010_Get_Panel_Handle(void)
{
    return panel_handle;
}
