/*
 * stim_command_manager.c
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
#include <stdbool.h>

#define EVENTS_PER_PULSE      2U
#define DMA_EVENTS_PER_HALF   64U
#define DMA_EVENT_COUNT       (2U * DMA_EVENTS_PER_HALF)

#define PULSE_WIDTH_US        10U
#define DAC_LEAD_US           5U
#define START_MARGIN_US       1000U

#define DMA_DONE_TIM2_CH1     (1U << 0)
#define DMA_DONE_TIM2_CH3     (1U << 1)
#define DMA_DONE_DAC_CH1      (1U << 2)
#define DMA_DONE_ALL          (DMA_DONE_TIM2_CH1 | \
                               DMA_DONE_TIM2_CH3 | \
                               DMA_DONE_DAC_CH1)

#if (DMA_EVENTS_PER_HALF % EVENTS_PER_PULSE) != 0U
#error "Each circular DMA half must contain complete pulses"
#endif

typedef enum
{
	NEXT_EVENT_RISE,
	NEXT_EVENT_FALL
} PulseEventPhase;

/*
 * These are DMA transfer values. Event zero is seeded directly into CCR1,
 * CCR3, and the DAC DHR before the circular streams are started.
 */
static uint32_t dac_dma_ticks[DMA_EVENT_COUNT];
static uint32_t trigger_dma_ticks[DMA_EVENT_COUNT];
static uint16_t dac_dma_codes[DMA_EVENT_COUNT];

/*
 * Half zero is released by half-transfer callbacks; half one is released by
 * transfer-complete callbacks. All three DMA streams must release a half
 * before software may rewrite it.
 */
static volatile uint8_t dma_half_done_mask[2] = {0U, 0U};

static bool pulse_dma_active = false;
static bool stop_planned = false;
static uint32_t stop_after_tick = 0U;

static PulseEventPhase next_event_phase = NEXT_EVENT_RISE;
static uint32_t next_rise_tick = 0U;
static uint32_t current_fall_tick = 0U;
static uint32_t last_dac_event_tick = 0U;
static uint32_t last_trigger_event_tick = 0U;

void stim_command_init(stimCommandQueue* stim_queue)
{
	memset(stim_queue->ampArray, 0, sizeof(stim_queue->ampArray));
	memset(stim_queue->periodArray, 0, sizeof(stim_queue->periodArray));
	stim_queue->totalTime = 0;
	stim_queue->remainingSpace = MAX_CMD_LENGTH;
	stim_queue->count = 0;
	stim_queue->head = 0;
	stim_queue->tail = 0;
	stim_queue->busy_flag = 0;
	stim_queue->stop_flag = 0;
	stim_queue->queue_lock = 0;
	stim_queue->stim_mode = 0;
	stim_queue->last_amp = 0;
	stim_queue->last_period = 0;
}


uint8_t changeStimMode(stimCommandQueue* stim_queue, uint8_t incoming_mode)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}
	stim_queue->stim_mode = incoming_mode;
	stim_queue->queue_lock = 0U;
	return 1U;
}

uint8_t pushCommand(stimCommandQueue* stim_queue, uint16_t* amp,
		uint32_t* period, uint16_t cmd_size)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}

	stim_queue->queue_lock = 1U;

	if (cmd_size > stim_queue->remainingSpace)
	{
		cmd_size = stim_queue->remainingSpace;
	}

	for (uint16_t i = 0U; i < cmd_size; i++)
	{
		uint16_t index = (stim_queue->tail + i) % MAX_CMD_LENGTH;
		stim_queue->ampArray[index] = amp[i];
		stim_queue->periodArray[index] = period[i];
	}

	stim_queue->tail =
			(stim_queue->tail + cmd_size) % MAX_CMD_LENGTH;
	stim_queue->count += cmd_size;
	stim_queue->remainingSpace = MAX_CMD_LENGTH - stim_queue->count;
	stim_queue->queue_lock = 0U;

	return 1U;
}

uint8_t popCommand(stimCommandQueue* stim_queue, uint16_t* amp_in,
		uint32_t* time_in)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}

	stim_queue->queue_lock = 1U;

	if (stim_queue->count == 0U)
	{
		stim_queue->queue_lock = 0U;
		return 0U;
	}

	if ((stim_queue->count == 1U) & (stim_queue->stim_mode == 1))
	{
		*amp_in = stim_queue->ampArray[stim_queue->head];
		*time_in = stim_queue->periodArray[stim_queue->head];
		stim_queue->queue_lock = 0U;
		return 1U;
	}


	*amp_in = stim_queue->ampArray[stim_queue->head];
	*time_in = stim_queue->periodArray[stim_queue->head];
	stim_queue->last_amp = *amp_in;
	stim_queue->last_period = *time_in;
	stim_queue->head = (stim_queue->head + 1U) % MAX_CMD_LENGTH;
	stim_queue->count--;
	stim_queue->remainingSpace = MAX_CMD_LENGTH - stim_queue->count;
	stim_queue->queue_lock = 0U;

	return 1U;
}

static uint16_t amplitudeToDacCode(uint16_t amplitude)
{
	if (amplitude > 4095U)
	{
		return 4095U;
	}

	return amplitude;
}

/*
 * Generate one synchronized DAC/trigger event. A rising event consumes one
 * command. Its falling event is generated on the next call without consuming
 * another command.
 */
static bool buildNextEvent(stimCommandQueue* stim_queue,
		uint32_t* dac_tick, uint32_t* trigger_tick, uint16_t* dac_code)
{
	if (next_event_phase == NEXT_EVENT_FALL)
	{
		*dac_tick = current_fall_tick;
		*trigger_tick = current_fall_tick;
		*dac_code = 0U;

		last_dac_event_tick = *dac_tick;
		last_trigger_event_tick = *trigger_tick;
		next_event_phase = NEXT_EVENT_RISE;

		return true;
	}

	/* Invalid commands are discarded. */
	while (stim_queue->count > 0U)
	{
		uint16_t amplitude;
		uint32_t period;

		if (!popCommand(stim_queue, &amplitude, &period))
		{
			return false;
		}

		if (period <= PULSE_WIDTH_US + DAC_LEAD_US)
		{
			continue;
		}

		uint32_t rise_tick = next_rise_tick;
		current_fall_tick = rise_tick + PULSE_WIDTH_US;

		*dac_tick = rise_tick - DAC_LEAD_US;
		*trigger_tick = rise_tick;
		*dac_code = amplitudeToDacCode(amplitude);

		last_dac_event_tick = *dac_tick;
		last_trigger_event_tick = *trigger_tick;
		next_event_phase = NEXT_EVENT_FALL;

		/* The period is measured from this rising edge to the next one. */
		next_rise_tick += period;

		return true;
	}

	return false;
}

/*
 * Fill a released half of all three DMA buffers. If the command queue runs
 * empty, insert a parked compare value after the final falling event. This
 * safely stalls the compare chain until servicePulseDma() stops the streams.
 */
static bool fillDmaRange(stimCommandQueue* stim_queue,
		uint16_t start_index, uint16_t length)
{
	uint16_t end_index = start_index + length;

	for (uint16_t i = start_index; i < end_index; i++)
	{
		if (!buildNextEvent(stim_queue, &dac_dma_ticks[i],
				&trigger_dma_ticks[i], &dac_dma_codes[i]))
		{
			uint32_t parked_dac_tick = last_dac_event_tick - 1U;
			uint32_t parked_trigger_tick =
					last_trigger_event_tick - 1U;

			for (uint16_t park = i; park < end_index; park++)
			{
				dac_dma_ticks[park] = parked_dac_tick;
				trigger_dma_ticks[park] = parked_trigger_tick;
				dac_dma_codes[park] = 0U;
			}

			stop_planned = true;
			stop_after_tick = last_trigger_event_tick + 1U;
			return false;
		}
	}

	return true;
}

static HAL_StatusTypeDef startPulseDma(stimCommandQueue* stim_queue)
{
	uint32_t first_dac_tick;
	uint32_t first_trigger_tick;
	uint16_t first_dac_code;

	next_rise_tick =
			__HAL_TIM_GET_COUNTER(&htim2) + START_MARGIN_US;
	next_event_phase = NEXT_EVENT_RISE;
	stop_planned = false;

	if (!buildNextEvent(stim_queue, &first_dac_tick,
			&first_trigger_tick, &first_dac_code))
	{
		return HAL_OK;
	}

	/* The circular buffers hold events 1..DMA_EVENT_COUNT. Event zero is
	 * written directly into the peripheral registers before DMA starts.
	 */
	(void)fillDmaRange(stim_queue, 0U, DMA_EVENT_COUNT);

	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, first_dac_tick);
	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, first_trigger_tick);

	if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1,
			DAC_ALIGN_12B_R, first_dac_code) != HAL_OK)
	{
		return HAL_ERROR;
	}

	__HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC3);
	dma_half_done_mask[0] = 0U;
	dma_half_done_mask[1] = 0U;

	if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
			(uint32_t*)dac_dma_codes, DMA_EVENT_COUNT,
			DAC_ALIGN_12B_R) != HAL_OK)
	{
		return HAL_ERROR;
	}

	/* Starting CH3 enables TIM2. The first event is START_MARGIN_US in the
	 * future, leaving ample time to start CH1 afterward.
	 */
	if (HAL_TIM_OC_Start_DMA(&htim2, TIM_CHANNEL_3,
			trigger_dma_ticks, DMA_EVENT_COUNT) != HAL_OK)
	{
		return HAL_ERROR;
	}

	if (HAL_TIM_OC_Start_DMA(&htim2, TIM_CHANNEL_1,
			dac_dma_ticks, DMA_EVENT_COUNT) != HAL_OK)
	{
		return HAL_ERROR;
	}

	pulse_dma_active = true;

	return HAL_OK;
}

static void stopPulseDma(void)
{
	(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_1);
	(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_3);

	/* The final real DAC event set the output to zero. Stop its circular DMA,
	 * then re-enable the DAC without DMA so it continues driving zero.
	 */
	(void)HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
	(void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1,
			DAC_ALIGN_12B_R, 0U);
	(void)HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);

	dma_half_done_mask[0] = 0U;
	dma_half_done_mask[1] = 0U;
	pulse_dma_active = false;
	stop_planned = false;
	next_event_phase = NEXT_EVENT_RISE;
}

static bool tickReached(uint32_t now, uint32_t deadline)
{
	return (int32_t)(now - deadline) >= 0;
}

void servicePulseDma(stimCommandQueue* stim_queue)
{
	if (!pulse_dma_active)
	{
		if (stim_queue->count > 0U &&
				startPulseDma(stim_queue) != HAL_OK)
		{
			Error_Handler();
		}
		return;
	}

	if (stop_planned)
	{
		if (tickReached(__HAL_TIM_GET_COUNTER(&htim2), stop_after_tick))
		{
			stopPulseDma();
		}
		return;
	}

	for (uint8_t half = 0U; half < 2U; half++)
	{
		if (dma_half_done_mask[half] == DMA_DONE_ALL)
		{
			dma_half_done_mask[half] = 0U;

			uint16_t start_index = half * DMA_EVENTS_PER_HALF;
			(void)fillDmaRange(stim_queue, start_index,
					DMA_EVENTS_PER_HALF);
		}
	}
}

static void markTimerDmaHalf(TIM_HandleTypeDef* htim, uint8_t half)
{
	if (htim->Instance != TIM2)
	{
		return;
	}

	if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)
	{
		dma_half_done_mask[half] |= DMA_DONE_TIM2_CH1;
	}
	else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3)
	{
		dma_half_done_mask[half] |= DMA_DONE_TIM2_CH3;
	}
}

void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef* htim)
{
	markTimerDmaHalf(htim, 0U);
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim)
{
	markTimerDmaHalf(htim, 1U);
}

void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef* hdac)
{
	if (hdac->Instance == DAC1)
	{
		dma_half_done_mask[0] |= DMA_DONE_DAC_CH1;
	}
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef* hdac)
{
	if (hdac->Instance == DAC1)
	{
		dma_half_done_mask[1] |= DMA_DONE_DAC_CH1;
	}
}
