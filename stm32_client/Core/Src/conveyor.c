#include "conveyor.h"
#include "main.h"

extern TIM_HandleTypeDef htim2;

volatile int32_t conveyor_pwm_status = -1;
volatile uint32_t conveyor_channel_state = 0;

void Conveyor_Stop(void)
{
    // ENA의 PWM을 0으로 설정
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, 0U);

    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);
}

void Conveyor_Init(void)
{
    Conveyor_Stop();

    // PWM 시작 직전 채널 상태 기록
    conveyor_channel_state = htim2.ChannelState[0];

    // PWM 시작 결과 기록
    conveyor_pwm_status =
        HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);

    if (conveyor_pwm_status != HAL_OK)
    {
        Error_Handler();
    }
}

// 속도 명령: 0~100%
void Conveyor_Run(uint8_t speed_percent)
{
    if (speed_percent > 100U)
    {
        speed_percent = 100U;
    }

    if (speed_percent == 0U)
    {
        Conveyor_Stop();
        return;
    }

    // 컨베이어 운전 방향
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_4, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, GPIO_PIN_5, GPIO_PIN_RESET);

    uint32_t period =
        __HAL_TIM_GET_AUTORELOAD(&htim2) + 1U;

    uint32_t pulse =
        (period * speed_percent) / 100U;

    __HAL_TIM_SET_COMPARE(
        &htim2,
        TIM_CHANNEL_1,
        pulse
    );
}