#include "button.h"
#include "main.h"

#define BUTTON_STABLE_MS 20U

static uint8_t candidate;
static uint8_t stable;
static uint32_t changed_at;

uint8_t Button_ReadRaw(void)
{
    return HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_12)
           == GPIO_PIN_SET;
}

void Button_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_12;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &gpio);

    candidate = (Button_ReadRaw() == 0U);
    stable = candidate;
    changed_at = HAL_GetTick();
}

uint8_t Button_WasPressed(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t pressed = (Button_ReadRaw() == 0U);

    if (pressed != candidate)
    {
        candidate = pressed;
        changed_at = now;
    }

    if (now - changed_at >= BUTTON_STABLE_MS &&
        stable != candidate)
    {
        stable = candidate;

        if (stable)
        {
            return 1U;
        }
    }

    return 0U;
}