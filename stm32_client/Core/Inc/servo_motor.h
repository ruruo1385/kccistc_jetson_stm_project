#ifndef SERVO_MOTOR_H
#define SERVO_MOTOR_H

#include <stdint.h>

void Servo_Init(void);
void Servo_SetPulseUs(uint16_t pulse_us);
void Servo_SortOnce(void);

#endif