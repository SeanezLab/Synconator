/*
 * stim_command_manager.h
 *
 *  Created on: Jul 31, 2026
 *      Author: k.rodolfo
 *
 */

#ifndef INC_STIM_COMMAND_MANAGER_H_
#define INC_STIM_COMMAND_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"
#include <stdint.h>


// Keep the queued command arrays below 6 KB total. Each command contains a
// mode, GPIO mask, amplitude, and period.
#define MAX_CMD_LENGTH 400

typedef struct{
	uint8_t modeArray[MAX_CMD_LENGTH]; // Per-command stimulation mode (0 single, 1 continuous)
	uint16_t gpioArray[MAX_CMD_LENGTH]; // Per-command GPIO bit mask
	uint16_t ampArray[MAX_CMD_LENGTH]; // Requested amplitude in mA
	uint32_t periodArray[MAX_CMD_LENGTH]; // Time to the next pulse in microseconds
	float totalTime; // In seconds
	uint16_t remainingSpace;
	uint16_t head;
	uint16_t tail;
	uint16_t count;
	uint8_t busy_flag;
	uint8_t stop_flag;
	uint8_t queue_lock;
	uint8_t clear_flag;
	volatile uint8_t stim_mode; // Mode applied by the most recent rising event
	uint8_t last_mode;
	uint16_t last_gpio;
	uint16_t last_amp;
	uint32_t last_period;
	uint16_t watchdog_counter;
}stimCommandQueue;

void stim_command_init(stimCommandQueue* stim_queue);
void incrementWatchdogCounter(stimCommandQueue* stim_queue);
uint8_t getLastMode(stimCommandQueue* stim_queue, uint8_t* mode_in);
uint8_t getLastGpio(stimCommandQueue* stim_queue, uint16_t* gpio_in);
uint8_t getLastAmp(stimCommandQueue* stim_queue, uint16_t* amp_in);
uint8_t getLastPeriod(stimCommandQueue* stim_queue, uint32_t* period_in);
/* Clears the queue immediately; active DMA stops at the next safe falling edge. */
uint8_t clearStimCommands(stimCommandQueue* stim_queue);
uint8_t pushCommand(stimCommandQueue* stim_queue, uint8_t* mode, uint16_t* gpio, uint16_t* amp, uint32_t* period, uint16_t cmd_size);
uint8_t popCommand(stimCommandQueue* stim_queue, uint8_t* mode_in, uint16_t* gpio_in, uint16_t* amp_in, uint32_t* time_in);
uint8_t disposeCommand(stimCommandQueue* stim_queue);
void servicePulseDma(stimCommandQueue *stim_queue);
void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef* htim);
void HAL_TIM_PWM_PulseFinishedHalfCpltCallback(TIM_HandleTypeDef* htim);
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim);
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef* hdac);
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef* hdac);





#ifdef __cplusplus
}
#endif

#endif /* INC_STIM_COMMAND_MANAGER_H_ */
