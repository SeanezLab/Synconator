#ifndef GP8403_H
#define GP8403_H

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GP8403_DEFAULT_I2C_ADDRESS  0x58U
#define GP8403_MIN_I2C_ADDRESS      0x58U
#define GP8403_MAX_I2C_ADDRESS      0x5FU
#define GP8403_MAX_DAC_CODE         4095U
#define GP8403_DEFAULT_TIMEOUT_MS   100U
#define GP8403_DMA_BUFFER_SIZE      5U

typedef enum
{
    GP8403_RANGE_5V = 0x00U,
    GP8403_RANGE_10V = 0x11U
} GP8403_OutputRange;

typedef enum
{
    GP8403_CHANNEL_0 = 0U,
    GP8403_CHANNEL_1 = 1U,
    GP8403_CHANNEL_BOTH = 2U
} GP8403_Channel;

typedef struct
{
    I2C_HandleTypeDef *hi2c;
    uint8_t address;              /* Unshifted 7-bit I2C address. */
    GP8403_OutputRange range;
    uint32_t timeout_ms;
    uint8_t dma_buffer[GP8403_DMA_BUFFER_SIZE];
    volatile uint8_t dma_busy;
    volatile HAL_StatusTypeDef dma_status;
    volatile uint32_t i2c_error;
} GP8403;

/**
 * @brief Initialize a GP8403 instance and configure its output range.
 *
 * The I2C peripheral and GPIO pins must already be initialized by STM32 HAL.
 *
 * @param device  Driver instance to initialize.
 * @param hi2c    HAL I2C handle, such as &hi2c1.
 * @param address Unshifted 7-bit address from 0x58 through 0x5F.
 * @param range   Desired 0-5 V or 0-10 V output range.
 */
HAL_StatusTypeDef GP8403_Init(GP8403 *device, I2C_HandleTypeDef *hi2c,
                              uint8_t address, GP8403_OutputRange range);

/**
 * @brief Check whether the DAC acknowledges its I2C address.
 */
HAL_StatusTypeDef GP8403_IsReady(const GP8403 *device);

/**
 * @brief Set the output range used by both DAC channels.
 */
HAL_StatusTypeDef GP8403_SetOutputRange(GP8403 *device,
                                        GP8403_OutputRange range);

/**
 * @brief Set one or both outputs using a 12-bit DAC code.
 *
 * @param code Value from 0 through 4095.
 */
HAL_StatusTypeDef GP8403_SetRaw(const GP8403 *device, GP8403_Channel channel,
                                uint16_t code);

/**
 * @brief Set one or both outputs in millivolts.
 *
 * The allowed range is 0-5000 mV or 0-10000 mV, depending on the configured
 * output range. The voltage is rounded to the nearest 12-bit DAC code.
 */
HAL_StatusTypeDef GP8403_SetMillivolts(const GP8403 *device,
                                       GP8403_Channel channel,
                                       uint16_t millivolts);

/**
 * @brief Start a non-blocking DMA update using a 12-bit DAC code.
 *
 * The transfer buffer is owned by the GP8403 instance. A new DMA update must
 * not be started until GP8403_IsDMABusy() returns zero. Overlapping updates
 * return HAL_BUSY.
 */
HAL_StatusTypeDef GP8403_SetRawDMA(GP8403 *device, GP8403_Channel channel,
                                   uint16_t code);

/**
 * @brief Start a non-blocking DMA update in millivolts.
 */
HAL_StatusTypeDef GP8403_SetMillivoltsDMA(GP8403 *device,
                                          GP8403_Channel channel,
                                          uint16_t millivolts);

/**
 * @brief Return nonzero while this instance owns an active DMA transfer.
 */
uint8_t GP8403_IsDMABusy(const GP8403 *device);

/**
 * @brief Return HAL_BUSY while active, then HAL_OK or HAL_ERROR on completion.
 */
HAL_StatusTypeDef GP8403_GetDMAStatus(const GP8403 *device);

/**
 * @brief Return the HAL I2C error flags captured for the last DMA operation.
 */
uint32_t GP8403_GetI2CError(const GP8403 *device);

/**
 * @brief Forward HAL_I2C_MasterTxCpltCallback() here from the application.
 */
void GP8403_HandleI2CMasterTxComplete(GP8403 *device,
                                       I2C_HandleTypeDef *hi2c);

/**
 * @brief Forward HAL_I2C_ErrorCallback() here from the application.
 */
void GP8403_HandleI2CError(GP8403 *device, I2C_HandleTypeDef *hi2c);

#ifdef __cplusplus
}
#endif

#endif /* GP8403_H */
