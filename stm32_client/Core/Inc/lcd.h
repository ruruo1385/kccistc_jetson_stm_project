#ifndef LCD_H
#define LCD_H

#include <stdint.h>

uint8_t LCD_Init(void);
void LCD_Update(void);
void LCD_ShowResetBlocked(void);
void LCD_ClearResetBlocked(void);

void LCD_SetReceivedResult(uint8_t is_normal);

#endif