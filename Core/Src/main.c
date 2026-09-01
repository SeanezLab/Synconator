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
#include "i2c.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "structs.h"
#include "test_signals.h"
#include "crc.h"
#include "gp8403.h"
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
GP8403 dac;

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
  MX_TIM6_Init();
  MX_TIM7_Init();
  MX_TIM2_Init();
  MX_TIM15_Init();
  MX_I2C1_Init();
  /* USER CODE BEGIN 2 */
  // starting timers

     HAL_TIM_Base_Start_IT(&htim6); // Communications loop (100hz)
     HAL_TIM_Base_Start_IT(&htim7); // Stimulation loop (66667hz)



     // enabling receive to idle
     HAL_UARTEx_ReceiveToIdle_DMA(&huart2, rx_dma_buffer, RX_DMA_SIZE);
     // Turn off DMA half-transfer + transfer-complete interupts
     __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_HT);
     __HAL_DMA_DISABLE_IT(huart2.hdmarx, DMA_IT_TC);

     // Init our command structures
     stim_command_init(&stim_queue);

     // Init the DAC
//     if (GP8403_Init(&dac, &hi2c1, GP8403_DEFAULT_I2C_ADDRESS, GP8403_RANGE_10V) != HAL_OK)
//     {
//         Error_Handler();
//     }

     // Set channel 0 to 0.0 V
//     GP8403_SetMillivolts(&dac, GP8403_CHANNEL_0, 0);


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

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 10;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
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
	// Count remaining commands
	float queued_cmds = (float)(MAX_CMD_LENGTH - stim_queue.remainingSpace);
	memcpy(queue_len, &queued_cmds, sizeof(queued_cmds));


	compile_data_sources(5,
			  status, queue_len, queue_time, debug, frame);
	crc_uart_send_data(compiled_payload, &huart2);

//	Check our inbox for any commands
	if (got_msg == true)
	{
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
	//HAL_GPIO_WritePin(Timing_GPIO_Port, Timing_Pin, GPIO_PIN_SET); //This loop take 7.1 us to run w/out popping.
	if (stim_queue.busy_flag == 0 && stim_queue.tail != stim_queue.head)
	{
		static uint16_t popped_amp = 0;
		static uint32_t popped_period = 500;
		static uint32_t pulse_width = 10; //10 us
		uint8_t cmd_success = 1;
		cmd_success = popCommand(&stim_queue, &popped_amp, &popped_period);
		//send a stimulation pulse (Set reload to 4 when feeling brave)
		if (cmd_success == 1)
		{
			stim_queue.busy_flag = 1;
			sendPulse(&stim_queue, pulse_width, popped_period, popped_amp);
		}

	}
	stim_loop_flag = 0;
	//HAL_GPIO_WritePin(Timing_GPIO_Port, Timing_Pin, GPIO_PIN_RESET);
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
