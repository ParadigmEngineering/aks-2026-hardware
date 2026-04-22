/*
 * steering.c
 *
 * Created on: Mar 19, 2026
 *     Authors: Jordan Bishop
 *     		   Dipayan Dhar
 *
 * Handles RC PWM input capture from the Pixhawk and generates
 *  a PWM position command for the ClearPath steering motor.
 *
 *  us = micro seconds
 *
 *  Signal chain:
 *  Pixhawk CH4 -> PA11 (TIM4 CH1) -> STM32 -> PC0 (TIM1 CH1) -> Steering Motor Input B+ (Black wire)
 */

#ifndef STEERING_H
#define STEERING_H

#include "stm32g4xx_hal.h"

typedef struct {
	TIM_HandleTypeDef *htim_output;
	TIM_HandleTypeDef *htim_input;
	volatile uint32_t input_start;
	volatile uint32_t input_width_us;
	volatile uint32_t input_edge;
} SteeringController_t;


#define STEERING_PWM_MIN_US 1000
#define STEERING_PWM_MAX_US 2000
#define STEERING_PWM_CENTER_US 1500
#define STEERING_DUTY_MAX 250
#define STEERING_DUTY_MIN 5
#define STEERING_DUTY_CENTER ((STEERING_DUTY_MAX + STEERING_DUTY_MIN) / 2U)  // = 127
#define STEERING_DEADBAND_US 20

/** void Steering_Init(TIM_HandleTypeDef *htim_output, TIM_HandleTypeDef *htim_input);
 *
 * Initialize steering struct and STM timers (TIM1 CH. 1, and TIM4 CH.1)
 */
void Steering_Init(TIM_HandleTypeDef *htim_output, TIM_HandleTypeDef *htim_input);

/** void Steering_Tick(void);
 *
 * Update the ClearPath PWM output based on the latest RC input
 */
void Steering_Tick(void);

/** uint32_t Steering_GetPulseWidth(void);
 *
 * Return pulse width
 */
uint32_t Steering_GetPulseWidth(void);


SteeringController_t* Steering_GetHandle(void);

#endif // STEERING_H
