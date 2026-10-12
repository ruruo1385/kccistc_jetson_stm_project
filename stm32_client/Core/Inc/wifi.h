#ifndef WIFI_H
#define WIFI_H
#include <stdint.h>
#include "main.h"

typedef void (*WifiLineHandler)(const char *line);
void Wifi_Init(WifiLineHandler handler);
void Wifi_Update(void);
uint8_t Wifi_IsReady(void);
uint8_t Wifi_SendLine(const char *line);
void Wifi_RxComplete(UART_HandleTypeDef *huart);
void Wifi_UartError(UART_HandleTypeDef *huart);

/* Debugger watch variables. 1 = TCP connected. Error: 1 UART,
 * 2 receive overflow, 3 AT timeout, 4 AT ERROR/FAIL, 5 disconnect,
 * 6 invalid configuration, 7 framing error, 8 transmit failure. */
extern volatile uint8_t wifi_connected;
extern volatile uint8_t wifi_authenticated;
extern volatile uint32_t wifi_last_error;
extern volatile uint32_t wifi_stop_error;
extern volatile uint32_t wifi_tx_dropped;
extern volatile uint32_t wifi_tx_uncertain;
#endif
