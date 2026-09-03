/*
 * trajectory_manager.c
 *
 *  Created on: Mar 4, 2026
 *      Author: k.rodolfo
 */

#include "stim_command_manager.h"
#include "data_tx_arrays.h"
#include "tim.h"
#include "structs.h"
#include "dac.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdbool.h>


#define TIMING_OFFSET 7 //us

#define PULSES_PER_DMA_BLOCK  64U
#define EVENTS_PER_PULSE      2U
#define EVENT_CAPACITY        (PULSES_PER_DMA_BLOCK * EVENTS_PER_PULSE)

#define PULSE_WIDTH_US        10U
#define DAC_LEAD_US           5U
#define START_MARGIN_US       1000U

#define DMA_DONE_TIM2_CH1     (1U << 0)
#define DMA_DONE_TIM2_CH3     (1U << 1)
#define DMA_DONE_DAC_CH1      (1U << 2)
#define DMA_DONE_ALL          (DMA_DONE_TIM2_CH1 | \
                               DMA_DONE_TIM2_CH3 | \
                               DMA_DONE_DAC_CH1)

/*
 * One extra entry is used as a terminal DMA transfer.
 */
static uint32_t dac_event_ticks[EVENT_CAPACITY + 1U];
static uint32_t trigger_edge_ticks[EVENT_CAPACITY + 1U];
static uint16_t dac_codes[EVENT_CAPACITY + 1U];

static volatile uint8_t dma_done_mask = 0U;
static bool dma_block_active = false;
static bool timeline_valid = false;
static uint32_t next_rise_tick = 0U;



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


static uint16_t amplitudeToDacCode(uint16_t amplitude)
{
    if (amplitude > 4095U)
    {
        return 4095U;
    }

    return amplitude;
}

static uint16_t buildDmaBlock(stimCommandQueue* stim_queue)
{
	uint16_t event_count = 0;
	uint16_t pulse_count = 0;

	uint32_t now = __HAL_TIM_GET_COUNTER(&htim2);
	uint32_t earliest_rise = now + START_MARGIN_US; //what is this? Why 1 ms wait?

	// Start a new timeline if the next rise time is too close or has passed
	if (!timeline_valid || (int32_t)(next_rise_tick - earliest_rise) < 0)
	{
		timeline_valid = true;
	}

	while (pulse_count < PULSES_PER_DMA_BLOCK && stim_queue->tail != stim_queue->head)
	{
		uint16_t amplitude;
		uint32_t period;

		// Attempt to pop a command
		if (!popCommand(stim_queue, &amplitude, &period))
		{
			break;
		}

		// Prevent the next DAC amplitude from overlapping this pulse
		if (period < PULSE_WIDTH_US + DAC_LEAD_US)
		{
			continue;
		}

		uint32_t rise_tick = next_rise_tick;
		uint32_t fall_tick = rise_tick + PULSE_WIDTH_US;

		// Set the amplitude right before the trigger rises.
		dac_event_ticks[event_count] = rise_tick - DAC_LEAD_US;
		dac_codes[event_count] = amplitudeToDacCode(amplitude);

		// Digital rising edge
		trigger_edge_ticks[event_count] = rise_tick;

		event_count++;

		// Return DAC to zero when the trigger falls
		dac_event_ticks[event_count] = fall_tick;
		dac_codes[event_count] = 0;

		// Digital falling edge
		trigger_edge_ticks[event_count] = fall_tick;

		event_count++;
		pulse_count++;

		// Period is measured between rising edges. Unsigned addition handles TIM2 rollovers.
		next_rise_tick += period;
	}

	if (event_count == 0)
	{
		return 0;
	}

	// Each DMA gets one final transfer at the last event. Writing a compared time a tick behind the current
	// counter prevents another match until TIM2 wraps. The DAC dummy tranfer acks the final DAC DMA request
	// and leave zero preloaded

	dac_event_ticks[event_count] = dac_event_ticks[event_count - 1];
	trigger_edge_ticks[event_count] = trigger_edge_ticks[event_count - 1];
	dac_codes[event_count] = 0;

	return event_count;
}

static HAL_StatusTypeDef startDmaBlock(uint16_t event_count)
{
	dma_done_mask = 0;

	//Seed the first compare values. DMA cannot load until the first compare event occurs.

	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, dac_event_ticks[0]);
	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, trigger_edge_ticks[0]);

	// First DAC Value must be placed in the DHR (data holding register) before the first external trigger
	if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_codes[0]) != HAL_OK)
	{
		return HAL_ERROR;
	}

	// Clear all flags on timer 2
	__HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC3);

	// DMA Source begins at element 1, as element 0 was seeded manually. The final terminal entry means the
	// length is always an even count. The dac_codes are uin16_t because DAC_DMA is configured half-word.

	if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t*)&dac_codes[1], event_count, DAC_ALIGN_12B_R) != HAL_OK)
	{
		return HAL_ERROR;
	}

	// We start CH3 before CH1, because the DAC event occurs slightly earlier

	if (HAL_TIM_OC_Start_DMA(&htim2, TIM_CHANNEL_1, &dac_event_ticks[1], event_count) != HAL_OK)
	{
		return HAL_ERROR;
	}

	dma_block_active = true;

	return HAL_OK;

}

void servicePulseDma(stimCommandQueue *stim_queue)
{
    if (dma_block_active)
    {
        if (dma_done_mask != DMA_DONE_ALL)
        {
            return;
        }

        dma_block_active = false;
    }

    if (stim_queue->tail == stim_queue->head)
    {
        return;
    }

    uint16_t event_count = buildDmaBlock(stim_queue);

    if (event_count == 0)
    {
        return;
    }

    if (startDmaBlock(event_count) != HAL_OK)
    {
        Error_Handler();
    }
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim)
{
	if (htim->Instance != TIM2)
	{
		return;
	}

	if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
	{
		dma_done_mask |= DMA_DONE_TIM2_CH1;
	}
	else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3)
	{
		dma_done_mask |= DMA_DONE_TIM2_CH3;
	}
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef* hdac)
{
	if (hdac->Instance == DAC1)
	{
		dma_done_mask |= DMA_DONE_DAC_CH1;
	}
}


