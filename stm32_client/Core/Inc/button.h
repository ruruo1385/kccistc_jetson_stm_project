#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>

void Button_Init(void);

// 새로 눌렀을 때 한 번만 1 반환
uint8_t Button_WasPressed(void);

// 안 누름 1, 누름 0
uint8_t Button_ReadRaw(void);

#endif