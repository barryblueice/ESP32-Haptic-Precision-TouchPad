#pragma once
typedef void *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline int xSemaphoreTake(SemaphoreHandle_t h, unsigned int ticks) { (void)h; (void)ticks; return 1; }
static inline void xSemaphoreGive(SemaphoreHandle_t h) { (void)h; }
