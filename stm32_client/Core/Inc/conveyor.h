#ifndef CONVEYOR_H
#define CONVEYOR_H

#include <stdint.h>

void Conveyor_Init(void);
void Conveyor_Run(uint8_t speed_percent);
void Conveyor_Stop(void);

#endif