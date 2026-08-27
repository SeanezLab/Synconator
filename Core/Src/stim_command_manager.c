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
#include <stdlib.h>

#define TIMING_OFFSET 104 //us

void stim_command_init(stimCommandQueue* stim_queue)
{
	memset(stim_queue->ampArray, 0, sizeof(stim_queue->ampArray));
	memset(stim_queue->periodArray, 0, sizeof(stim_queue->periodArray));
	stim_queue->totalTime = 0;
	stim_queue->remainingSpace = MAX_CMD_LENGTH;
	stim_queue->head = 0;
	stim_queue->tail = 0;
	stim_queue->busy_flag = 0; // Initialize not busy...
	stim_queue->stop_flag = 0; // but with the stop command up
	stim_queue->queue_lock = 0; // Locks the buffer when commands are being updated
}

uint8_t pushCommand(stimCommandQueue* stim_queue, uint16_t* amp, uint32_t* period, uint16_t cmd_size)
{
	// Acquire the lock, return early if the lock is already acquired
	uint8_t success_flag = 1;
	if (stim_queue->queue_lock == 1)
	{
		success_flag = 0;
		return success_flag; // Return 0 if
	}
	stim_queue->queue_lock  = 1;
	// Add into the buffer
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
	// Update the tail
	stim_queue->tail += cmd_size;
	if (stim_queue->tail >= MAX_CMD_LENGTH)
		{
			stim_queue->tail = stim_queue->tail % MAX_CMD_LENGTH;
		}
	// Update the remaining space
	updateRemainingSpace(stim_queue);
	// Release the lock
	stim_queue-> queue_lock = 0;
	// Return the success flag
	return success_flag;
}
uint8_t popCommand(stimCommandQueue* stim_queue, uint16_t* amp_in, uint32_t* time_in)
{
	// Acquire the lock, if already acquired, return early.
	uint8_t success_flag = 1;
	if (stim_queue->queue_lock == 1)
	{
		success_flag = 0;
		return success_flag; // Return 0 if
	}
	stim_queue->queue_lock  = 1;
	// Update the popped values from the head index
	*amp_in = stim_queue->ampArray[stim_queue->head];
	*time_in = stim_queue->periodArray[stim_queue->head];
	// Update the location of the head
	stim_queue->head += 1;
	if (stim_queue->head >= MAX_CMD_LENGTH)
	{
		stim_queue->head = stim_queue->head % MAX_CMD_LENGTH;
	}

	// Update the remainder
	updateRemainingSpace(stim_queue);
	// Release the lock
	stim_queue-> queue_lock = 0;
	// Return the status
	return success_flag;
}

void updateRemainingSpace(stimCommandQueue* stim_queue)
{
	if (stim_queue->tail < stim_queue->head)
	{
		stim_queue->remainingSpace = abs(stim_queue->tail - stim_queue->head);
	}
	else if (stim_queue->tail>=stim_queue->head)
	{
		uint16_t btwn_tail_head = stim_queue->tail - stim_queue->head;
		stim_queue->remainingSpace = MAX_CMD_LENGTH - btwn_tail_head;
	}
}

void sendPulse(stimCommandQueue* stim_queue, uint32_t pulse_width, uint32_t pulse_period)
{
//	HAL_GPIO_WritePin(Timing_GPIO_Port, Timing_Pin, GPIO_PIN_SET);
	__HAL_TIM_SET_AUTORELOAD(&htim2, pulse_period - TIMING_OFFSET);
	__HAL_TIM_SET_COUNTER(&htim2, 0);
	__HAL_TIM_SET_AUTORELOAD(&htim16, pulse_width);
	__HAL_TIM_SET_COUNTER(&htim16, 0);
	HAL_GPIO_WritePin(Trigger_GPIO_Port, Trigger_Pin, GPIO_PIN_SET);
//	HAL_GPIO_WritePin(Timing_GPIO_Port, Timing_Pin, GPIO_PIN_RESET);
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
//	HAL_GPIO_WritePin(Timing_GPIO_Port, Timing_Pin, GPIO_PIN_SET);
	HAL_TIM_Base_Stop_IT(&htim2);
	stim_queue->busy_flag = 0;
//	HAL_GPIO_WritePin(Timing_GPIO_Port, Timing_Pin, GPIO_PIN_RESET);

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

