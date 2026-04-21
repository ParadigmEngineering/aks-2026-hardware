// Implements
#include "steering.h"

static SteeringController_t steering;

void Steering_Init(TIM_HandleTypeDef *htim_output, TIM_HandleTypeDef *htim_input) {

		steering.htim_output     = htim_output;
	    steering.htim_input      = htim_input;

	    steering.input_start     = 0;
	    steering.input_width_us  = 0;
	    steering.input_edge      = 0;

	    //Start PWM output to ClearPath motor
	    HAL_TIM_PWM_Start(htim_output, TIM_CHANNEL_3); // TIM2, CH3

	    // Start input capture interrupts for steering channel
	    HAL_TIM_IC_Start_IT(htim_input, TIM_CHANNEL_1);
}

void Steering_Tick(void) {
    uint32_t pulse_us = steering.input_width_us;
    uint32_t duty;

	// Deadband - if within 20 us of center, treat exactly as center (fixes idle jitter)
	if (pulse_us > (STEERING_PWM_CENTER_US - STEERING_DEADBAND_US) &&
		pulse_us < (STEERING_PWM_CENTER_US + STEERING_DEADBAND_US))
	{
		pulse_us = STEERING_PWM_CENTER_US;
	}

    if (pulse_us < STEERING_PWM_MIN_US) pulse_us = STEERING_PWM_MIN_US;
    if (pulse_us > STEERING_PWM_MAX_US) pulse_us = STEERING_PWM_MAX_US;


    duty = ((pulse_us - STEERING_PWM_MIN_US) * (STEERING_DUTY_MAX - STEERING_DUTY_MIN)) / (STEERING_PWM_MAX_US - STEERING_PWM_MIN_US) + STEERING_DUTY_MIN;

    __HAL_TIM_SET_COMPARE(steering.htim_output, TIM_CHANNEL_3, duty);
}

// Get/return pulse width
uint32_t Steering_GetPulseWidth(void)
{
    return steering.input_width_us;
}

SteeringController_t* Steering_GetHandle(void) {
    return &steering;
}

