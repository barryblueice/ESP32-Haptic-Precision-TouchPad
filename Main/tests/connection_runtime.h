/* Hardware boundaries for the real connection manager (no selector stub). */
typedef void *SemaphoreHandle_t;
#define ESP_ERR_INVALID_ARG 0x102
#define RTC_NOINIT_ATTR
#define ESP_RST_SW 3
static int reset_reason, saved_manual_mode;
static bool fail_manual_save;
static unsigned restart_count;
static void esp_restart(void) { ++restart_count; }
static unsigned route_lock_depth, route_lock_errors;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)17; }
static int xSemaphoreTake(SemaphoreHandle_t h, unsigned wait)
{ (void)h;(void)wait;if(route_lock_depth++)++route_lock_errors;return 1; }
static void xSemaphoreGive(SemaphoreHandle_t h)
{ (void)h;if(!route_lock_depth)++route_lock_errors;else --route_lock_depth; }
static int esp_reset_reason(void) { return reset_reason; }
static int nvs_write_int(const char *key,int value)
{ (void)key;if(fail_manual_save)return ESP_FAIL;saved_manual_mode=value;return ESP_OK; }
