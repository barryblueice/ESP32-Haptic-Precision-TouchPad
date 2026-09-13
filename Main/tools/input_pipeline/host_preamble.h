#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>
#include <inttypes.h>
#define EXPORT __declspec(dllexport)
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
#define CONFIG_PTP_SIMULATED_MOUSE_MODE 1
#define CONFIG_BLE_ENABLE_PTP_MODE 0
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef unsigned TickType_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
static int check_error;
#define ESP_ERROR_CHECK(e) do { check_error = (e); if (check_error) return; } while (0)
void *memset(void *dest, int value, size_t count) {
    volatile unsigned char *d = dest; while (count--) *d++ = (unsigned char)value; return dest;
}
void *memcpy(void *dest, const void *src, size_t count) {
    unsigned char *d = dest; const unsigned char *s = src;
    while (count--) *d++ = *s++; return dest;
}
void *memmove(void *dest, const void *src, size_t count) {
    unsigned char *d = dest; const unsigned char *s = src;
    if (d < s) while (count--) *d++ = *s++;
    else while (count) { --count; d[count] = s[count]; }
    return dest;
}
