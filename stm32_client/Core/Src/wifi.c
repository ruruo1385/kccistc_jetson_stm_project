#include "wifi.h"
#include "wifi_config.h"
#include "sorter.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart1;
#define RX_SIZE 1024U
#define LINE_SIZE 160U
#define TX_SIZE 16U
#define MESSAGE_SIZE 80U

typedef enum {
    DISABLED, BOOT, AT_CHECK, ECHO_OFF, MODE_SET, MUX_SET,
    TRANSFER_SET, INFO_SET, JOIN, CONNECTING,
    ONLINE, PROMPT, SEND_DONE, BACKOFF
} WifiState;
static WifiState state;
static uint32_t deadline;
static uint8_t rx_byte;
static uint8_t rx_ring[RX_SIZE];
static volatile uint32_t rx_write, rx_read;
static volatile uint8_t rx_fault, rx_overflow;
static char at_line[LINE_SIZE], product_line[160];
static uint32_t at_length, product_length, payload_left;
static uint8_t product_discard;
static char tx_queue[TX_SIZE][MESSAGE_SIZE];
static uint8_t tx_read, tx_write, tx_count;
static WifiLineHandler on_line;
volatile uint8_t wifi_connected;
volatile uint8_t wifi_authenticated;
static uint32_t login_deadline;
volatile uint32_t wifi_last_error, wifi_tx_dropped, wifi_tx_uncertain;
volatile uint32_t wifi_uart_error_code = 0U;
volatile uint32_t wifi_uart_error_count = 0U;

static uint8_t Expired(uint32_t now)
{
    return (int32_t)(now - deadline) >= 0;
}

static void FlushRx(void)
{
    uint32_t saved = __get_PRIMASK();
    __disable_irq();
    rx_read = rx_write;
    rx_overflow = 0U;
    __set_PRIMASK(saved);
    at_length = product_length = payload_left = 0U;
    product_discard = 0U;
}

volatile uint32_t wifi_failed_stage;
volatile char wifi_failed_reply[64];
volatile uint32_t wifi_stop_error = 0U;

static void Fail(uint32_t error)
{
    if (state != BACKOFF) {
    wifi_failed_stage = (uint32_t)state;
    for (uint32_t i = 0U; i < sizeof(wifi_failed_reply); ++i)
        wifi_failed_reply[i] = (i < at_length && i < sizeof(at_line)) ? at_line[i] : '\0';
    wifi_failed_reply[sizeof(wifi_failed_reply) - 1U] = '\0';
    }
    /* A broken link invalidates any partially received verdict. Do not
       automatically restart the conveyor when the link reconnects. */
    if (wifi_connected)
    {
        if (wifi_stop_error == 0U) wifi_stop_error = error;
        Sorter_Stop();
    }
    if (state == PROMPT || state == SEND_DONE) wifi_tx_uncertain++;
    wifi_connected = 0U;
    wifi_authenticated = 0U;
    wifi_last_error = error;
    wifi_tx_dropped += tx_count;
    tx_read = tx_write = tx_count = 0U;
    FlushRx();
    state = BACKOFF;
    deadline = HAL_GetTick() + 3000U;
}

static uint8_t Command(const char *command, WifiState next, uint32_t timeout)
{
    state = next;
    deadline = HAL_GetTick() + timeout;
    if (HAL_UART_Transmit(&huart1, (uint8_t *)command,
                         (uint16_t)strlen(command), 30U) != HAL_OK) {
        Fail(8U);
        return 0U;
    }
    return 1U;
}

/* Quote user configuration for ESP AT commands. */
static uint8_t Escape(const char *src, char *dst, uint32_t capacity)
{
    uint32_t used = 0U;
    for (; *src; ++src) {
        if ((unsigned char)*src < 32U) return 0U;
        if (*src == '\\' || *src == '"' || *src == ',') {
            if (used + 1U >= capacity) return 0U;
            dst[used++] = '\\';
        }
        if (used + 1U >= capacity) return 0U;
        dst[used++] = *src;
    }
    dst[used] = '\0';
    return 1U;
}

static void NextCommand(void)
{
    char command[256], ssid[65], password[129], host[129];
    switch (state) {
    case AT_CHECK: Command("ATE0\r\n", ECHO_OFF, 2000U); break;
    case ECHO_OFF: Command("AT+CWMODE=1\r\n", MODE_SET, 2000U); break;
    case MODE_SET: Command("AT+CIPMUX=0\r\n", MUX_SET, 2000U); break;
    case MUX_SET: Command("AT+CIPMODE=0\r\n", TRANSFER_SET, 2000U); break;
    case TRANSFER_SET: Command("AT+CIPDINFO=0\r\n", INFO_SET, 2000U); break;
    case INFO_SET:
        if (!Escape(WIFI_SSID, ssid, sizeof ssid) ||
            !Escape(WIFI_PASSWORD, password, sizeof password)) {
            Fail(6U); break;
        }
        snprintf(command, sizeof command, "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
        Command(command, JOIN, 30000U);
        break;
    case JOIN:
        if (!Escape(WIFI_PI_HOST, host, sizeof host)) { Fail(6U); break; }
        snprintf(command, sizeof command, "AT+CIPSTART=\"TCP\",\"%s\",%u\r\n",
                 host, (unsigned)WIFI_PI_PORT);
        Command(command, CONNECTING, 10000U);
        break;
    case CONNECTING:
        wifi_connected = 1U;
        wifi_authenticated = 0U;
        wifi_last_error = 0U;
        state = ONLINE;
        /* Same login framing as the working classroom example. */
        snprintf(tx_queue[tx_write], MESSAGE_SIZE, "[%s:%s]",
                 WIFI_LOGIN_ID, WIFI_LOGIN_PASSWORD);
        tx_write = (tx_write + 1U) % TX_SIZE;
        tx_count++;
        login_deadline = HAL_GetTick() + 10000U;
        break;
    default: break;
    }
}

static void ProcessAtLine(void)
{
    at_line[at_length] = '\0';
    if (strcmp(at_line, "CLOSED") == 0 ||
        (strcmp(at_line, "WIFI DISCONNECT") == 0 && state != JOIN && state != BOOT && state != BACKOFF)) {
        Fail(5U);
    } else if (strcmp(at_line, "ready") == 0) {
        if (state == BOOT) Command("AT\r\n", AT_CHECK, 2000U);
        else if (state != BACKOFF && state != DISABLED) Fail(5U);
    } else if (strcmp(at_line, "ERROR") == 0 ||
               strcmp(at_line, "FAIL") == 0 ||
               strcmp(at_line, "SEND FAIL") == 0 ||
               strncmp(at_line, "busy", 4U) == 0) {
        if (state != BOOT && state != BACKOFF && state != DISABLED) Fail(4U);
    } else if (strcmp(at_line, "SEND OK") == 0 && state == SEND_DONE) {
        tx_read = (tx_read + 1U) % TX_SIZE;
        tx_count--;
        state = ONLINE;
    } else if (strcmp(at_line, "OK") == 0) {
        NextCommand();
    }
}

static void ProductByte(uint8_t byte)
{
    if (byte == '\n') {
        if (!product_discard && product_length && wifi_connected) {
            product_line[product_length] = '\0';
            if (!wifi_authenticated) {
                /* Classroom client explicitly handles ' New conn'.
                   Wait for the server confirmation, not just SEND OK. */
                if (strstr(product_line, " New conn") != NULL) {
                    wifi_authenticated = 1U;
                    wifi_last_error = 0U;
                }
            } else if (on_line) on_line(product_line);
        }
        product_length = 0U;
        product_discard = 0U;
    } else if (byte != '\r' && !product_discard) {
        if (byte < 32U || byte > 126U || product_length >= sizeof(product_line) - 1U) {
            product_discard = 1U;
            product_length = 0U;
        } else product_line[product_length++] = (char)byte;
    }
}

static void ReceiveByte(uint8_t byte)
{
    if (payload_left) {
        ProductByte(byte);
        payload_left--;
        return;
    }
    if (byte == '>' && state == PROMPT) {
        state = SEND_DONE;
        deadline = HAL_GetTick() + 5000U;
        const char *message = tx_queue[tx_read];
        if (HAL_UART_Transmit(&huart1, (uint8_t *)message,
                             (uint16_t)strlen(message), 15U) != HAL_OK) Fail(8U);
        at_length = 0U;
        return;
    }
    if (byte == '\r') return;
    if (byte == '\n') {
        if (at_length) ProcessAtLine();
        at_length = 0U;
        return;
    }
    if (at_length >= sizeof(at_line) - 1U) { Fail(7U); return; }
    at_line[at_length++] = (char)byte;
    if (byte == ':' && at_length >= 6U && strncmp(at_line, "+IPD,", 5U) == 0) {
        uint32_t count = 0U;
        for (uint32_t i = 5U; i + 1U < at_length; ++i) {
            if (at_line[i] < '0' || at_line[i] > '9') { Fail(7U); return; }
            count = count * 10U + (uint32_t)(at_line[i] - '0');
            if (count > 65535U) { Fail(7U); return; }
        }
        if (at_length == 6U) { Fail(7U); return; }
        payload_left = count;
        at_length = 0U;
    }
}

volatile uint32_t wifi_detected_baud = 38400U;

void Wifi_Init(WifiLineHandler handler)
{
    on_line = handler;
    wifi_connected = 0U;
    wifi_authenticated = 0U;
    wifi_uart_error_code = wifi_uart_error_count = 0U;
    wifi_last_error = wifi_tx_dropped = wifi_tx_uncertain = 0U;
    rx_write = rx_read = 0U;
    rx_fault = rx_overflow = 0U;
    tx_read = tx_write = tx_count = 0U;
    at_length = product_length = payload_left = 0U;
    product_discard = 0U;
    state = DISABLED;
    if (strcmp(WIFI_SSID, "X") == 0 || strcmp(WIFI_PI_HOST, "X") == 0 ||
        strcmp(WIFI_PASSWORD, "X") == 0 ||
        strcmp(WIFI_LOGIN_PASSWORD, "X") == 0 ||
        strlen(WIFI_LOGIN_ID) == 0U || strlen(WIFI_LOGIN_ID) > 9U ||
        strlen(WIFI_LOGIN_PASSWORD) == 0U || strlen(WIFI_LOGIN_PASSWORD) > 9U ||
        strpbrk(WIFI_LOGIN_ID, "[]:\r\n") != NULL ||
        strpbrk(WIFI_LOGIN_PASSWORD, "[]:\r\n") != NULL ||
        WIFI_PI_PORT == 0U || WIFI_PI_PORT > 65535U ||
        strlen(WIFI_SSID) > 32U || strlen(WIFI_PASSWORD) > 64U ||
        strlen(WIFI_PI_HOST) > 64U) {
        wifi_last_error = 6U;
        return;
    }
    /* Initial setup runs while the conveyor is stopped. Do not receive
       the ESP ROM boot log at the AT command baud rate. */
    HAL_Delay(2000U);
    huart1.Init.BaudRate = 38400U;
    if (HAL_UART_Init(&huart1) != HAL_OK) { wifi_last_error = 1U; return; }
    HAL_UART_Transmit(&huart1, (uint8_t *)"AT+RST\r\n", 8U, 100U);
    HAL_Delay(2500U);
    HAL_UART_AbortReceive(&huart1);
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    FlushRx();
    if (HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) != HAL_OK) {
        rx_fault = 1U;
        Fail(1U);
        return;
    }
    /* 전원 켜진 뒤 부팅이 끝날 시간을 확보 */
    state = BOOT;
    deadline = HAL_GetTick() + 3000U;
}

void Wifi_RxComplete(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;
    /* 수신 재시작 전에 오류 값 보관 */
    if (huart->ErrorCode != HAL_UART_ERROR_NONE)
    {
        wifi_uart_error_code = huart->ErrorCode;
    }
    uint32_t next = (rx_write + 1U) % RX_SIZE;
    if (next == rx_read) rx_overflow = 1U;
    else { rx_ring[rx_write] = rx_byte; rx_write = next; }
    if (HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) != HAL_OK) rx_fault = 1U;
}

void Wifi_UartError(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1)
    {
        return;
    }

    /* 이미 저장한 오류를 0으로 덮어쓰지 않음 */
    if (huart->ErrorCode != HAL_UART_ERROR_NONE)
    {
        wifi_uart_error_code = huart->ErrorCode;
    }

    wifi_uart_error_count++;
    rx_fault = 1U;
}

uint8_t Wifi_IsReady(void) { return wifi_connected && wifi_authenticated; }

uint8_t Wifi_SendLine(const char *line)
{
    if (!line || !Wifi_IsReady() || tx_count >= TX_SIZE ||
        strlen(line) >= MESSAGE_SIZE) {
        wifi_tx_dropped++;
        return 0U;
    }
    strcpy(tx_queue[tx_write], line);
    tx_write = (tx_write + 1U) % TX_SIZE;
    tx_count++;
    return 1U;
}

void Wifi_Update(void)
{
    if (state == DISABLED) return;
    if (rx_fault)
    {
        HAL_UART_AbortReceive(&huart1);
        rx_fault = 0U;

        if (state == BOOT)
        {
            /* 부팅 로그 오류에서는 재리셋하지 않고 수신만 복구 */
            FlushRx();
        }
        else
        {
            Fail(1U);
        }

        if (HAL_UART_Receive_IT(&huart1, &rx_byte, 1U) != HAL_OK)
        {
            rx_fault = 1U;
        }

        return;
    }
    if (rx_overflow) { Fail(2U); return; }
    /* Bound parsing work per loop so sensors/buttons continue to run. */
    for (uint32_t budget = 0U; budget < 512U && rx_read != rx_write; ++budget) {
        uint8_t byte = rx_ring[rx_read];
        rx_read = (rx_read + 1U) % RX_SIZE;
        ReceiveByte(byte);
    }
    uint32_t now = HAL_GetTick();
    if (wifi_connected && !wifi_authenticated &&
        (int32_t)(now - login_deadline) >= 0) {
        Fail(9U); /* Server login acknowledgment timeout. */
        return;
    }
    if (state == BOOT && Expired(now)) Command("AT\r\n", AT_CHECK, 2000U);
    else if (state == BACKOFF && Expired(now)) {
    FlushRx();

    /* 리셋 대신 AT 응답부터 다시 확인 */
    Command("AT\r\n", AT_CHECK, 2000U);
    } else if (state != ONLINE && state != BACKOFF && state != BOOT &&
               state != DISABLED && Expired(now)) Fail(3U);
    if (state == ONLINE && tx_count) {
        char command[32];
        snprintf(command, sizeof command, "AT+CIPSEND=%u\r\n",
                 (unsigned)strlen(tx_queue[tx_read]));
        Command(command, PROMPT, 2000U);
    }
}
