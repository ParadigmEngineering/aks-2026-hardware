#include "braking.h"

extern TIM_HandleTypeDef htim8;

void Servo_WriteMicroseconds(uint16_t channel, uint16_t us){
    if (us < SERVO_CLAMP_MIN_US) us = SERVO_CLAMP_MIN_US;
    if (us > SERVO_CLAMP_MAX_US) us = SERVO_CLAMP_MAX_US;

    if (channel == 1){
    	__HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_1, us);
    }
    else if (channel == 2){
    	__HAL_TIM_SET_COMPARE(&htim8, TIM_CHANNEL_2, us);
    }
}
