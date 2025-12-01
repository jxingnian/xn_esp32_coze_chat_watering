/*
 * @Author: xingnian jixingnian@gmail.com
 * @Date: 2025-11-10 15:33:02
 * @LastEditors: xingnian jixingnian@gmail.com
 * @LastEditTime: 2025-11-15 10:28:48
 * @FilePath: \ESP_ChunFeng\main\bsp\bsp_i2c_driver\bsp_i2c_driver.c
 * @Description: 
 * 
 * Copyright (c) 2025 by ${git_name_email}, All Rights Reserved. 
 */
#include "bsp_i2c_driver.h"

static const char *I2C_TAG = "I2C";

// I2C bus handle
i2c_master_bus_handle_t i2c_bus_handle = NULL;
/**
 * @brief Initialize I2C master bus
 */
esp_err_t I2C_Init(void)
{
    if (i2c_bus_handle != NULL) {
        ESP_LOGW(I2C_TAG, "I2C already initialized");
        return ESP_OK;
    }

    ESP_LOGI(I2C_TAG, "Initializing I2C master bus");

    i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = I2C_MASTER_NUM,
        .scl_io_num = I2C_SCL_IO,
        .sda_io_num = I2C_SDA_IO,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    esp_err_t ret = i2c_new_master_bus(&i2c_bus_config, &i2c_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(I2C_TAG, "Failed to initialize I2C master bus: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(I2C_TAG, "I2C master bus initialized successfully");
    return ESP_OK;
}

/**
 * @brief Deinitialize I2C master bus
 */
void I2C_Deinit(void)
{
    if (i2c_bus_handle != NULL) {
        i2c_del_master_bus(i2c_bus_handle);
        i2c_bus_handle = NULL;
        ESP_LOGI(I2C_TAG, "I2C master bus deinitialized");
    }
}

/**
 * @brief Get I2C bus handle
 */
i2c_master_bus_handle_t I2C_Get_Bus_Handle(void)
{
    return i2c_bus_handle;
}


/**
 * @brief Write data to I2C device register
 * @param Driver_addr I2C device address
 * @param Reg_addr Register address (8-bit)
 * @param Reg_data Data to write
 * @param Length Data length
 * @return esp_err_t ESP_OK on success
 */
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length)
{
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(I2C_TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create device configuration
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = Driver_addr,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(I2C_TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    // Prepare write buffer: [reg_addr][data...]
    uint8_t *write_buf = malloc(Length + 1);
    if (write_buf == NULL) {
        i2c_master_bus_rm_device(dev_handle);
        return ESP_ERR_NO_MEM;
    }

    write_buf[0] = Reg_addr;
    memcpy(&write_buf[1], Reg_data, Length);

    ret = i2c_master_transmit(dev_handle, write_buf, Length + 1, I2C_MASTER_TIMEOUT_MS);
    
    free(write_buf);
    i2c_master_bus_rm_device(dev_handle);
    
    return ret;
}

/**
 * @brief Read data from I2C device register
 * @param Driver_addr I2C device address
 * @param Reg_addr Register address (8-bit)
 * @param Reg_data Buffer to store read data
 * @param Length Data length to read
 * @return esp_err_t ESP_OK on success
 */
esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length)
{
    if (i2c_bus_handle == NULL) {
        ESP_LOGE(I2C_TAG, "I2C not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create device configuration
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = Driver_addr,
        .scl_speed_hz = I2C_MASTER_FREQ_HZ,
    };

    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(i2c_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(I2C_TAG, "Failed to add device: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = i2c_master_transmit_receive(dev_handle, &Reg_addr, 1, Reg_data, Length, I2C_MASTER_TIMEOUT_MS);
    
    i2c_master_bus_rm_device(dev_handle);
    
    return ret;
}
