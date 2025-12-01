/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-30 16:30:00
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-26 21:11:54
 * @FilePath: \xn_esp32_lottie\components\xn_lottie_manager\src\bsp_touch_spd2010.c
 * @Description: 使用官方esp_lcd_touch_spd2010组件的触摸驱动实现
 */

#include "bsp_touch_spd2010.h"
#include "bsp_i2c_driver.h"

static const char *TAG = "TOUCH_SPD2010_OFFICIAL";

// 全局触摸句柄
esp_lcd_touch_handle_t touch_handle = NULL;
esp_lcd_panel_io_handle_t touch_io_handle = NULL;

esp_err_t Touch_Init_Official(void)
{
    ESP_LOGI(TAG, "开始初始化SPD2010触摸控制器（官方组件版本）");

    // Get the shared I2C bus handle
    i2c_master_bus_handle_t i2c_bus = I2C_Get_Bus_Handle();
    if (i2c_bus == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "创建触摸面板IO");

    // 手动配置触摸面板IO，确保频率与I2C总线兼容
    esp_lcd_panel_io_i2c_config_t tp_io_config = {
        .dev_addr = ESP_LCD_TOUCH_IO_I2C_SPD2010_ADDRESS,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,  // 使用与I2C总线相同的频率
        .control_phase_bytes = 1,
        .dc_bit_offset = 0,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags = {
            .disable_control_phase = 1,
        }
    };

    esp_err_t ret = esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &touch_io_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建触摸面板IO失败: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "触摸面板IO创建成功");

    ESP_LOGI(TAG, "配置触摸控制器参数");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = TOUCH_MAX_X,
        .y_max = TOUCH_MAX_Y,
        .rst_gpio_num = TOUCH_RST_PIN,
        .int_gpio_num = TOUCH_INT_PIN,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = TOUCH_SWAP_XY,
            .mirror_x = TOUCH_MIRROR_X,
            .mirror_y = TOUCH_MIRROR_Y,
        },
        .interrupt_callback = NULL,
    };

    ESP_LOGI(TAG, "创建SPD2010触摸控制器");

    ret = esp_lcd_touch_new_i2c_spd2010(touch_io_handle, &tp_cfg, &touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "创建SPD2010触摸控制器失败: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "SPD2010触摸控制器初始化成功");
    return ESP_OK;
}

void Touch_Deinit_Official(void)
{
    ESP_LOGI(TAG, "反初始化SPD2010触摸驱动");

    if (touch_handle) {
        esp_lcd_touch_del(touch_handle);
        touch_handle = NULL;
    }

    if (touch_io_handle) {
        esp_lcd_panel_io_del(touch_io_handle);
        touch_io_handle = NULL;
    }

    ESP_LOGI(TAG, "SPD2010触摸驱动反初始化完成");
}

bool Touch_Get_xy_Official(uint16_t *touch_x, uint16_t *touch_y, uint16_t *strength,
                           uint8_t *touch_count, uint8_t max_points)
{
    if (!touch_handle || !touch_x || !touch_y || !touch_count) {
        return false;
    }

    esp_err_t ret = esp_lcd_touch_read_data(touch_handle);
    if (ret != ESP_OK) {
        ESP_LOGI(TAG, "读取触摸数据失败: %s", esp_err_to_name(ret));
        return false;
    }

    bool touch_pressed = esp_lcd_touch_get_coordinates(touch_handle, touch_x, touch_y,
                                                       strength, touch_count, max_points);

    if (touch_pressed && *touch_count > 0) {
        ESP_LOGI(TAG, "检测到 %d 个触摸点", *touch_count);
        for (int i = 0; i < *touch_count && i < max_points; i++) {
            ESP_LOGI(TAG, "触摸点 %d: (%d, %d)", i, touch_x[i], touch_y[i]);
        }
    }

    return touch_pressed;
}

esp_lcd_touch_handle_t Touch_Get_Handle_Official(void)
{
    return touch_handle;
}

bool Touch_Is_Initialized_Official(void)
{
    return (touch_handle != NULL);
}
