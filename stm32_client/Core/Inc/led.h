#ifndef LED_H
#define LED_H

typedef enum
{
    LED_OFF,
    LED_RED,
    LED_GREEN,
    LED_BLUE,
    LED_YELLOW
} LedColor;

void LED_SetColor(LedColor color);

#endif