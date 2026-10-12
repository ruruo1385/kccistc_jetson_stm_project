#include "tracking.h"
#include "main.h"

uint8_t Tracking_Read1(void)
{
    return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_0)
           == GPIO_PIN_SET;
}

uint8_t Tracking_Read2(void)
{
    return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_1)
           == GPIO_PIN_SET;
}

uint8_t Tracking_Read3(void)
{
    return HAL_GPIO_ReadPin(GPIOC, GPIO_PIN_2)
           == GPIO_PIN_SET;
}

uint8_t Tracking_IsDetected1(void)
{
    return Tracking_Read1() == 0U;
}

#define EXIT_SENSOR_FILTER_MS 50U

uint8_t Tracking_IsDetected2(void)
{
    static uint8_t initialized;
    static uint8_t candidate;
    static uint8_t stable;
    static uint32_t changed_at;
    uint32_t now = HAL_GetTick();
    uint8_t detected = Tracking_Read2() == 0U;

    // PC1 normal exit: retain the previous state through short glitches.
    if (!initialized)
    {
        initialized = 1U;
        candidate = stable = detected;
        changed_at = now;
    }
    if (detected != candidate)
    {
        candidate = detected;
        changed_at = now;
    }
    if (now - changed_at >= EXIT_SENSOR_FILTER_MS)
    {
        stable = candidate;
    }
    return stable;
}

uint8_t Tracking_IsDetected3(void)
{
    return Tracking_Read3() == 0U;
}