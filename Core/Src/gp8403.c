#include "gp8403.h"

#define GP8403_REG_OUTPUT_RANGE  0x01U
#define GP8403_REG_CHANNEL_0     0x02U
#define GP8403_REG_CHANNEL_1     0x04U

#define GP8403_I2C_READY_TRIALS  2U

static uint8_t GP8403_IsValidRange(GP8403_OutputRange range)
{
    return (range == GP8403_RANGE_5V) || (range == GP8403_RANGE_10V);
}

static uint8_t GP8403_IsValidDevice(const GP8403 *device)
{
    if ((device == NULL) || (device->hi2c == NULL))
    {
        return 0U;
    }

    return (device->address >= GP8403_MIN_I2C_ADDRESS) &&
           (device->address <= GP8403_MAX_I2C_ADDRESS);
}

static uint16_t GP8403_GetHALAddress(const GP8403 *device)
{
    /* STM32 HAL expects a 7-bit I2C address shifted left by one bit. */
    return (uint16_t)(device->address << 1U);
}

static HAL_StatusTypeDef GP8403_PrepareRawTransfer(GP8403_Channel channel,
                                                    uint16_t code,
                                                    uint8_t *register_address,
                                                    uint8_t *payload,
                                                    uint16_t *payload_size)
{
    uint16_t aligned_code;

    if ((channel > GP8403_CHANNEL_BOTH) ||
        (code > GP8403_MAX_DAC_CODE) ||
        (register_address == NULL) || (payload == NULL) ||
        (payload_size == NULL))
    {
        return HAL_ERROR;
    }

    /* The GP8403 expects the 12-bit value left-aligned in a 16-bit word. */
    aligned_code = (uint16_t)(code << 4U);
    payload[0] = (uint8_t)(aligned_code & 0xFFU);
    payload[1] = (uint8_t)(aligned_code >> 8U);

    if (channel == GP8403_CHANNEL_1)
    {
        *register_address = GP8403_REG_CHANNEL_1;
        *payload_size = 2U;
    }
    else
    {
        *register_address = GP8403_REG_CHANNEL_0;
        *payload_size = 2U;

        if (channel == GP8403_CHANNEL_BOTH)
        {
            payload[2] = payload[0];
            payload[3] = payload[1];
            *payload_size = 4U;
        }
    }

    return HAL_OK;
}

static HAL_StatusTypeDef GP8403_MillivoltsToCode(const GP8403 *device,
                                                  uint16_t millivolts,
                                                  uint16_t *code)
{
    uint16_t maximum_millivolts;

    if (!GP8403_IsValidDevice(device) ||
        !GP8403_IsValidRange(device->range) || (code == NULL))
    {
        return HAL_ERROR;
    }

    maximum_millivolts = (device->range == GP8403_RANGE_5V) ? 5000U : 10000U;
    if (millivolts > maximum_millivolts)
    {
        return HAL_ERROR;
    }

    *code = (uint16_t)(((uint32_t)millivolts * GP8403_MAX_DAC_CODE +
                        (maximum_millivolts / 2U)) /
                       maximum_millivolts);

    return HAL_OK;
}

HAL_StatusTypeDef GP8403_Init(GP8403 *device, I2C_HandleTypeDef *hi2c,
                              uint8_t address, GP8403_OutputRange range)
{
    HAL_StatusTypeDef status;

    if ((device == NULL) || (hi2c == NULL) ||
        (address < GP8403_MIN_I2C_ADDRESS) ||
        (address > GP8403_MAX_I2C_ADDRESS) ||
        !GP8403_IsValidRange(range))
    {
        return HAL_ERROR;
    }

    device->hi2c = hi2c;
    device->address = address;
    device->range = range;
    device->timeout_ms = GP8403_DEFAULT_TIMEOUT_MS;
    device->dma_busy = 0U;
    device->dma_status = HAL_OK;
    device->i2c_error = HAL_I2C_ERROR_NONE;

    status = GP8403_IsReady(device);
    if (status != HAL_OK)
    {
        return status;
    }

    return GP8403_SetOutputRange(device, range);
}

HAL_StatusTypeDef GP8403_IsReady(const GP8403 *device)
{
    if (!GP8403_IsValidDevice(device))
    {
        return HAL_ERROR;
    }

    return HAL_I2C_IsDeviceReady(device->hi2c, GP8403_GetHALAddress(device),
                                 GP8403_I2C_READY_TRIALS,
                                 device->timeout_ms);
}

HAL_StatusTypeDef GP8403_SetOutputRange(GP8403 *device,
                                        GP8403_OutputRange range)
{
    HAL_StatusTypeDef status;
    uint8_t range_value = (uint8_t)range;

    if (!GP8403_IsValidDevice(device) || !GP8403_IsValidRange(range))
    {
        return HAL_ERROR;
    }

    status = HAL_I2C_Mem_Write(device->hi2c, GP8403_GetHALAddress(device),
                               GP8403_REG_OUTPUT_RANGE,
                               I2C_MEMADD_SIZE_8BIT, &range_value,
                               sizeof(range_value), device->timeout_ms);

    if (status == HAL_OK)
    {
        device->range = range;
    }

    return status;
}

HAL_StatusTypeDef GP8403_SetRaw(const GP8403 *device, GP8403_Channel channel,
                                uint16_t code)
{
    HAL_StatusTypeDef status;
    uint8_t register_address;
    uint8_t payload[GP8403_DMA_BUFFER_SIZE];
    uint16_t payload_size;

    if (!GP8403_IsValidDevice(device))
    {
        return HAL_ERROR;
    }

    status = GP8403_PrepareRawTransfer(channel, code, &register_address,
                                       payload, &payload_size);
    if (status != HAL_OK)
    {
        return status;
    }

    return HAL_I2C_Mem_Write(device->hi2c, GP8403_GetHALAddress(device),
                             register_address, I2C_MEMADD_SIZE_8BIT,
                             payload, payload_size, device->timeout_ms);
}

HAL_StatusTypeDef GP8403_SetMillivolts(const GP8403 *device,
                                       GP8403_Channel channel,
                                       uint16_t millivolts)
{
    HAL_StatusTypeDef status;
    uint16_t code;

    status = GP8403_MillivoltsToCode(device, millivolts, &code);
    if (status != HAL_OK)
    {
        return status;
    }

    return GP8403_SetRaw(device, channel, code);
}

HAL_StatusTypeDef GP8403_SetRawDMA(GP8403 *device, GP8403_Channel channel,
                                   uint16_t code)
{
    HAL_StatusTypeDef status;
    uint16_t payload_size;

    if (!GP8403_IsValidDevice(device))
    {
        return HAL_ERROR;
    }

    if (device->dma_busy != 0U)
    {
        return HAL_BUSY;
    }

    status = GP8403_PrepareRawTransfer(channel, code,
                                       &device->dma_buffer[0],
                                       &device->dma_buffer[1], &payload_size);
    if (status != HAL_OK)
    {
        return status;
    }

    device->dma_busy = 1U;
    device->dma_status = HAL_BUSY;
    device->i2c_error = HAL_I2C_ERROR_NONE;

    /*
     * Send the register address and payload as one DMA frame. Unlike the F4
     * HAL memory-DMA API, this path does not poll the register-address phase
     * before returning to the caller.
     */
    status = HAL_I2C_Master_Transmit_DMA(device->hi2c,
                                         GP8403_GetHALAddress(device),
                                         device->dma_buffer,
                                         (uint16_t)(payload_size + 1U));
    if (status != HAL_OK)
    {
        device->dma_busy = 0U;
        device->dma_status = status;
        device->i2c_error = HAL_I2C_GetError(device->hi2c);
    }

    return status;
}

HAL_StatusTypeDef GP8403_SetMillivoltsDMA(GP8403 *device,
                                          GP8403_Channel channel,
                                          uint16_t millivolts)
{
    HAL_StatusTypeDef status;
    uint16_t code;

    status = GP8403_MillivoltsToCode(device, millivolts, &code);
    if (status != HAL_OK)
    {
        return status;
    }

    return GP8403_SetRawDMA(device, channel, code);
}

uint8_t GP8403_IsDMABusy(const GP8403 *device)
{
    if (device == NULL)
    {
        return 0U;
    }

    return device->dma_busy;
}

HAL_StatusTypeDef GP8403_GetDMAStatus(const GP8403 *device)
{
    if (device == NULL)
    {
        return HAL_ERROR;
    }

    return device->dma_status;
}

uint32_t GP8403_GetI2CError(const GP8403 *device)
{
    if (device == NULL)
    {
        return HAL_I2C_ERROR_NONE;
    }

    return device->i2c_error;
}

void GP8403_HandleI2CMasterTxComplete(GP8403 *device,
                                       I2C_HandleTypeDef *hi2c)
{
    if ((device == NULL) || (hi2c != device->hi2c) ||
        (device->dma_busy == 0U))
    {
        return;
    }

    device->i2c_error = HAL_I2C_ERROR_NONE;
    device->dma_status = HAL_OK;
    device->dma_busy = 0U;
}

void GP8403_HandleI2CError(GP8403 *device, I2C_HandleTypeDef *hi2c)
{
    if ((device == NULL) || (hi2c != device->hi2c) ||
        (device->dma_busy == 0U))
    {
        return;
    }

    device->i2c_error = HAL_I2C_GetError(hi2c);
    device->dma_status = HAL_ERROR;
    device->dma_busy = 0U;
}
