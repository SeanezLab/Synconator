/*
 * trajectory_manager.c
 *
 *  Created on: Mar 4, 2026
 *      Author: k.rodolfo
 */

#include "stim_command_manager.h"
#include "data_tx_arrays.h"
#include "tim.h"
#include <string.h>
#include <math.h>


void stim_command_init(stimCommandQueue* stim_queue)
{
	memset(stim_queue->ampArray, 0, sizeof(stim_queue->ampArray));
	memset(stim_queue->periodArray, 0, sizeof(stim_queue->periodArray));
	stim_queue->totalTime = 0;
	stim_queue->remainingSpace = MAX_CMD_LENGTH;
	stim_queue->tail = 0;
	stim_queue->busy_flag = 0; // Initialize not busy...
	stim_queue->stop_flag = 0; // but with the stop command up
}

void pushCommand(stimCommandQueue* stim_queue, uint16_t* amp, uint32_t* period, uint16_t cmd_size)
{
	if (cmd_size > stim_queue->remainingSpace)
	{
		cmd_size = stim_queue->remainingSpace;
	}

	uint16_t start_index = stim_queue->tail;
	for (uint16_t i = 0; i < cmd_size; ++i)
	{
		stim_queue->ampArray[start_index+i] = amp[i];
		stim_queue->periodArray[start_index+i] = period[i];
	}
	stim_queue->tail += cmd_size;
	stim_queue->remainingSpace = MAX_CMD_LENGTH - stim_queue->tail;
}
void popCommand(stimCommandQueue* stim_queue, uint16_t* amp_in, uint32_t* time_in)
{
	*amp_in = stim_queue->ampArray[0];
	*time_in = stim_queue->periodArray[0];
	// Shift the data
	memmove(stim_queue->ampArray, &(stim_queue->ampArray[1]), sizeof(uint16_t)*(MAX_CMD_LENGTH-1));
	memmove(stim_queue->periodArray, &(stim_queue->periodArray[1]), sizeof(uint32_t)*(MAX_CMD_LENGTH-1));
	// Set the last index to 0
	stim_queue->ampArray[MAX_CMD_LENGTH] = 0;
	stim_queue->periodArray[MAX_CMD_LENGTH] = 0;
	// Update the location of the tail
	stim_queue->tail -= 1;
	stim_queue->remainingSpace = MAX_CMD_LENGTH - stim_queue->tail;
}

void sendPulse(stimCommandQueue* stim_queue, uint32_t pulse_width, uint32_t pulse_period)
{
	__HAL_TIM_SET_AUTORELOAD(&htim2, pulse_period);
	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_AUTORELOAD(&htim16, pulse_width);
	__HAL_TIM_SET_COUNTER(&htim16, 0);
	HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, GPIO_PIN_SET);
	HAL_TIM_Base_Start_IT(&htim2); // Stimulation Period (1mhz counter)
	HAL_TIM_Base_Start_IT(&htim16); // Pulse Width Period (1mhz counter)

}

void schedulePulsePeriod(uint32_t time_us)
{
	__HAL_TIM_SET_AUTORELOAD(&htim2, time_us);
	__HAL_TIM_SET_COUNTER(&htim2, 0);
//	HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, GPIO_PIN_SET);
	HAL_TIM_Base_Start_IT(&htim2); // Stimulation Period (1mhz counter)
}

void completePulsePeriod(stimCommandQueue* stim_queue)
{
	HAL_TIM_Base_Stop_IT(&htim2);
	stim_queue->busy_flag = 0;

}

void schedulePulseWidth(uint32_t time_us)
{
	__HAL_TIM_SET_AUTORELOAD(&htim16, time_us);
	__HAL_TIM_SET_COUNTER(&htim16, 0);
	HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, GPIO_PIN_SET);
	HAL_TIM_Base_Start_IT(&htim16); // Pulse Width Period (1mhz counter)
}

void completePulseWidth()
{
	HAL_TIM_Base_Stop_IT(&htim16);
	HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, GPIO_PIN_RESET);

}

