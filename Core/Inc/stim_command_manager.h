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

#include <stdint.h>


// I want to allocate a max of 6kb of memory to the commands. At a stim rate of 200hz, 2byte encoding of amp, and float encoding of
// time, one command is 6 bytes. This allows me space for 1000 commands, or 5 seconds of preloaded stimulation. That should be plenty.
#define MAX_CMD_LENGTH 1000

typedef struct{
	uint16_t ampArray[MAX_CMD_LENGTH];
	float periodArray[MAX_CMD_LENGTH];
	float totalTime;
	uint16_t remainingSpace;
	uint16_t tail;
}stimCommandQueue;

void stim_command_init(stimCommandQueue* stim_queue);
void pushCommand(stimCommandQueue* stim_queue, uint16_t* amp, float* period, uint16_t cmd_size);
void popCommand(stimCommandQueue* stim_queue, uint16_t* amp_in, float* time_in);




#ifdef __cplusplus
}
#endif

#endif /* INC_STIM_COMMAND_MANAGER_H_ */
