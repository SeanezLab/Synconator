/*
 * test_signal.c
 *
 *  Created on: Feb 28, 2026
 *      Author: k.rodolfo
 */


#include "test_signals.h"
#include "math.h"

#define PI 3.1415926F

float cos_counter = 0;
float cos_dot = 0;
uint8_t frame_counter = 0;


void increment_cos_counter(void)
{
	if (cos_counter >= 2*PI)
	{
		cos_counter = 0;
	}
	else
	{
		cos_counter += 1.0F;
	}
}

void increment_frame_counter(void)
{
	if (frame_counter >= 255)
	{
		frame_counter = 0;
	}
	else
	{
		frame_counter += 1;
	}
}


float update_cos_signal(void)
{
	float cos_out = cosf(cos_counter);
	increment_cos_counter();
	return cos_out;
}

float calculate_cos_dot(void)
{
	float cos_dot = -2 * sinf(cos_counter);
	return cos_dot;

}

float traj_cos_theta(float freq, float time)
{
	float theta = cosf(2*PI*freq*time);
	return theta;
}

float traj_cos_theta_dot(float freq, float time)
{
	float theta_dot = -2*PI*freq*sinf(2*PI*freq*time);
	return theta_dot;
}
