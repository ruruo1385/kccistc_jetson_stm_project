#include "led.h"
#include "main.h"

void LED_SetColor(LedColor color)
{
    uint16_t pins = 0U;

    switch (color)
    {
        case LED_RED:
            pins = GPIO_PIN_0;
            break;

        case LED_GREEN:
            pins = GPIO_PIN_1;
            break;

        case LED_BLUE:
            pins = GPIO_PIN_2;
            break;

        case LED_YELLOW:
            pins = GPIO_PIN_0 | GPIO_PIN_1;
            break;

        case LED_OFF:
        default:
            break;
    }

    // 이전 색 끄기
    HAL_GPIO_WritePin(
        GPIOB,
        GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2,
        GPIO_PIN_RESET
    );

    // 새 색 켜기
    if (pins != 0U)
    {
        HAL_GPIO_WritePin(GPIOB, pins, GPIO_PIN_SET);
    }
}