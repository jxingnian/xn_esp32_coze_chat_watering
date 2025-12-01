/*
 * @Author: xingnian j_xingnian@163.com
 * @Date: 2025-08-28 15:16:27
 * @LastEditors: xingnian j_xingnian@163.com
 * @LastEditTime: 2025-08-28 15:32:51
 * @FilePath: \esp-chunfeng\main\LCD_Driver\Display_SPD2010.c
 * @Description: TCA9554PWR I2C扩展IO芯片的驱动实现
 *
 */
#include "bsp_exio_tca9554.h"
#include "bsp_i2c_driver.h"

/*****************************************************  操作寄存器REG   ****************************************************/
/**
 * @brief  读取TCA9554PWR指定寄存器的值
 * @param  REG: 需要读取的寄存器地址
 * @retval 返回寄存器的8位值
 */
uint8_t Read_REG(uint8_t REG)
{
    uint8_t bitsStatus = 0; // 用于存储读取到的寄存器值
    
    // 使用新的I2C驱动读取寄存器
    esp_err_t ret = I2C_Read(TCA9554_ADDRESS, REG, &bitsStatus, 1);
    if (ret != ESP_OK) {
        // 读取失败，返回0
        return 0;
    }
    
    return bitsStatus; // 返回读取到的寄存器值
}

/**
 * @brief  向TCA9554PWR指定寄存器写入数据
 * @param  REG: 需要写入的寄存器地址
 * @param  Data: 要写入的数据
 * @retval 无
 */
void Write_REG(uint8_t REG, uint8_t Data)
{
    // 使用新的I2C驱动写入寄存器
    I2C_Write(TCA9554_ADDRESS, REG, &Data, 1);
}

/********************************************************** 设置EXIO模式 **********************************************************/
/**
 * @brief  设置TCA9554PWR某个引脚的输入/输出模式
 * @param  Pin: 引脚编号（1~8）
 * @param  State: 0=输出模式，1=输入模式
 * @retval 无
 * @note   默认输出模式
 */
void Mode_EXIO(uint8_t Pin, uint8_t State)
{
    uint8_t bitsStatus = Read_REG(TCA9554_CONFIG_REG); // 读取当前配置寄存器状态
    // 设置对应引脚为输入模式（1），其余位保持不变
    uint8_t Data = (0x01 << (Pin - 1)) | bitsStatus;
    Write_REG(TCA9554_CONFIG_REG, Data); // 写回配置寄存器
}

/**
 * @brief  设置TCA9554PWR所有7个引脚的输入/输出模式
 * @param  PinState: 每一位代表一个引脚的模式，0=输出，1=输入
 * @retval 无
 */
void Mode_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_CONFIG_REG, PinState); // 直接写入配置寄存器
}

/********************************************************** 读取EXIO状态 **********************************************************/
/**
 * @brief  读取TCA9554PWR某个引脚的电平状态
 * @param  Pin: 引脚编号（1~8）
 * @retval 该引脚的电平（0或1）
 */
uint8_t Read_EXIO(uint8_t Pin)
{
    uint8_t inputBits = Read_REG(TCA9554_INPUT_REG); // 读取输入寄存器
    uint8_t bitStatus = (inputBits >> (Pin - 1)) & 0x01; // 提取对应引脚的状态
    return bitStatus;
}

/**
 * @brief  读取TCA9554PWR所有引脚的电平状态
 * @retval 8位输入状态，每一位代表一个引脚
 */
uint8_t Read_EXIOS(void)
{
    uint8_t inputBits = Read_REG(TCA9554_INPUT_REG); // 读取输入寄存器
    return inputBits;
}

/********************************************************** 设置EXIO输出状态 **********************************************************/
/**
 * @brief  设置TCA9554PWR某个引脚的输出电平，不影响其他引脚
 * @param  Pin: 引脚编号（1~8）
 * @param  State: 输出电平（true=高，false=低）
 * @retval 无
 */
void Set_EXIO(uint8_t Pin, bool State)
{
    uint8_t Data = 0;
    uint8_t bitsStatus = Read_REG(TCA9554_OUTPUT_REG); // 读取当前输出寄存器状态
    if (Pin < 9 && Pin > 0) { // 检查引脚编号合法性
        if (State) // 设置为高电平
            Data = (0x01 << (Pin - 1)) | bitsStatus;
        else // 设置为低电平
            Data = (~(0x01 << (Pin - 1)) & bitsStatus);
        Write_REG(TCA9554_OUTPUT_REG, Data); // 写回输出寄存器
    } else
        printf("Parameter error, please enter the correct parameter!\r\n"); // 参数错误提示
}

/**
 * @brief  设置TCA9554PWR所有7个引脚的输出电平
 * @param  PinState: 每一位代表一个引脚的输出电平（最高位未用）
 * @retval 无
 */
void Set_EXIOS(uint8_t PinState)
{
    Write_REG(TCA9554_OUTPUT_REG, PinState); // 直接写入输出寄存器
}

/********************************************************** 翻转EXIO状态 **********************************************************/
/**
 * @brief  翻转TCA9554PWR某个引脚的电平
 * @param  Pin: 引脚编号（1~8）
 * @retval 无
 */
void Set_Toggle(uint8_t Pin)
{
    uint8_t bitsStatus = Read_EXIO(Pin); // 读取当前引脚电平
    Set_EXIO(Pin, (bool)!bitsStatus); // 取反后写回
}

/********************************************************* TCA9554PWR 设备初始化 ***********************************************************/
/**
 * @brief  初始化TCA9554PWR所有引脚的输入/输出模式
 * @param  PinState: 每一位代表一个引脚的模式，0=输出，1=输入（最高位未用）
 * @retval 无
 * @note   默认全部为输出模式
 */
void TCA9554PWR_Init(uint8_t PinState)
{
    // i2c_master_init(); // I2C初始化（如有需要可取消注释）
    Mode_EXIOS(PinState); // 设置所有引脚模式
}

/**
 * @brief  EXIO初始化函数，设置所有引脚为输出模式
 * @retval ESP_OK
 */
esp_err_t EXIO_Init(void)
{
    TCA9554PWR_Init(0x00); // 全部引脚设为输出
    return ESP_OK;
}
