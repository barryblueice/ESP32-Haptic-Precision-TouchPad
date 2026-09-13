int32_t current_mode;
uint8_t current_tp_mode;
uint8_t ptp_button_press_threshold;
QueueHandle_t tp_data_queue;
static uint32_t clock_ms;
static unsigned raw_count;
static input_frame_t raw_frames[16];
static bool fail_alloc;
static int mode_error, mode_writes, cancellations, notifications, delay_count;
static int test_steps;
static void (*wait_hook)(void);
static void (*mode_hook)(void);
static int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
static const char *esp_err_to_name(int err) { (void)err; return "injected"; }
static QueueHandle_t xQueueCreate(unsigned capacity, unsigned size) {
    if (capacity != 16 || size != sizeof(input_frame_t) || fail_alloc) return NULL;
    return raw_frames;
}
static int xQueueSend(QueueHandle_t q, const void *item, unsigned ticks) {
    if (q != raw_frames || ticks || raw_count == 16) return 0;
    raw_frames[raw_count++] = *(const input_frame_t *)item; return pdPASS;
}
static int xQueueReceive(QueueHandle_t q, void *item, unsigned ticks) {
    (void)ticks;
    if (q != raw_frames || !raw_count) return 0;
    *(input_frame_t *)item = raw_frames[0];
    --raw_count; memmove(raw_frames, raw_frames + 1, raw_count * sizeof(*raw_frames)); return pdPASS;
}
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
static void xTaskNotifyGive(TaskHandle_t task) { (void)task; ++notifications; }
static unsigned ulTaskNotifyTake(int clear, unsigned ticks) {
    (void)clear; (void)ticks; clock_ms += 10;
    if (wait_hook) wait_hook(); return 0;
}
static void vTaskDelay(unsigned ticks) { clock_ms += ticks * 10; ++delay_count; }
static void cs40l25_surface_cancel_click(void) { ++cancellations; }
static esp_err_t touchpad_mode_set(bool ptp) {
    (void)ptp; ++mode_writes; if (mode_hook) mode_hook(); return mode_error;
}
static uint8_t ptp_haptic_click_intensity_get(void) { return 63; }
