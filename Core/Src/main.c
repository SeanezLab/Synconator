/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
#include "main.h"
#include "dma.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "structs.h"
#include "test_signals.h"
#include "crc.h"
#include <stdint.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// Buffer to hold rx data
char buffer[50] = {0};
uint8_t msg_rdy = 0;
int done = 0;
int idx = 0;
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
stimCommandQueue stim_queue;
rdg_buf_struct* dma_reader;
uint8_t rx_dma_buffer[RX_DMA_SIZE];
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  dma_reader = rdg_buf_init(RX_DMA_SIZE);
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART2_UART_Init();
  MX_TIM2_Init();
  MX_TIM6_Init();
  MX_TIM7_Init();
  MX_TIM16_Init();
  /* USER CODE BEGIN 2 */
  // starting timers

  HAL_TIM_Base_Start_IT(&htim6); // Communications loop (100hz)
  HAL_TIM_Base_Start_IT(&htim7); // Stimulation loop (500hz)



  // enabling receiver timeout
  huart2.Instance->RTOR = 1000;  // timeout value
  SET_BIT(huart2.Instance->CR2, USART_CR2_RTOEN);
  SET_BIT(huart2.Instance->CR1, USART_CR1_RTOIE);

  HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buffer, RX_DMA_SIZE);
  // Turn off DMA half-transfer + transfer-complete interupts
  __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
  __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_TC);

  // Try receiver timeout interrupt on huart2
  __HAL_UART_ENABLE_IT(&huart2, UART_IT_RTO);

  // Init our command structures
  stim_command_init(&stim_queue);

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {

	  if (com_loop_flag == 1)
	  {
		  run_com_loop();
	  }
	  if (stim_loop_flag == 1)
	  {
		  run_stim_loop();
	  }


    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void run_com_loop(void)
{
//	Send our current state
	increment_frame_counter();
	memcpy(frame, &frame_counter, (size_t)sizeof(frame_counter));
	compile_data_sources(5,
			  status, queue_len, queue_time, debug, frame);
	crc_uart_send_data(compiled_payload, &huart2);

//	Check our inbox for any commands
	if (got_msg == true)
	{
	  uint32_t period_test[4] = {10.0f, 10.0f, 10.0f, 10.0f};
	  uint16_t amp_test[4] = {3, 3, 3, 3};
	  uint16_t cmd_size = 4;
	  pushCommand(&stim_queue, amp_test, period_test, cmd_size);

	  dma_to_rdg_buf(dma_reader, rx_dma_buffer, msg_size);
	  crc_uart_rcv_data(dma_reader, msg_size);
	  flush_buffer(dma_reader);
	  got_msg = false;
	}
	com_loop_flag = 0;
}

void run_stim_loop(void)
{
	// Check if the stim command queue is still busy
	if (stim_queue.busy_flag == 0)
	{
		uint16_t popped_amp = 0;
		uint32_t popped_period = 4500;
		uint32_t pulse_width = 1; //10 us
//		popCommand(&stim_queue, &popped_amp, &popped_period);
		//send a stimulation pulse, then schedule a cooldown
		stim_queue.busy_flag = 1;
		sendPulse(&stim_queue, pulse_width, popped_period);

	}
	stim_loop_flag = 0;
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
