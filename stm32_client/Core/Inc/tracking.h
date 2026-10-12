#ifndef TRACKING_H
#define TRACKING_H

#include <stdint.h>

uint8_t Tracking_Read1(void);
uint8_t Tracking_Read2(void);
uint8_t Tracking_Read3(void);

uint8_t Tracking_IsDetected1(void);
uint8_t Tracking_IsDetected2(void);
uint8_t Tracking_IsDetected3(void);

#endif