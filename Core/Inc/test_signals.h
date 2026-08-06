/*
 * test_signals.h
 *
 *  Created on: Feb 28, 2026
 *      Author: k.rodolfo
 */

#ifndef INC_TEST_SIGNALS_H_
#define INC_TEST_SIGNALS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

extern uint8_t frame_counter;
void increment_cos_counter(void);
void increment_frame_counter(void);
float update_cos_signal(void);
float calculate_cos_dot(void);
float traj_cos_theta(float freq, float time);
float traj_cos_theta_dot(float freq, float time);



#ifdef __cplusplus
}
#endif

#endif /* INC_TEST_SIGNALS_H_ */
