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
#define DMA_DONE_OUTPUT_CC2   (1U << 3)
#define DMA_DONE_ALL          (DMA_DONE_TIM2_CH1 | \
                               DMA_DONE_TIM2_CH3 | \
                               DMA_DONE_DAC_CH1 | \
                               DMA_DONE_OUTPUT_CC2)

#define OUTPUT_GPIO_PORT      Sync_GPIO_Port
#define OUTPUT_ALL_PINS       (Sync_Pin | D188_1_Pin | D188_2_Pin | \
                               D188_3_Pin | D188_4_Pin | D188_5_Pin | \
                               D188_6_Pin | D188_7_Pin | D188_8_Pin)

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
 * CCR2, CCR3, and the DAC DHR before the circular streams are started.
 */
static uint32_t dac_dma_ticks[DMA_EVENT_COUNT];
static uint32_t output_dma_ticks[DMA_EVENT_COUNT];
static uint32_t trigger_dma_ticks[DMA_EVENT_COUNT];
static uint16_t dac_dma_codes[DMA_EVENT_COUNT];
static volatile uint32_t gpio_bsrr_values[DMA_EVENT_COUNT];
static volatile uint8_t stim_mode_values[DMA_EVENT_COUNT];
static volatile bool stim_mode_updates[DMA_EVENT_COUNT];

/*
 * Half zero is released by half-transfer callbacks; half one is released by
 * transfer-complete callbacks. The software-output half is released by the
 * CC2 interrupt after its final GPIO/mode event has been consumed. All four
 * event streams must release a half before software may rewrite it.
 */
static volatile uint8_t dma_half_done_mask[2] = {0U, 0U};

static bool pulse_dma_active = false;
static bool stop_planned = false;
static uint32_t stop_after_tick = 0U;
static stimCommandQueue* volatile active_stim_queue = NULL;

static PulseEventPhase next_event_phase = NEXT_EVENT_RISE;
static uint32_t next_rise_tick = 0U;
static uint32_t current_fall_tick = 0U;
static uint32_t last_dac_event_tick = 0U;
static uint32_t last_output_event_tick = 0U;
static uint32_t last_trigger_event_tick = 0U;

/* Event zero is not in the circular arrays, so its software values are kept here. */
static volatile uint32_t first_gpio_bsrr = 0U;
static volatile uint8_t first_stim_mode = 0U;
static volatile uint16_t output_event_index = 0U;
static volatile bool first_output_event_pending = false;

static const uint16_t gpio_pins[9] =
{
	Sync_Pin,
	D188_1_Pin,
	D188_2_Pin,
	D188_3_Pin,
	D188_4_Pin,
	D188_5_Pin,
	D188_6_Pin,
	D188_7_Pin,
	D188_8_Pin
};

/* Bit zero selects Sync; bits one through eight select D188_1 through D188_8. */
static uint32_t gpioMaskToBsrr(uint16_t gpio_mask)
{
	uint16_t pins_to_set = 0U;

	for (uint8_t channel = 0U; channel < 9U; channel++)
	{
		if ((gpio_mask & (1U << channel)) != 0U)
		{
			pins_to_set |= gpio_pins[channel];
		}
	}

	uint16_t pins_to_reset =
			(uint16_t)(OUTPUT_ALL_PINS & (uint16_t)~pins_to_set);

	return (uint32_t)pins_to_set | ((uint32_t)pins_to_reset << 16U);
}

void stim_command_init(stimCommandQueue* stim_queue)
{
	memset(stim_queue->modeArray, 0, sizeof(stim_queue->modeArray));
	memset(stim_queue->gpioArray, 0, sizeof(stim_queue->gpioArray));
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
	stim_queue->last_mode = 0;
	stim_queue->last_gpio = 0;
	stim_queue->last_amp = 0;
	stim_queue->last_period = 0;
}

uint8_t getLastMode(stimCommandQueue* stim_queue, uint8_t* mode_in)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}
	*mode_in = stim_queue->last_mode;
	return 1U;
}

uint8_t getLastGpio(stimCommandQueue* stim_queue, uint16_t* gpio_in)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}
	*gpio_in = stim_queue->last_gpio;
	return 1U;
}

uint8_t getLastAmp(stimCommandQueue* stim_queue, uint16_t* amp_in)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}
	*amp_in = stim_queue->last_amp;
	return 1U;
}

uint8_t getLastPeriod(stimCommandQueue* stim_queue, uint32_t* period_in)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}
	*period_in = stim_queue->last_period;
	return 1U;
}

uint8_t pushCommand(stimCommandQueue* stim_queue, uint8_t* mode, uint16_t* gpio, uint16_t* amp,
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
		stim_queue->modeArray[index] = mode[i];
		stim_queue->gpioArray[index] = gpio[i];
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

uint8_t popCommand(stimCommandQueue* stim_queue, uint8_t* mode_in, uint16_t* gpio_in, uint16_t* amp_in,
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

	if ((stim_queue->count == 1U) &&
			(stim_queue->modeArray[stim_queue->head] == 1U))
	{
		*mode_in = stim_queue->modeArray[stim_queue->head];
		*gpio_in = stim_queue->gpioArray[stim_queue->head];
		*amp_in = stim_queue->ampArray[stim_queue->head];
		*time_in = stim_queue->periodArray[stim_queue->head];
		stim_queue->last_mode = *mode_in;
		stim_queue->last_gpio = *gpio_in;
		stim_queue->last_amp = *amp_in;
		stim_queue->last_period = *time_in;
		stim_queue->queue_lock = 0U;
		return 1U;
	}


	*mode_in = stim_queue->modeArray[stim_queue->head];
	*gpio_in = stim_queue->gpioArray[stim_queue->head];
	*amp_in = stim_queue->ampArray[stim_queue->head];
	*time_in = stim_queue->periodArray[stim_queue->head];
	stim_queue->last_mode = *mode_in;
	stim_queue->last_gpio = *gpio_in;
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
 * Generate one synchronized DAC/GPIO/mode/trigger event. A rising event
 * consumes one command. Its falling event is generated on the next call
 * without consuming another command.
 */
static bool buildNextEvent(stimCommandQueue* stim_queue,
		uint32_t* dac_tick, uint32_t* output_tick, uint32_t* trigger_tick,
		uint16_t* dac_code, uint32_t* gpio_bsrr,
		uint8_t* stim_mode, bool* update_stim_mode)
{
	if (next_event_phase == NEXT_EVENT_FALL)
	{
		*dac_tick = current_fall_tick;
		*output_tick = current_fall_tick;
		*trigger_tick = current_fall_tick;
		*dac_code = 0U;
		/* GPIO modes persist between pulses. A zero BSRR write is a no-op. */
		*gpio_bsrr = 0U;
		*stim_mode = 0U;
		*update_stim_mode = false;

		last_dac_event_tick = *dac_tick;
		last_output_event_tick = *output_tick;
		last_trigger_event_tick = *trigger_tick;
		next_event_phase = NEXT_EVENT_RISE;

		return true;
	}

	/* Invalid commands are discarded. */
	while (stim_queue->count > 0U)
	{
		uint8_t mode;
		uint16_t gpio;
		uint16_t amplitude;
		uint32_t period;

		if (!popCommand(stim_queue, &mode, &gpio, &amplitude, &period))
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
		*output_tick = rise_tick;
		*trigger_tick = rise_tick;
		*dac_code = amplitudeToDacCode(amplitude);
		*gpio_bsrr = gpioMaskToBsrr(gpio);
		*stim_mode = mode;
		*update_stim_mode = true;

		last_dac_event_tick = *dac_tick;
		last_output_event_tick = *output_tick;
		last_trigger_event_tick = *trigger_tick;
		next_event_phase = NEXT_EVENT_FALL;

		/* The period is measured from this rising edge to the next one. */
		next_rise_tick += period;

		return true;
	}

	return false;
}

/*
 * Fill a released half of all four event buffers. If the command queue runs
 * empty, insert a parked compare value after the final falling event. This
 * safely stalls the compare chain until servicePulseDma() stops the streams.
 */
static bool fillDmaRange(stimCommandQueue* stim_queue,
		uint16_t start_index, uint16_t length)
{
	uint16_t end_index = start_index + length;

	for (uint16_t i = start_index; i < end_index; i++)
	{
		uint32_t gpio_bsrr;
		uint8_t stim_mode;
		bool update_stim_mode;

		if (!buildNextEvent(stim_queue, &dac_dma_ticks[i],
				&output_dma_ticks[i], &trigger_dma_ticks[i],
				&dac_dma_codes[i], &gpio_bsrr,
				&stim_mode, &update_stim_mode))
		{
			uint32_t parked_dac_tick = last_dac_event_tick - 1U;
			uint32_t parked_output_tick = last_output_event_tick - 1U;
			uint32_t parked_trigger_tick =
					last_trigger_event_tick - 1U;

			for (uint16_t park = i; park < end_index; park++)
			{
				dac_dma_ticks[park] = parked_dac_tick;
				output_dma_ticks[park] = parked_output_tick;
				trigger_dma_ticks[park] = parked_trigger_tick;
				dac_dma_codes[park] = 0U;
				gpio_bsrr_values[park] = 0U;
				stim_mode_values[park] = 0U;
				stim_mode_updates[park] = false;
			}

			stop_planned = true;
			stop_after_tick = last_trigger_event_tick + 1U;
			return false;
		}

		gpio_bsrr_values[i] = gpio_bsrr;
		stim_mode_values[i] = stim_mode;
		stim_mode_updates[i] = update_stim_mode;
	}

	return true;
}

static HAL_StatusTypeDef startPulseDma(stimCommandQueue* stim_queue)
{
	uint32_t first_dac_tick;
	uint32_t first_output_tick;
	uint32_t first_trigger_tick;
	uint16_t first_dac_code;
	uint32_t first_gpio_value;
	uint8_t first_mode;
	bool update_first_mode;

	next_rise_tick =
			__HAL_TIM_GET_COUNTER(&htim2) + START_MARGIN_US;
	next_event_phase = NEXT_EVENT_RISE;
	stop_planned = false;

	if (!buildNextEvent(stim_queue, &first_dac_tick, &first_output_tick,
			&first_trigger_tick, &first_dac_code, &first_gpio_value,
			&first_mode, &update_first_mode))
	{
		return HAL_OK;
	}
	if (!update_first_mode)
	{
		return HAL_ERROR;
	}

	/* The circular buffers hold events 1..DMA_EVENT_COUNT. Event zero is
	 * written directly into the peripheral registers before DMA starts.
	 */
	(void)fillDmaRange(stim_queue, 0U, DMA_EVENT_COUNT);

	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, first_dac_tick);
	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, first_output_tick);
	__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, first_trigger_tick);
	first_gpio_bsrr = first_gpio_value;
	first_stim_mode = first_mode;
	first_output_event_pending = true;
	output_event_index = 0U;
	active_stim_queue = stim_queue;

	if (HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1,
			DAC_ALIGN_12B_R, first_dac_code) != HAL_OK)
	{
		return HAL_ERROR;
	}

	__HAL_TIM_CLEAR_FLAG(&htim2,
			TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC3);
	dma_half_done_mask[0] = 0U;
	dma_half_done_mask[1] = 0U;

	if (HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
			(uint32_t*)dac_dma_codes, DMA_EVENT_COUNT,
			DAC_ALIGN_12B_R) != HAL_OK)
	{
		return HAL_ERROR;
	}

	/* Starting CH3 enables TIM2. The first event is START_MARGIN_US in the
	 * future, leaving ample time to start the remaining channels afterward.
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

	if (HAL_TIM_OC_Start_DMA(&htim2, TIM_CHANNEL_2,
			output_dma_ticks, DMA_EVENT_COUNT) != HAL_OK)
	{
		return HAL_ERROR;
	}
	__HAL_DMA_DISABLE_IT(htim2.hdma[TIM_DMA_ID_CC2], DMA_IT_HT | DMA_IT_TC);

	/* CC2 drives both DMA and an interrupt. DMA advances CCR2; the interrupt
	 * applies the GPIO and mode values associated with the compare that just
	 * occurred. DMA half/full interrupts are unused because the output consumer
	 * determines when each half is safe to refill.
	 */
	__HAL_TIM_ENABLE_IT(&htim2, TIM_IT_CC2);

	pulse_dma_active = true;

	return HAL_OK;
}

static void stopPulseDma(void)
{
	__HAL_TIM_DISABLE_IT(&htim2, TIM_IT_CC2);
	(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_2);
	(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_1);
	(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_3);
	__HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC2);

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
	first_output_event_pending = false;
	output_event_index = 0U;
	active_stim_queue = NULL;
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

void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef* htim)
{
	if (htim->Instance != TIM2 ||
			htim->Channel != HAL_TIM_ACTIVE_CHANNEL_2)
	{
		return;
	}

	if (first_output_event_pending)
	{
		OUTPUT_GPIO_PORT->BSRR = first_gpio_bsrr;
		if (active_stim_queue != NULL)
		{
			active_stim_queue->stim_mode = first_stim_mode;
		}
		first_output_event_pending = false;
		return;
	}

	OUTPUT_GPIO_PORT->BSRR = gpio_bsrr_values[output_event_index];
	if (stim_mode_updates[output_event_index] &&
			active_stim_queue != NULL)
	{
		active_stim_queue->stim_mode = stim_mode_values[output_event_index];
	}
	output_event_index++;

	/* DMA loads the next CCR2 value at the current compare. Waiting until the
	 * interrupt consumes the final output entry prevents an early buffer refill.
	 */
	if (output_event_index == DMA_EVENTS_PER_HALF)
	{
		dma_half_done_mask[0] |= DMA_DONE_OUTPUT_CC2;
	}
	else if (output_event_index == DMA_EVENT_COUNT)
	{
		output_event_index = 0U;
		dma_half_done_mask[1] |= DMA_DONE_OUTPUT_CC2;
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
