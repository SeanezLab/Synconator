/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    usart.c
  * @brief   This file provides code for the configuration
  *          of the USART instances.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "usart.h"

/* USER CODE BEGIN 0 */
uint8_t rx_dma_buffer[RX_DMA_SIZE];
volatile uint8_t  huart3_tx_complete = 1;
static volatile bool huart3_rx_data_pending = false;
static volatile uint16_t huart3_rx_write_position = 0U;
static volatile bool huart3_rx_restart_requested = false;
/* USER CODE END 0 */

UART_HandleTypeDef huart3;
DMA_HandleTypeDef hdma_usart3_rx;
DMA_HandleTypeDef hdma_usart3_tx;

/* USART3 init function */

void MX_USART3_UART_Init(void)
{

  /* USER CODE BEGIN USART3_Init 0 */

  /* USER CODE END USART3_Init 0 */

  /* USER CODE BEGIN USART3_Init 1 */

  /* USER CODE END USART3_Init 1 */
  huart3.Instance = USART3;
  huart3.Init.BaudRate = 921600;
  huart3.Init.WordLength = UART_WORDLENGTH_8B;
  huart3.Init.StopBits = UART_STOPBITS_1;
  huart3.Init.Parity = UART_PARITY_NONE;
  huart3.Init.Mode = UART_MODE_TX_RX;
  huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart3.Init.OverSampling = UART_OVERSAMPLING_16;
  huart3.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart3.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart3) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART3_Init 2 */

  /* USER CODE END USART3_Init 2 */

}

void HAL_UART_MspInit(UART_HandleTypeDef* uartHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};
  if(uartHandle->Instance==USART3)
  {
  /* USER CODE BEGIN USART3_MspInit 0 */

  /* USER CODE END USART3_MspInit 0 */

  /** Initializes the peripherals clock
  */
    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART3;
    PeriphClkInit.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
    {
      Error_Handler();
    }

    /* USART3 clock enable */
    __HAL_RCC_USART3_CLK_ENABLE();

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    /**USART3 GPIO Configuration
    PB11     ------> USART3_RX
    PC10     ------> USART3_TX
    */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* USART3 DMA Init */
    /* USART3_RX Init */
    hdma_usart3_rx.Instance = DMA1_Channel3;
    hdma_usart3_rx.Init.Request = DMA_REQUEST_2;
    hdma_usart3_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_usart3_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart3_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart3_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart3_rx.Init.Mode = DMA_CIRCULAR;
    hdma_usart3_rx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart3_rx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmarx,hdma_usart3_rx);

    /* USART3_TX Init */
    hdma_usart3_tx.Instance = DMA1_Channel2;
    hdma_usart3_tx.Init.Request = DMA_REQUEST_2;
    hdma_usart3_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart3_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart3_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart3_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart3_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart3_tx.Init.Mode = DMA_NORMAL;
    hdma_usart3_tx.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_usart3_tx) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(uartHandle,hdmatx,hdma_usart3_tx);

    /* USART3 interrupt Init */
    HAL_NVIC_SetPriority(USART3_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);
  /* USER CODE BEGIN USART3_MspInit 1 */

  /* USER CODE END USART3_MspInit 1 */
  }
}

void HAL_UART_MspDeInit(UART_HandleTypeDef* uartHandle)
{

  if(uartHandle->Instance==USART3)
  {
  /* USER CODE BEGIN USART3_MspDeInit 0 */

  /* USER CODE END USART3_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_USART3_CLK_DISABLE();

    /**USART3 GPIO Configuration
    PB11     ------> USART3_RX
    PC10     ------> USART3_TX
    */
    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_11);

    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_10);

    /* USART3 DMA DeInit */
    HAL_DMA_DeInit(uartHandle->hdmarx);
    HAL_DMA_DeInit(uartHandle->hdmatx);

    /* USART3 interrupt Deinit */
    HAL_NVIC_DisableIRQ(USART3_IRQn);
  /* USER CODE BEGIN USART3_MspDeInit 1 */

  /* USER CODE END USART3_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
void huart3_try_send(uint8_t* msg, uint16_t msg_size)
{
	if (huart3_tx_complete == 1)
	{
		huart3_tx_complete = 0;
		HAL_StatusTypeDef st = HAL_UART_Transmit_DMA(&huart3, msg, msg_size);

		if (st != HAL_OK)
		{
			huart3_tx_complete = 1;
		}
	}
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == &huart3)
  {
	  huart3_tx_complete = 1;
  }

}

static void huart3_rx_reset_positions(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	huart3_rx_write_position = 0U;
	huart3_rx_data_pending = false;
	if (primask == 0U)
	{
		__enable_irq();
	}
}

HAL_StatusTypeDef huart3_rx_start(void)
{
	/* Clear state left by a framing/noise/overrun error before arming DMA. */
	__HAL_UART_CLEAR_FLAG(&huart3,
			UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF |
			UART_CLEAR_PEF | UART_CLEAR_IDLEF);
	__HAL_UART_SEND_REQ(&huart3, UART_RXDATA_FLUSH_REQUEST);
	huart3_rx_reset_positions();

	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	huart3_rx_restart_requested = false;
	if (primask == 0U)
	{
		__enable_irq();
	}

	HAL_StatusTypeDef status = HAL_UARTEx_ReceiveToIdle_DMA(
			&huart3, rx_dma_buffer, RX_DMA_SIZE);
	if (status == HAL_OK)
	{
		/* IDLE supplies the producer position. The DMA stays circular, so its
		 * half/full callbacks are unnecessary.
		 */
		__HAL_DMA_DISABLE_IT(huart3.hdmarx, DMA_IT_HT | DMA_IT_TC);
	}
	else
	{
		huart3_rx_restart_requested = true;
	}

	return status;
}

bool huart3_rx_recover(void)
{
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	bool restart_requested = huart3_rx_restart_requested;
	if (restart_requested)
	{
		huart3_rx_restart_requested = false;
	}
	if (primask == 0U)
	{
		__enable_irq();
	}

	if (!restart_requested)
	{
		return false;
	}

	/* Keep recovery out of the IRQ callback. This also handles a partially
	 * aborted DMA channel before starting a fresh circular reception.
	 */
	(void)HAL_UART_AbortReceive(&huart3);
	return huart3_rx_start() == HAL_OK;
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
	if (huart->Instance == USART3)
	{
		/* The HAL has already ended the failed DMA reception. Restart it from
		 * the main loop after the interrupt/abort path has returned.
		 */
		huart3_rx_restart_requested = true;
	}
}

bool huart3_rx_take_write_position(uint16_t* write_position)
{
	if (write_position == NULL)
	{
		return false;
	}

	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	bool data_pending = huart3_rx_data_pending;
	if (data_pending)
	{
		*write_position = huart3_rx_write_position;
		huart3_rx_data_pending = false;
	}
	if (primask == 0U)
	{
		__enable_irq();
	}

	return data_pending;
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
    if (huart->Instance != USART3)
    {
        return;
    }

	/* In circular Receive-to-Idle mode, size is the DMA producer position,
	 * not the number of bytes in this callback. Keeping the latest position
	 * allows the main loop to consume every byte even if callbacks coalesce.
	 */
	if (size <= RX_DMA_SIZE)
	{
		huart3_rx_write_position = size;
		huart3_rx_data_pending = true;
	}


//    // Required when DMA is configured in Normal mode.
//    if (HAL_UARTEx_ReceiveToIdle_DMA(huart, rx_dma_buffer, RX_DMA_SIZE) != HAL_OK)
//    {
//        Error_Handler();
//    }
//
//    __HAL_DMA_DISABLE_IT(huart->hdmarx, DMA_IT_HT);
}

void huart3_RTO_handler(void)
{
	if (__HAL_UART_GET_FLAG(&huart3, UART_FLAG_IDLE))
		{
			// Clear the timeout flag
			__HAL_UART_CLEAR_FLAG(&huart3, UART_FLAG_IDLE);

			uint16_t remaining = __HAL_DMA_GET_COUNTER(huart3.hdmarx);
			huart3_rx_write_position = RX_DMA_SIZE - remaining;
			huart3_rx_data_pending = true;

		}
}

/* USER CODE END 1 */
