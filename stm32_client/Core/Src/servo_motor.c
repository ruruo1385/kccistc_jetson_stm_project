#include "servo_motor.h"
#include "main.h"

extern TIM_HandleTypeDef htim1;

#define SERVO_MIN_US 1000U
#define SERVO_MAX_US 2000U
#define SERVO_HOME_US 1500U

void Servo_SetPulseUs(uint16_t pulse_us)
{
    if (pulse_us < SERVO_MIN_US)
    {
        pulse_us = SERVO_MIN_US;
    }
    else if (pulse_us > SERVO_MAX_US)
    {
        pulse_us = SERVO_MAX_US;
    }

    // Single servo: PA8 / TIM1_CH1
    __HAL_TIM_SET_COMPARE(
        &htim1, TIM_CHANNEL_1, pulse_us);
}

void Servo_Init(void)
{
    Servo_SetPulseUs(SERVO_HOME_US);

    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK)
    {
        Error_Handler();
    }
}

void Servo_SortOnce(void)
{
    Servo_SetPulseUs(1200U);
    HAL_Delay(500);

    Servo_SetPulseUs(SERVO_HOME_US);
    HAL_Delay(500);
}