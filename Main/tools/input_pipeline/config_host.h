/* Deterministic NVS, parser scheduling and USB completion for production services. */
typedef void *SemaphoreHandle_t;
typedef int nvs_handle_t;
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define pdMS_TO_TICKS(ms) (ms)
#define SURFACE_FAULT 7
#define CONFIG_TP_ROTATION_LANDSCAPE_FLIPPED 1
#define CONFIG_TP_ENABLE_SLEEP_MODE 1
#define CONFIG_TP_SLEEP_MODE_TIME_MS 180000
static uint8_t ptp_button_press_threshold, click_light_weight_threshold, click_midium_weight_threshold, click_strong_weight_threshold;
static int current_mode;
static int storage_failure, stores, commits, recoveries, restarts, disconnects, haptic_state;
static bool stored, staged_valid, semaphore_ready, parser_halted;
static uint8_t disk[40], staged[40];
static unsigned semaphore_count;
static int test_steps, send_attempts, completions, config_test_mode;
static uint8_t transmitted[64];
static uint8_t queue_bytes[8][128];
static unsigned queue_count, queue_size;
static int legacy_value[6];
static void (*notify_hook)(void);
static void (*submit_hook)(void);
static bool usb_accept = true, usb_ready = true;
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
#define taskENTER_CRITICAL_ISR(p) ((void)(p))
#define taskEXIT_CRITICAL_ISR(p) ((void)(p))
static int64_t config_clock_us;
static bool timer_failed, sleep_requested;
static uint64_t timer_period_us;
static int64_t esp_timer_get_time(void) { return config_clock_us; }
static esp_err_t esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *handle)
{ (void)args; if(timer_failed) return ESP_ERR_NO_MEM; *handle=(void*)3; return ESP_OK; }
static esp_err_t esp_timer_stop(esp_timer_handle_t handle) { (void)handle; return ESP_OK; }
static esp_err_t esp_timer_start_once(esp_timer_handle_t handle, uint64_t period)
{ (void)handle; timer_period_us=period; return ESP_OK; }
static void cs40l25_surface_set_modern_sleep(bool enabled) { sleep_requested=enabled; }
static bool device_config_parser_boundary(void);
static int cs40l25_surface_get_state(void) { return haptic_state; }
static const char *esp_err_to_name(int e) { (void)e; return "injected"; }
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { ++semaphore_count; return (void *)1; }
static SemaphoreHandle_t xSemaphoreCreateBinary(void) { ++semaphore_count; return (void *)2; }
static int xSemaphoreTake(SemaphoreHandle_t sem, unsigned ticks)
{
    if (sem == (void *)2) {
        if (!semaphore_ready && ticks) parser_halted = device_config_parser_boundary();
        bool result = semaphore_ready; semaphore_ready = false; return result;
    }
    return pdTRUE;
}
static int xSemaphoreGive(SemaphoreHandle_t sem) { if (sem == (void *)2) semaphore_ready = true; return pdTRUE; }
static void input_wake_parser(void) { }
static void input_wake_sender(void) { }
static void input_recover(void) { ++recoveries; }
static esp_err_t nvs_open(const char *space, int mode, nvs_handle_t *h)
{ (void)space; (void)mode; *h = 1; return storage_failure == 1 ? ESP_FAIL : ESP_OK; }
static esp_err_t nvs_get_blob(nvs_handle_t h, const char *key, void *data, size_t *size)
{ (void)h; (void)key; if (!stored) return ESP_ERR_NVS_NOT_FOUND; memcpy(data,disk,40); *size=40; return ESP_OK; }
static esp_err_t nvs_set_blob(nvs_handle_t h, const char *key, const void *data, size_t size)
{
    (void)h; (void)key; ++stores;
    if (storage_failure==2 || size!=40) return ESP_FAIL;
    memcpy(staged,data,40); staged_valid=true; return ESP_OK;
}
static esp_err_t nvs_commit(nvs_handle_t h)
{ (void)h; ++commits; if (storage_failure==3) return ESP_FAIL; if(staged_valid) { memcpy(disk,staged,40); stored=true; } return ESP_OK; }
static void nvs_close(nvs_handle_t h) { (void)h; staged_valid=false; }
static esp_err_t nvs_read_int(const char *key, int32_t *value)
{
    const char *keys[]={"haptic_surface","haptic_click","btn_press_th","clk_th_l","clk_th_m","clk_th_s"};
    for(unsigned i=0;i<6;++i) {
        unsigned n=0; while(key[n] && keys[i][n] && key[n]==keys[i][n]) ++n;
        if(!key[n] && !keys[i][n] && legacy_value[i]>=0) { *value=legacy_value[i]; return ESP_OK; }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}
static QueueHandle_t xQueueCreate(unsigned capacity, unsigned size)
{ if(capacity!=8 || size>128) return NULL; queue_size=size; return queue_bytes; }
static int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks)
{ (void)q;(void)ticks; if(queue_count==8) return 0; memcpy(queue_bytes[queue_count++],item,queue_size); return pdTRUE; }
static int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks)
{
    (void)q;(void)ticks; if(!queue_count) return 0; memcpy(item,queue_bytes[0],queue_size);
    --queue_count; memmove(queue_bytes,queue_bytes+1,queue_count*128); return pdTRUE;
}
static int xTaskCreate(void (*task)(void *), const char *name, unsigned size, void *arg, unsigned priority, TaskHandle_t *handle)
{ (void)task;(void)name;(void)size;(void)arg;(void)priority; *handle=(void*)1; return pdPASS; }
static void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
static unsigned ulTaskNotifyTake(int clear, unsigned ticks)
{ (void)clear;(void)ticks; if(notify_hook) notify_hook(); return 0; }
static void vTaskDelay(unsigned ticks) { (void)ticks; }
static bool tud_hid_n_ready(unsigned instance) { (void)instance; return usb_ready; }
static bool tud_hid_n_report(unsigned instance, unsigned id, const void *data, unsigned size)
{
    ++send_attempts;
    if(instance!=0 || id!=0 || size!=64 || !usb_accept) return false;
    memcpy(transmitted,data,64); if(submit_hook) submit_hook(); return true;
}
static void tud_disconnect(void) { ++disconnects; }
static void esp_restart(void) { ++restarts; }
void enter_dfu_mode(void) { }
