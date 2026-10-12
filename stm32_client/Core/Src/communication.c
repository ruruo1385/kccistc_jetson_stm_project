#include "communication.h"
#include "wifi.h"
#include "wifi_config.h"
#include <stdio.h>
#include <string.h>
#include "lcd.h"
static void ReceiveVerdict(const char *line)
{
    /* 서버가 붙인 [QC_CHK] 같은 발신자 표시 제거 */
    if (line[0] == '[')
    {
        const char *end = strchr(line, ']');
        if (end == NULL)
            return;

        line = end + 1;
    }

    if (strcmp(line, "PASS") == 0 ||
        strcmp(line, "NORMAL") == 0 ||
        strncmp(line, "IMAGE@PASS@", 11U) == 0)
    {
        if (Sorter_SetResult(PRODUCT_NORMAL))
            LCD_SetReceivedResult(1U);
    }
    else if (strcmp(line, "FAIL") == 0 ||
             strcmp(line, "DEFECT") == 0 ||
             strncmp(line, "IMAGE@FAIL@", 11U) == 0)
    {
        if (Sorter_SetResult(PRODUCT_DEFECT))
            LCD_SetReceivedResult(0U);
    }
}

void Communication_Init(void) { Wifi_Init(ReceiveVerdict); }
void Communication_Update(void) { Wifi_Update(); }

/* These callbacks must have exactly one definition in the entire project. */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) { Wifi_RxComplete(huart); }
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) { Wifi_UartError(huart); }

static void SendReport(const char *message)
{
    if (strcmp(WIFI_REPORT_TO, "X") == 0) return;
    char routed[80];
    int count = snprintf(routed, sizeof routed, "[%s]%s", WIFI_REPORT_TO, message);
    if (count > 0 && (size_t)count < sizeof routed) Wifi_SendLine(routed);
}

static const char *ErrorName(SorterError error)
{
    switch (error) {
    case SORTER_ERR_NO_RESULT: return "NO_RESULT";
    case SORTER_ERR_EXIT_OCCUPIED: return "EXIT_OCCUPIED";
    case SORTER_ERR_EXIT_TIMEOUT: return "EXIT_TIMEOUT";
    case SORTER_ERR_EXIT_STUCK: return "EXIT_STUCK";
    case SORTER_ERR_ROUTE_MISMATCH: return "ROUTE_MISMATCH";
    case SORTER_ERR_QUEUE_FULL: return "QUEUE_FULL";
    case SORTER_ERR_TOO_CLOSE: return "TOO_CLOSE";
    default: return "NONE";
    }
}

void Communication_ReportDone(ProductResult result, uint32_t normal_count, uint32_t defect_count)
{
    if (result != PRODUCT_NORMAL && result != PRODUCT_DEFECT) return;
    char message[80];
    snprintf(message, sizeof message, "DONE,%s,%lu,%lu\n",
             result == PRODUCT_NORMAL ? "NORMAL" : "DEFECT",
             (unsigned long)normal_count, (unsigned long)defect_count);
    SendReport(message);
}

void Communication_ReportError(SorterError error)
{
    char message[80];
    snprintf(message, sizeof message, "ERROR,%s\n", ErrorName(error));
    SendReport(message);
}

void Communication_ReportState(const char *state)
{
    if (!state || (strcmp(state, "RUN") && strcmp(state, "MANUAL_STOP") &&
                   strcmp(state, "ERROR_STOP") && strcmp(state, "RESTART"))) return;
    char message[80];
    snprintf(message, sizeof message, "STATE,%s\n", state);
    SendReport(message);
}
