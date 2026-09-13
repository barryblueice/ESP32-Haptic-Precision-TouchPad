#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define portMAX_DELAY 0xffffffffU
#define pdTRUE 1
