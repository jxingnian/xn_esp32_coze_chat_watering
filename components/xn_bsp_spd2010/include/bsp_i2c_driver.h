#pragma once

#include <stdint.h>
#include <string.h>  // For memcpy
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"


/********************* I2C *********************/
#define I2C_SCL_IO                  10         /*!< GPIO number used for I2C master clock */
#define I2C_SDA_IO                  11         /*!< GPIO number used for I2C master data  */
#define I2C_MASTER_NUM              0         /*!< I2C master i2c port number, the number of i2c peripheral interfaces available will depend on the chip */
#define I2C_MASTER_FREQ_HZ          400000    /*!< I2C master clock frequency */
#define I2C_MASTER_TIMEOUT_MS       1000

// I2C bus handle
extern i2c_master_bus_handle_t i2c_bus_handle;

/**
 * @brief Initialize I2C
 */
esp_err_t I2C_Init(void);
/**
 * @brief Deinitialize I2C
 */
void I2C_Deinit(void);
/**
 * @brief Get I2C bus handle
 */
i2c_master_bus_handle_t I2C_Get_Bus_Handle(void);
// Reg addr is 8 bit
esp_err_t I2C_Write(uint8_t Driver_addr, uint8_t Reg_addr, const uint8_t *Reg_data, uint32_t Length);
esp_err_t I2C_Read(uint8_t Driver_addr, uint8_t Reg_addr, uint8_t *Reg_data, uint32_t Length);