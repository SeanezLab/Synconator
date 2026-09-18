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

#define WATCHDOG_COUNTER_MAX 12000U
#define EDGES_PER_PULSE        2U
#define PULSES_PER_DMA_HALF    5U
#define EDGE_EVENTS_PER_HALF   (EDGES_PER_PULSE * PULSES_PER_DMA_HALF)
#define EDGE_EVENT_COUNT       (2U * EDGE_EVENTS_PER_HALF)
#define DAC_EVENTS_PER_HALF    PULSES_PER_DMA_HALF
#define DAC_EVENT_COUNT        (2U * DAC_EVENTS_PER_HALF)

#define PULSE_WIDTH_US        20U
#define DAC_LEAD_US           1000U
#define START_MARGIN_US       2000U
#define LIVE_REARM_MARGIN_US  100U
#define LIVE_COMPARE_MARGIN_US 5U

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
#define D188_COMMAND_MASK     0x01FEU

typedef enum
{
	NEXT_EVENT_RISE,
	NEXT_EVENT_FALL
} PulseEventPhase;

/*
 * These are DMA transfer values. Event zero is seeded directly into CCR1,
 * CCR2, CCR3, and the DAC DHR before the circular streams are started.
 */
static uint32_t dac_dma_ticks[DAC_EVENT_COUNT];
static uint16_t dac_dma_codes[DAC_EVENT_COUNT];
static uint32_t output_dma_ticks[EDGE_EVENT_COUNT];
static uint32_t trigger_dma_ticks[EDGE_EVENT_COUNT];
static volatile uint32_t gpio_bsrr_values[EDGE_EVENT_COUNT];
static volatile uint8_t stim_mode_values[EDGE_EVENT_COUNT];
static volatile bool stim_mode_updates[EDGE_EVENT_COUNT];
static volatile bool held_command_events[EDGE_EVENT_COUNT];

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
static volatile bool held_command_active = false;
static volatile bool dma_flush_requested = false;
static volatile bool dma_flush_ready = false;
static volatile bool replacement_flush_requested = false;
static volatile bool replacement_rise_tick_valid = false;
static volatile uint32_t replacement_rise_tick = 0U;

static PulseEventPhase next_event_phase = NEXT_EVENT_RISE;
static uint32_t next_rise_tick = 0U;
static uint32_t current_fall_tick = 0U;
static uint32_t last_dac_event_tick = 0U;
static uint32_t last_output_event_tick = 0U;
static uint32_t last_trigger_event_tick = 0U;
static uint32_t earliest_dac_event_tick = 0U;
static uint16_t scheduled_dac_code = 0U;

/* Event zero is not in the circular arrays, so its software values are kept here. */
static volatile uint32_t first_gpio_bsrr = 0U;
static volatile uint8_t first_stim_mode = 0U;
static volatile bool first_command_held = false;
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

/* Return the zero-based D188 channel when exactly one D188 bit is selected.
 * Bit zero is Sync and is intentionally ignored.
 */
static bool singleD188Channel(uint16_t gpio_mask, uint8_t* channel)
{
	uint16_t d188_mask = (gpio_mask & D188_COMMAND_MASK) >> 1U;
	if ((d188_mask == 0U) || ((d188_mask & (d188_mask - 1U)) != 0U))
	{
		return false;
	}

	for (uint8_t i = 0U; i < AMPLITUDE_OVERRIDE_CHANNELS; i++)
	{
		if ((d188_mask & (1U << i)) != 0U)
		{
			*channel = i;
			return true;
		}
	}

	return false;
}

static bool isRetainedContinuousCommand(const stimCommandQueue* stim_queue)
{
	return (stim_queue->count == 1U) &&
			(stim_queue->modeArray[stim_queue->head] == 1U);
}

static void resetQueuedCommands(stimCommandQueue* stim_queue)
{
	stim_queue->totalTime = 0.0f;
	stim_queue->remainingSpace = MAX_CMD_LENGTH;
	stim_queue->head = 0U;
	stim_queue->tail = 0U;
	stim_queue->count = 0U;
}

void stim_command_init(stimCommandQueue* stim_queue)
{
	memset(stim_queue->modeArray, 0, sizeof(stim_queue->modeArray));
	memset(stim_queue->gpioArray, 0, sizeof(stim_queue->gpioArray));
	memset(stim_queue->ampArray, 0, sizeof(stim_queue->ampArray));
	memset(stim_queue->periodArray, 0, sizeof(stim_queue->periodArray));
	memset(stim_queue->amplitude_override, 0,sizeof(stim_queue->amplitude_override));
	stim_queue->amplitude_override_active = 0U;
	resetQueuedCommands(stim_queue);
	stim_queue->busy_flag = 0;
	stim_queue->stop_flag = 0;
	stim_queue->queue_lock = 0;
	stim_queue->clear_flag = 0;
	stim_queue->stim_mode = 0;
	stim_queue->last_mode = 0;
	stim_queue->last_gpio = 0;
	stim_queue->last_amp = 0;
	stim_queue->last_period = 0;
	stim_queue-> watchdog_counter = 0;
}

void incrementWatchdogCounter(stimCommandQueue* stim_queue)
{
	stim_queue->watchdog_counter++;

	if (stim_queue->watchdog_counter >= WATCHDOG_COUNTER_MAX)
	{
		stim_queue->clear_flag = 1;
	}
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

uint8_t clearStimCommands(stimCommandQueue* stim_queue)
{
	if (stim_queue->queue_lock == 1U)
	{
		return 0U;
	}

	stim_queue->queue_lock = 1U;
	resetQueuedCommands(stim_queue);

	/* The current pulse is allowed to finish. Its falling CC2 event freezes
	 * TIM2, then servicePulseDma() aborts every stream without restarting.
	 */
	if (pulse_dma_active)
	{
		dma_flush_requested = true;
		replacement_flush_requested = false;
	}

	stim_queue->queue_lock = 0U;
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
	bool replace_held_command = (cmd_size > 0U) &&
			held_command_active && !dma_flush_requested &&
			isRetainedContinuousCommand(stim_queue);

	if (replace_held_command)
	{
		/* The retained command is already represented by the active DMA ring.
		 * Drop it so the replacement is the next command used after the flush.
		 */
		resetQueuedCommands(stim_queue);
	}

	if (cmd_size > stim_queue->remainingSpace)
	{
		cmd_size = stim_queue->remainingSpace;
	}

	for (uint16_t i = 0U; i < cmd_size; i++)
	{
		uint16_t index = (stim_queue->tail + i) % MAX_CMD_LENGTH;
		uint16_t command_amplitude = amp[i];
		uint8_t override_channel;
		if ((stim_queue->amplitude_override_active != 0U) &&
				singleD188Channel(gpio[i], &override_channel))
		{
			command_amplitude =
					stim_queue->amplitude_override[override_channel];
		}

		stim_queue->modeArray[index] = mode[i];
		stim_queue->gpioArray[index] = gpio[i];
		stim_queue->ampArray[index] = command_amplitude;
		stim_queue->periodArray[index] = period[i];
	}

	stim_queue->tail =
			(stim_queue->tail + cmd_size) % MAX_CMD_LENGTH;
	stim_queue->count += cmd_size;
	stim_queue->remainingSpace = MAX_CMD_LENGTH - stim_queue->count;
	if (replace_held_command && (cmd_size > 0U))
	{
		dma_flush_requested = true;
		replacement_flush_requested = true;
	}
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

	if (isRetainedContinuousCommand(stim_queue))
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

uint8_t disposeCommand(stimCommandQueue* stim_queue)
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

static bool tickBefore(uint32_t tick, uint32_t reference)
{
	return (int32_t)(tick - reference) < 0;
}

/* Keep the DAC update after the preceding pulse and before this pulse. When
 * the requested 1 ms lead is unavailable, use as much lead as remains without
 * moving the trigger timestamp.
 */
static uint32_t dacTickForRise(uint32_t rise_tick)
{
	uint32_t dac_tick = rise_tick - DAC_LEAD_US;
	uint32_t earliest_tick = earliest_dac_event_tick;
	uint32_t schedule_tick = __HAL_TIM_GET_COUNTER(&htim2) + 1U;

	if (tickBefore(earliest_tick, schedule_tick))
	{
		earliest_tick = schedule_tick;
	}
	if (tickBefore(dac_tick, earliest_tick))
	{
		dac_tick = earliest_tick;
	}
	if (tickBefore(rise_tick, dac_tick))
	{
		dac_tick = rise_tick;
	}

	return dac_tick;
}

/*
 * Generate one GPIO/mode/trigger edge. A rising edge consumes one command and
 * also produces one DAC event. Falling edges do not touch the DAC.
 */
static bool buildNextEvent(stimCommandQueue* stim_queue,
		uint32_t* dac_tick, uint32_t* output_tick, uint32_t* trigger_tick,
		uint16_t* dac_code, uint32_t* gpio_bsrr,
		uint8_t* stim_mode, bool* update_stim_mode,
		bool* held_command, bool* dac_event)
{
	if (next_event_phase == NEXT_EVENT_FALL)
	{
		*output_tick = current_fall_tick;
		*trigger_tick = current_fall_tick;
		/* GPIO modes persist between pulses. A zero BSRR write is a no-op. */
		*gpio_bsrr = 0U;
		*stim_mode = 0U;
		*update_stim_mode = false;
		*held_command = false;
		*dac_event = false;

		last_output_event_tick = *output_tick;
		last_trigger_event_tick = *trigger_tick;
		earliest_dac_event_tick = current_fall_tick;
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
		bool command_is_held = isRetainedContinuousCommand(stim_queue);

		if (!popCommand(stim_queue, &mode, &gpio, &amplitude, &period))
		{
			return false;
		}

		if (period <= PULSE_WIDTH_US)
		{
			/* A retained continuous command was not removed by popCommand(). */
			if (command_is_held)
			{
				disposeCommand(stim_queue);
			}
			continue;
		}

		uint32_t rise_tick = next_rise_tick;
		current_fall_tick = rise_tick + PULSE_WIDTH_US;

		*dac_tick = dacTickForRise(rise_tick);
		*output_tick = rise_tick;
		*trigger_tick = rise_tick;
		scheduled_dac_code = amplitudeToDacCode(amplitude);
		*dac_code = scheduled_dac_code;
		*gpio_bsrr = gpioMaskToBsrr(gpio);
		*stim_mode = mode;
		*update_stim_mode = true;
		*held_command = command_is_held;
		*dac_event = true;

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
 * Fill matching portions of the edge and DAC buffers. Each edge half contains
 * 32 rising and 32 falling events, while the matching DAC half contains only
 * the 32 rising-event updates. If the queue runs empty, park every stream on
 * an expired compare value until servicePulseDma() stops it.
 */
static bool fillDmaRange(stimCommandQueue* stim_queue,
		uint16_t edge_start, uint16_t edge_length,
		uint16_t dac_start, uint16_t dac_length)
{
	uint16_t edge_end = edge_start + edge_length;
	uint16_t dac_end = dac_start + dac_length;
	uint16_t dac_index = dac_start;

	for (uint16_t edge_index = edge_start;
			edge_index < edge_end; edge_index++)
	{
		uint32_t dac_tick;
		uint16_t dac_code;
		uint32_t gpio_bsrr;
		uint8_t stim_mode;
		bool update_stim_mode;
		bool held_command;
		bool dac_event;

		if (!buildNextEvent(stim_queue, &dac_tick,
				&output_dma_ticks[edge_index],
				&trigger_dma_ticks[edge_index], &dac_code, &gpio_bsrr,
				&stim_mode, &update_stim_mode, &held_command, &dac_event))
		{
			uint32_t parked_dac_tick = last_dac_event_tick - 1U;
			uint32_t parked_output_tick = last_output_event_tick - 1U;
			uint32_t parked_trigger_tick =
					last_trigger_event_tick - 1U;

			for (uint16_t park = edge_index; park < edge_end; park++)
			{
				output_dma_ticks[park] = parked_output_tick;
				trigger_dma_ticks[park] = parked_trigger_tick;
				gpio_bsrr_values[park] = 0U;
				stim_mode_values[park] = 0U;
				stim_mode_updates[park] = false;
				held_command_events[park] = false;
			}
			for (uint16_t park = dac_index; park < dac_end; park++)
			{
				dac_dma_ticks[park] = parked_dac_tick;
				dac_dma_codes[park] = 0U;
			}

			stop_planned = true;
			stop_after_tick = last_trigger_event_tick + 1U;
			return false;
		}

		if (dac_event)
		{
			if (dac_index >= dac_end)
			{
				return false;
			}
			dac_dma_ticks[dac_index] = dac_tick;
			dac_dma_codes[dac_index] = dac_code;
			dac_index++;
		}

		gpio_bsrr_values[edge_index] = gpio_bsrr;
		stim_mode_values[edge_index] = stim_mode;
		stim_mode_updates[edge_index] = update_stim_mode;
		held_command_events[edge_index] = held_command;
	}

	return dac_index == dac_end;
}

/* HAL_TIM_OC_Start_DMA() enables TIM2 as part of starting each channel. Keep
 * the counter parked just past the first DAC compare while each DMA stream is
 * armed, then force the timer back off. startPulseDma() restores the real
 * counter and starts all streams together after setup is complete.
 */
static HAL_StatusTypeDef armTimerDmaChannel(uint32_t channel,
		const uint32_t* event_ticks, uint16_t event_count,
		uint32_t setup_tick)
{
	__HAL_TIM_SET_COUNTER(&htim2, setup_tick);
	HAL_StatusTypeDef status = HAL_TIM_OC_Start_DMA(&htim2, channel,
			event_ticks, event_count);
	CLEAR_BIT(htim2.Instance->CR1, TIM_CR1_CEN);

	return status;
}

static HAL_StatusTypeDef startPulseDma(stimCommandQueue* stim_queue,
		uint32_t first_rise_tick, bool keep_timebase_running)
{
	uint32_t first_dac_tick;
	uint32_t first_output_tick;
	uint32_t first_trigger_tick;
	uint16_t first_dac_code;
	uint32_t first_gpio_value;
	uint8_t first_mode;
	bool update_first_mode;
	bool first_held;
	bool first_dac_event;

	uint32_t timer_resume_tick = __HAL_TIM_GET_COUNTER(&htim2);
	if (!keep_timebase_running)
	{
		/* Cold starts keep TIM2 frozen until every stream is armed. */
		CLEAR_BIT(htim2.Instance->CR1, TIM_CR1_CEN);
	}
	else
	{
		/* Leave enough time to build and arm every stream without moving an
		 * already-safe replacement timestamp.
		 */
		uint32_t earliest_rise_tick =
				__HAL_TIM_GET_COUNTER(&htim2) + LIVE_REARM_MARGIN_US;
		if (tickBefore(first_rise_tick, earliest_rise_tick))
		{
			first_rise_tick = earliest_rise_tick;
		}
	}

	next_rise_tick = first_rise_tick;
	next_event_phase = NEXT_EVENT_RISE;
	earliest_dac_event_tick = timer_resume_tick + 1U;
	stop_planned = false;

	if (!buildNextEvent(stim_queue, &first_dac_tick, &first_output_tick,
			&first_trigger_tick, &first_dac_code, &first_gpio_value,
			&first_mode, &update_first_mode, &first_held,
			&first_dac_event))
	{
		return HAL_OK;
	}
	if (!update_first_mode || !first_dac_event)
	{
		return HAL_ERROR;
	}

	/* The circular buffers hold the events after the directly seeded rising
	 * edge. DAC buffers have one entry per pulse; edge buffers have two.
	 */
	(void)fillDmaRange(stim_queue, 0U, EDGE_EVENT_COUNT,
			0U, DAC_EVENT_COUNT);

	if (keep_timebase_running)
	{
		/* Arm against expired compare values. The real values are installed
		 * together after every DMA stream is ready.
		 */
		uint32_t parked_tick = __HAL_TIM_GET_COUNTER(&htim2) - 1U;
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, parked_tick);
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, parked_tick);
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, parked_tick);
	}
	else
	{
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, first_dac_tick);
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, first_output_tick);
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, first_trigger_tick);
	}
	first_gpio_bsrr = first_gpio_value;
	first_stim_mode = first_mode;
	first_command_held = first_held;
	first_output_event_pending = true;
	output_event_index = 0U;
	active_stim_queue = stim_queue;

	/* A queue-empty stop leaves the DAC running without a trigger so zero can
	 * reach the output. Restore TIM2 triggering before seeding a new sequence.
	 */
	(void)HAL_DAC_Stop(&hdac1, DAC_CHANNEL_1);
	MODIFY_REG(hdac1.Instance->CR, DAC_CR_TSEL1 | DAC_CR_TEN1,
			DAC_TRIGGER_T2_TRGO);
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
			(uint32_t*)dac_dma_codes, DAC_EVENT_COUNT,
			DAC_ALIGN_12B_R) != HAL_OK)
	{
		return HAL_ERROR;
	}

	if (keep_timebase_running)
	{
		if (HAL_TIM_OC_Start_DMA(&htim2, TIM_CHANNEL_1,
				dac_dma_ticks, DAC_EVENT_COUNT) != HAL_OK)
		{
			return HAL_ERROR;
		}
		if (HAL_TIM_OC_Start_DMA(&htim2, TIM_CHANNEL_3,
				trigger_dma_ticks, EDGE_EVENT_COUNT) != HAL_OK)
		{
			return HAL_ERROR;
		}
		if (HAL_TIM_OC_Start_DMA(&htim2, TIM_CHANNEL_2,
				output_dma_ticks, EDGE_EVENT_COUNT) != HAL_OK)
		{
			return HAL_ERROR;
		}
	}
	else
	{
		/* Each HAL start briefly enables TIM2. Park it just beyond the first DAC
		 * tick so none of the real compares can occur during channel setup.
		 */
		uint32_t setup_tick = first_dac_tick + 1U;
		if (armTimerDmaChannel(TIM_CHANNEL_1, dac_dma_ticks,
				DAC_EVENT_COUNT, setup_tick) != HAL_OK)
		{
			return HAL_ERROR;
		}

		if (armTimerDmaChannel(TIM_CHANNEL_3, trigger_dma_ticks,
				EDGE_EVENT_COUNT, setup_tick) != HAL_OK)
		{
			return HAL_ERROR;
		}

		if (armTimerDmaChannel(TIM_CHANNEL_2, output_dma_ticks,
				EDGE_EVENT_COUNT, setup_tick) != HAL_OK)
		{
			return HAL_ERROR;
		}
	}
	__HAL_DMA_DISABLE_IT(htim2.hdma[TIM_DMA_ID_CC2], DMA_IT_HT | DMA_IT_TC);

	/* CC2 drives both DMA and an interrupt. DMA advances CCR2; the interrupt
	 * applies the GPIO and mode values associated with the compare that just
	 * occurred. DMA half/full interrupts are unused because the output consumer
	 * determines when each half is safe to refill.
	 */
	__HAL_TIM_ENABLE_IT(&htim2, TIM_IT_CC2);

	pulse_dma_active = true;
	if (keep_timebase_running)
	{
		uint32_t earliest_dac_tick =
				__HAL_TIM_GET_COUNTER(&htim2) + LIVE_COMPARE_MARGIN_US;
		if (tickBefore(first_dac_tick, earliest_dac_tick))
		{
			first_dac_tick = earliest_dac_tick;
		}
		if (tickBefore(first_trigger_tick, first_dac_tick))
		{
			first_dac_tick = first_trigger_tick;
		}

		__HAL_TIM_CLEAR_FLAG(&htim2,
				TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC3);
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, first_output_tick);
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_3, first_trigger_tick);
		__HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, first_dac_tick);
	}
	else
	{
		__HAL_TIM_SET_COUNTER(&htim2, timer_resume_tick);
		__HAL_TIM_CLEAR_FLAG(&htim2,
				TIM_FLAG_CC1 | TIM_FLAG_CC2 | TIM_FLAG_CC3);
		SET_BIT(htim2.Instance->CR1, TIM_CR1_CEN);
	}

	return HAL_OK;
}

static void stopTimerDmaChannelKeepingCounter(uint32_t channel,
		uint32_t dma_id, uint32_t dma_request)
{
	__HAL_TIM_DISABLE_DMA(&htim2, dma_request);
	(void)HAL_DMA_Abort_IT(htim2.hdma[dma_id]);
	TIM_CCxChannelCmd(htim2.Instance, channel, TIM_CCx_DISABLE);
	TIM_CHANNEL_STATE_SET(&htim2, channel, HAL_TIM_CHANNEL_STATE_READY);
}

static void stopPulseDma(bool reset_dac, bool keep_timebase_running)
{
	__HAL_TIM_DISABLE_IT(&htim2, TIM_IT_CC2);
	if (keep_timebase_running)
	{
		stopTimerDmaChannelKeepingCounter(TIM_CHANNEL_2,
				TIM_DMA_ID_CC2, TIM_DMA_CC2);
		stopTimerDmaChannelKeepingCounter(TIM_CHANNEL_1,
				TIM_DMA_ID_CC1, TIM_DMA_CC1);
		stopTimerDmaChannelKeepingCounter(TIM_CHANNEL_3,
				TIM_DMA_ID_CC3, TIM_DMA_CC3);
	}
	else
	{
		(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_2);
		(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_1);
		(void)HAL_TIM_OC_Stop_DMA(&htim2, TIM_CHANNEL_3);
	}
	__HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC2);

	/* Keep driving the current value across a stop/restart when another command
	 * is waiting. A drained or explicitly cleared queue returns the DAC to zero.
	 */
	(void)HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
	if (reset_dac)
	{
		/* With external triggering disabled, a DHR write is transferred to
		 * the output without requiring another TIM2 event.
		 */
		MODIFY_REG(hdac1.Instance->CR, DAC_CR_TSEL1 | DAC_CR_TEN1,
				DAC_TRIGGER_NONE);
		(void)HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
		(void)HAL_DAC_SetValue(&hdac1, DAC_CHANNEL_1,
				DAC_ALIGN_12B_R, 0U);
		scheduled_dac_code = 0U;
	}
	else
	{
		(void)HAL_DAC_Start(&hdac1, DAC_CHANNEL_1);
	}

	dma_half_done_mask[0] = 0U;
	dma_half_done_mask[1] = 0U;
	pulse_dma_active = false;
	stop_planned = false;
	next_event_phase = NEXT_EVENT_RISE;
	first_output_event_pending = false;
	output_event_index = 0U;
	active_stim_queue = NULL;
	held_command_active = false;
	dma_flush_requested = false;
	dma_flush_ready = false;
	replacement_flush_requested = false;
	replacement_rise_tick_valid = false;
}

static bool tickReached(uint32_t now, uint32_t deadline)
{
	return (int32_t)(now - deadline) >= 0;
}

void servicePulseDma(stimCommandQueue* stim_queue)
{
	if (stim_queue->clear_flag == 1)
	{
		uint8_t success = clearStimCommands(stim_queue);
		if (success)
		{
			stim_queue->clear_flag = 0;
		}
	}
	if (dma_flush_ready)
	{
		bool use_replacement_tick = replacement_rise_tick_valid &&
				(stim_queue->count > 0U);
		uint32_t first_rise_tick = replacement_rise_tick;

		stopPulseDma(stim_queue->count == 0U, use_replacement_tick);

		if (stim_queue->count > 0U)
		{
			if (!use_replacement_tick)
			{
				first_rise_tick = __HAL_TIM_GET_COUNTER(&htim2) +
						START_MARGIN_US;
			}

			if (startPulseDma(stim_queue, first_rise_tick,
					use_replacement_tick) != HAL_OK)
			{
				Error_Handler();
			}
		}
		return;
	}

	/* A finite queue normally stops just after its final falling event. If a
	 * clear arrives in that narrow window, there is no future CC2 event to set
	 * dma_flush_ready, so finish the stop here instead.
	 */
	if (dma_flush_requested && stop_planned &&
			tickReached(__HAL_TIM_GET_COUNTER(&htim2), stop_after_tick))
	{
		stopPulseDma(stim_queue->count == 0U, false);
		return;
	}

	/* Once a replacement is queued, do not refill either circular half with
	 * stale repetitions. The CC2 ISR will request a rebuild on the next fall.
	 */
	if (dma_flush_requested)
	{
		return;
	}

	if (!pulse_dma_active)
	{
		if (stim_queue->count > 0U &&
				startPulseDma(stim_queue,
						__HAL_TIM_GET_COUNTER(&htim2) +
						START_MARGIN_US, false) != HAL_OK)
		{
			Error_Handler();
		}
		return;
	}

	if (stop_planned)
	{
		if (tickReached(__HAL_TIM_GET_COUNTER(&htim2), stop_after_tick))
		{
			stopPulseDma(stim_queue->count == 0U, false);
		}
		return;
	}

	for (uint8_t half = 0U; half < 2U; half++)
	{
		if (dma_half_done_mask[half] == DMA_DONE_ALL)
		{
			dma_half_done_mask[half] = 0U;

			uint16_t edge_start = half * EDGE_EVENTS_PER_HALF;
			uint16_t dac_start = half * DAC_EVENTS_PER_HALF;
			(void)fillDmaRange(stim_queue,
					edge_start, EDGE_EVENTS_PER_HALF,
					dac_start, DAC_EVENTS_PER_HALF);
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
		held_command_active = first_command_held;
		first_output_event_pending = false;
		return;
	}

	uint16_t event_index = output_event_index;
	bool rising_event = stim_mode_updates[event_index];

	OUTPUT_GPIO_PORT->BSRR = gpio_bsrr_values[event_index];
	if (rising_event &&
			active_stim_queue != NULL)
	{
		active_stim_queue->stim_mode = stim_mode_values[event_index];
		held_command_active = held_command_events[event_index];
	}
	output_event_index++;

	/* DMA loads the next CCR2 value at the current compare. Waiting until the
	 * interrupt consumes the final output entry prevents an early buffer refill.
	 */
	if (output_event_index == EDGE_EVENTS_PER_HALF)
	{
		dma_half_done_mask[0] |= DMA_DONE_OUTPUT_CC2;
	}
	else if (output_event_index == EDGE_EVENT_COUNT)
	{
		output_event_index = 0U;
		dma_half_done_mask[1] |= DMA_DONE_OUTPUT_CC2;
	}

	if (!rising_event && dma_flush_requested)
	{
		/* The next entry is the held command's already-scheduled rising edge.
		 * Preserve it so a replacement observes the full original period.
		 */
		if (replacement_flush_requested && active_stim_queue != NULL &&
				active_stim_queue->count > 0U)
		{
			replacement_rise_tick = output_dma_ticks[output_event_index];
			replacement_rise_tick_valid = true;
		}
		else
		{
			replacement_rise_tick_valid = false;
		}

		/* All four fall events have occurred at this timer count. Leave TIM2's
		 * timebase running with the trigger low; the service interrupt will
		 * replace and rearm the DMA streams.
		 */
		__HAL_TIM_DISABLE_IT(htim, TIM_IT_CC2);
		dma_flush_ready = true;
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
