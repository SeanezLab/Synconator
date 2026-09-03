/*
 * stim_command_manager.h
 *
 *  Created on: Jul 31, 2026
 *      Author: k.rodolfo
 */

#ifndef INC_STIM_COMMAND_MANAGER_H_
#define INC_STIM_COMMAND_MANAGER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32l4xx_hal.h"
#include <stdint.h>


// I want to allocate a max of 6kb of memory to the commands. At a stim rate of 200hz, 2byte encoding of amp, and float encoding of
// time, one command is 6 bytes. This allows me space for 1000 commands, or 5 seconds of preloaded stimulation. That should be plenty.
#define MAX_CMD_LENGTH 1000

typedef struct{
	uint16_t ampArray[MAX_CMD_LENGTH];
	uint32_t periodArray[MAX_CMD_LENGTH];
	float totalTime;
	uint16_t remainingSpace;
	uint16_t head;
	uint16_t tail;
	uint8_t busy_flag;
	uint8_t stop_flag;
	uint8_t queue_lock;
}stimCommandQueue;

void stim_command_init(stimCommandQueue* stim_queue);
uint8_t pushCommand(stimCommandQueue* stim_queue, uint16_t* amp, uint32_t* period, uint16_t cmd_size);
uint8_t popCommand(stimCommandQueue* stim_queue, uint16_t* amp_in, uint32_t* time_in);
void updateRemainingSpace(stimCommandQueue* stim_queue);
void servicePulseDma(stimCommandQueue *stim_queue);
void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim);
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef* hdac);





#ifdef __cplusplus
}
#endif

#endif /* INC_STIM_COMMAND_MANAGER_H_ */
