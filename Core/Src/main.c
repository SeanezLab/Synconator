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
#include "circular_reading_buffer.h"
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
uint8_t rxChar;
volatile int mailFlag = 0;
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
  /* USER CODE BEGIN 2 */
  // starting timers
  HAL_TIM_Base_Start_IT(&htim2);
  HAL_UART_Receive_IT(&huart2, &rxChar, 1);

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

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
	  if (got_msg == true)
	  {
		  dma_to_rdg_buf(dma_reader, rx_dma_buffer, msg_size);
		  HAL_UART_Transmit(&huart2, dma_reader->buffer, (size_t)msg_size, 1000);
		  flush_buffer(dma_reader);
		  got_msg = false;
	  }


	  // initial testing with Rod
//	  HAL_Delay(19);
//	  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
//	  HAL_Delay(1);
//	  HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin);
//
//	  if (msg_rdy == 1) //msg ready will be set to 1 in your interrupt.
	  {
		  //Handle the message logic. You already have this.
	  }

//	  char msg[] = "hello world. ";
//	  HAL_UART_Transmit(&huart2, msg, sizeof(msg), 1000);




	  // communication testing on my own with one character
//	  char receivedChar;
//
//	  char waitingMsg[] = "Waiting for character...\r\n";
//	  HAL_UART_Transmit(&huart2, (uint8_t*)waitingMsg, sizeof(waitingMsg), 1000);
//
//	  HAL_UART_Receive(&huart2, (uint8_t*)&receivedChar, 1, HAL_MAX_DELAY);
//
//	  char gotMsg[] = "Got: ";
//	  HAL_UART_Transmit(&huart2, (uint8_t*)gotMsg, sizeof(gotMsg), 1000);
//	  HAL_UART_Transmit(&huart2, (uint8_t*)&receivedChar, 1, 1000);
//
//	  char newline[] = "\r\n";
//	  HAL_UART_Transmit(&huart2, (uint8_t*)newline, sizeof(newline), 1000);



	  // communication testing on my own with strings
//	  char receivedChar =" ";
//
//	  char waitingMsg[] = "Waiting for command...\r\n";
//	  HAL_UART_Transmit(&huart2, (uint8_t*)waitingMsg, sizeof(waitingMsg), 1000);
//
//	  uint16_t idx = 0;
//	  int done = 0;
//
//	  while(done == 0)
//	  {
//		  HAL_UART_Receive(&huart2, (uint8_t*)&receivedChar, 1, HAL_MAX_DELAY);
//		  if (receivedChar == '\r')
//		  {
//			  buffer[idx] = '\0';
//			  done = 1;
//		  }
//		  else
//		  {
//			  buffer[idx] = receivedChar;
//			  idx++;
//			  if (idx == sizeof(buffer))
//			  {
//				  break;
//			  }
//		  }
//
//	  }
//
//	 char gotMsg[] = "Got: ";
//	 HAL_UART_Transmit(&huart2, (uint8_t*)gotMsg, sizeof(gotMsg), 1000);
//	 HAL_UART_Transmit(&huart2, (uint8_t*)buffer, (uint16_t)idx, 1000);
//	 HAL_UART_Transmit(&huart2, (uint8_t*)"\r\n", 2, 1000);





//	  // Communication testing with clock and interrupt on my own
//	  if (mailFlag == 1)
//	  {
//		  char msg = rxChar;
//		  buffer[idx] = rxChar;
//		  idx++;
//		  if (msg == '\r')
//		  {
//			  HAL_UART_Transmit(&huart2, (uint8_t*)buffer, (size_t)idx, 1000);
//			  idx = 0;
//
//		  }
//
//		  mailFlag = 0;
//	  }
//
//	  HAL_GPIO_TogglePin(debug_pin_GPIO_Port, debug_pin_Pin);
//	  HAL_Delay(19);


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
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM2)
	{
		;//HAL_GPIO_TogglePin(debug_pin_GPIO_Port, debug_pin_Pin);
	}
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{

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
