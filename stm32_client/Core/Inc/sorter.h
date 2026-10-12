#ifndef SORTER_H
#define SORTER_H

#include <stdint.h>

typedef enum
{
    PRODUCT_UNKNOWN,
    PRODUCT_NORMAL,
    PRODUCT_DEFECT
} ProductResult;

typedef enum
{
    SORTER_ERR_NONE,
    SORTER_ERR_NO_RESULT,
    SORTER_ERR_EXIT_OCCUPIED,
    SORTER_ERR_EXIT_TIMEOUT,
    SORTER_ERR_EXIT_STUCK,
    SORTER_ERR_ROUTE_MISMATCH,
    SORTER_ERR_QUEUE_FULL,
    SORTER_ERR_TOO_CLOSE
} SorterError;

void Sorter_Init(void);
uint8_t Sorter_SetResult(ProductResult result);
extern volatile uint32_t sorter_results_accepted;
extern volatile uint32_t sorter_results_rejected;
extern volatile uint32_t sorter_results_consumed;
void Sorter_Update(void);

void Sorter_Stop(void);
uint8_t Sorter_IsStopped(void);
uint8_t Sorter_TryReset(void);

SorterError Sorter_GetError(void);
uint8_t Sorter_GetPendingCount(void);
uint32_t Sorter_GetNormalCount(void);
uint32_t Sorter_GetDefectCount(void);

ProductResult Sorter_GetCurrentResult(void);
const char *Sorter_GetPhaseText(void);

#endif