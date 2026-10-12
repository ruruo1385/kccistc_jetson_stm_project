#ifndef COMMUNICATION_H
#define COMMUNICATION_H
#include <stdint.h>
#include "sorter.h"
void Communication_Init(void);
void Communication_Update(void);
void Communication_ReportDone(ProductResult result, uint32_t normal_count, uint32_t defect_count);
void Communication_ReportError(SorterError error);
void Communication_ReportState(const char *state);
#endif
