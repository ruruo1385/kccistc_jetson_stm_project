#include "lcd.h"
#include "main.h"
#include "sorter.h"
#include "wifi.h"
#include "tracking.h"

#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;

// 흔한 PCF8574 LCD 변환 보드:
// P0=RS, P1=RW, P2=E, P3=백라이트, P4~P7=데이터
#define LCD_RS          0x01U
#define LCD_ENABLE      0x04U
#define LCD_BACKLIGHT   0x08U

#define LCD_COLUMNS     16U
#define LCD_REFRESH_MS  100U
// TEMP photo display only: set to 0 after taking screenshots.
#define LCD_PHOTO_DEMO  0

static uint16_t lcd_address;
static uint8_t lcd_ready;
static uint32_t last_update;
static uint8_t next_row;
static uint8_t reset_blocked_visible;
static uint32_t reset_blocked_at;
static char reset_blocked_reason[17];

void LCD_ClearResetBlocked(void)
{
    reset_blocked_visible = 0U;
}

void LCD_ShowResetBlocked(void)
{
    if (!wifi_connected)
        snprintf(reset_blocked_reason, sizeof(reset_blocked_reason), "WIFI NOT READY");
    else if (!wifi_authenticated)
        snprintf(reset_blocked_reason, sizeof(reset_blocked_reason), "WIFI NOT READY");
    else
        snprintf(reset_blocked_reason, sizeof(reset_blocked_reason),
                 "CLEAR SENSORS");
    reset_blocked_at = HAL_GetTick();
    reset_blocked_visible = 1U;
}

#define LCD_RESULT_HOLD_MS 2000U
static const char *last_received = "WAIT";
static uint32_t result_received_at;
static uint8_t result_visible;

void LCD_SetReceivedResult(uint8_t is_normal)
{
    last_received = is_normal ? "NORMAL" : "DEFECT";
    result_received_at = HAL_GetTick();
    result_visible = 1U;
}

static char previous[2][LCD_COLUMNS + 1U];

// LCD 주소와 연결 상태를 조사식에서 확인
volatile uint8_t lcd_address_7bit = 0U;
volatile uint8_t lcd_connected = 0U;

static uint8_t LCD_SendNibble(uint8_t nibble)
{
    uint8_t data[3];

    nibble |= LCD_BACKLIGHT;

    data[0] = nibble;
    data[1] = nibble | LCD_ENABLE;
    data[2] = nibble;

    return HAL_I2C_Master_Transmit(
        &hi2c1,
        lcd_address,
        data,
        sizeof(data),
        10U
    ) == HAL_OK;
}

static uint8_t LCD_SendByte(uint8_t value, uint8_t is_data)
{
    uint8_t rs = is_data ? LCD_RS : 0U;

    uint8_t high = (value & 0xF0U)
                   | rs | LCD_BACKLIGHT;

    uint8_t low = ((value << 4U) & 0xF0U)
                  | rs | LCD_BACKLIGHT;

    uint8_t data[6] =
    {
        high,
        high | LCD_ENABLE,
        high,
        low,
        low | LCD_ENABLE,
        low
    };

    return HAL_I2C_Master_Transmit(
        &hi2c1,
        lcd_address,
        data,
        sizeof(data),
        10U
    ) == HAL_OK;
}

static uint8_t LCD_FindAddress(void)
{
    // PCF8574 계열 주소 범위 검색.
    // 현재 I2C1에 LCD만 연결한 구성 기준.
    for (uint8_t group = 0U; group < 2U; group++)
    {
        uint8_t first = group == 0U ? 0x20U : 0x38U;

        for (uint8_t offset = 0U; offset < 8U; offset++)
        {
            uint8_t address = first + offset;

            if (HAL_I2C_IsDeviceReady(
                    &hi2c1,
                    (uint16_t)address << 1U,
                    1U,
                    2U) == HAL_OK)
            {
                lcd_address_7bit = address;
                lcd_address = (uint16_t)address << 1U;
                return 1U;
            }
        }
    }

    return 0U;
}

uint8_t LCD_Init(void)
{
    lcd_ready = 0U;
    lcd_connected = 0U;
    lcd_address_7bit = 0U;
    result_visible = 0U;
    last_received = "WAIT";

    // LCD 전원 안정화
    HAL_Delay(50);

    if (!LCD_FindAddress())
    {
        return 0U;
    }

    // 4비트 모드 초기화 순서
    if (!LCD_SendNibble(0x30U))
    {
        return 0U;
    }

    HAL_Delay(5);

    if (!LCD_SendNibble(0x30U))
    {
        return 0U;
    }

    HAL_Delay(1);

    if (!LCD_SendNibble(0x30U))
    {
        return 0U;
    }

    HAL_Delay(1);

    if (!LCD_SendNibble(0x20U))
    {
        return 0U;
    }

    HAL_Delay(1);

    // 4비트, 2줄, 5x8 글꼴
    if (!LCD_SendByte(0x28U, 0U))
    {
        return 0U;
    }

    // 화면 켜기, 커서 끄기
    if (!LCD_SendByte(0x0CU, 0U))
    {
        return 0U;
    }

    // 문자 출력 후 커서 오른쪽 이동
    if (!LCD_SendByte(0x06U, 0U))
    {
        return 0U;
    }

    // 초기화 때만 화면 지우기
    if (!LCD_SendByte(0x01U, 0U))
    {
        return 0U;
    }

    HAL_Delay(2);

    memset(previous, 0, sizeof(previous));

    next_row = 0U;
    last_update = HAL_GetTick();

    lcd_ready = 1U;
    lcd_connected = 1U;
    return 1U;
}

static uint8_t LCD_WriteRow(uint8_t row, const char *text)
{
    uint8_t command = row == 0U ? 0x80U : 0xC0U;

    if (!LCD_SendByte(command, 0U))
    {
        return 0U;
    }

    // 한 줄을 한 번의 I2C 전송으로 출력
    uint8_t data[LCD_COLUMNS * 6U];

    for (uint8_t i = 0U; i < LCD_COLUMNS; i++)
    {
        uint8_t value = (uint8_t)text[i];

        uint8_t high = (value & 0xF0U)
                       | LCD_RS | LCD_BACKLIGHT;

        uint8_t low = ((value << 4U) & 0xF0U)
                      | LCD_RS | LCD_BACKLIGHT;

        uint8_t index = i * 6U;

        data[index] = high;
        data[index + 1U] = high | LCD_ENABLE;
        data[index + 2U] = high;

        data[index + 3U] = low;
        data[index + 4U] = low | LCD_ENABLE;
        data[index + 5U] = low;
    }

    return HAL_I2C_Master_Transmit(
        &hi2c1,
        lcd_address,
        data,
        sizeof(data),
        15U
    ) == HAL_OK;
}

static void LCD_PadLine(
    char output[LCD_COLUMNS + 1U],
    const char *input)
{
    memset(output, ' ', LCD_COLUMNS);
    output[LCD_COLUMNS] = '\0';

    size_t length = strlen(input);

    if (length > LCD_COLUMNS)
    {
        length = LCD_COLUMNS;
    }

    memcpy(output, input, length);
}

static const char *LCD_ErrorText(SorterError error)
{
    switch (error)
    {
        case SORTER_ERR_NO_RESULT:
            return "NO RESULT";

        case SORTER_ERR_EXIT_OCCUPIED:
            return "EXIT OCCUPIED";

        case SORTER_ERR_EXIT_TIMEOUT:
            return "EXIT TIMEOUT";

        case SORTER_ERR_EXIT_STUCK:
            return "EXIT STUCK";

        case SORTER_ERR_ROUTE_MISMATCH:
            return "WRONG EXIT";

        case SORTER_ERR_QUEUE_FULL:
            return "QUEUE FULL";

        case SORTER_ERR_TOO_CLOSE:
            return "TOO CLOSE";

        default:
            return "UNKNOWN";
    }
}

static const char *LCD_WifiErrorText(uint32_t error)
{
    switch (error)
    {
        case 1U: return "UART RX";
        case 2U: return "RX OVERFLOW";
        case 3U: return "AT TIMEOUT";
        case 4U: return "AT ERROR";
        case 5U: return "LINK LOST";
        case 6U: return "WIFI CONFIG";
        case 7U: return "RX FRAME";
        case 8U: return "UART TX";
        case 9U: return "AUTH TIMEOUT";
        default: return "WIFI UNKNOWN";
    }
}

void LCD_Update(void)
{
    if (!lcd_ready)
    {
        return;
    }

    uint32_t now = HAL_GetTick();

    if (now - last_update < LCD_REFRESH_MS)
    {
        return;
    }

    last_update = now;

    char first[40];
    char second[40];

    SorterError error = Sorter_GetError();

    // A start message is unavailable: show each new result briefly.
    if (result_visible &&
        (now - result_received_at >= LCD_RESULT_HOLD_MS ||
         error != SORTER_ERR_NONE || !Wifi_IsReady() || Sorter_IsStopped()))
    {
        result_visible = 0U;
        last_received = "WAIT";
    }

    if (reset_blocked_visible && now - reset_blocked_at < 3000U)
    {
        snprintf(first, sizeof(first), "RESET BLOCKED");
        snprintf(second, sizeof(second), "%s", reset_blocked_reason);
    }
    else if (error != SORTER_ERR_NONE)
    {
        snprintf(
            first,
            sizeof(first),
            "ERR:%s",
            LCD_ErrorText(error)
        );

        snprintf(
            second,
            sizeof(second),
            "PRESS TO RESET"
        );
    }
    else if (Sorter_IsStopped() && wifi_stop_error != 0U)
    {
        snprintf(first, sizeof(first), "ERR:%s", LCD_WifiErrorText(wifi_stop_error));
        snprintf(second, sizeof(second), "PRESS TO RESET");
    }
    else if (!Wifi_IsReady())
    {
        snprintf(first, sizeof(first), "WIFI NOT READY");
        snprintf(second, sizeof(second), "PLEASE WAIT");
    }
    else if (Sorter_IsStopped())
    {
        if (Tracking_IsDetected1() || Tracking_IsDetected2() || Tracking_IsDetected3())
        {
            snprintf(first, sizeof(first), "CLEAR SENSORS");
            snprintf(second, sizeof(second), "PRESS TO RESET");
        }
        else
        {
            snprintf(first, sizeof(first), "STOP");
            snprintf(second, sizeof(second), "PRESS TO RESUME");
        }
    }
    else
    {
        snprintf(
            first,
            sizeof(first),
            "%s",
            last_received
        );

        uint32_t normal = Sorter_GetNormalCount();
        uint32_t defect = Sorter_GetDefectCount();

        if (normal > 9999U)
        {
            normal = 9999U;
        }

        if (defect > 9999U)
        {
            defect = 9999U;
        }

        snprintf(
            second,
            sizeof(second),
            (now / 2000U) % 2U == 0U ? "NORMAL:%lu" : "DEFECT:%lu",
            (unsigned long)((now / 2000U) % 2U == 0U ? normal : defect)
        );
    }

#if LCD_PHOTO_DEMO
    // Simulated display examples; never modify sorter counters or state.
    if ((now / 8000U) % 2U == 0U)
    {
        snprintf(first, sizeof(first), "ERR:WRONG EXIT");
        snprintf(second, sizeof(second), "PRESS TO RESET");
    }
    else
    {
        snprintf(first, sizeof(first), "NORMAL:2");
        snprintf(second, sizeof(second), "DEFECT:3");
    }
#endif

    char lines[2][LCD_COLUMNS + 1U];

    LCD_PadLine(lines[0], first);
    LCD_PadLine(lines[1], second);

    // 100msごとに最大1行。変更のない行は送信しない。
    for (uint8_t attempt = 0U; attempt < 2U; attempt++)
    {
        uint8_t row = next_row;
        next_row ^= 1U;

        if (strcmp(previous[row], lines[row]) != 0)
        {
            if (!LCD_WriteRow(row, lines[row]))
            {
                // LCD故障時も分류 제어는 계속 실행
                lcd_ready = 0U;
                lcd_connected = 0U;
                return;
            }

            memcpy(
                previous[row],
                lines[row],
                LCD_COLUMNS + 1U
            );

            break;
        }
    }
}