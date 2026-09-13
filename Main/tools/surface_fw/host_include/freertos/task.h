#pragma once
#include "FreeRTOS.h"
void vTaskDelay(TickType_t ticks);
void vTaskDelete(void *task);
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned int stack,
                            void *arg, unsigned int priority, void *handle, int core);
