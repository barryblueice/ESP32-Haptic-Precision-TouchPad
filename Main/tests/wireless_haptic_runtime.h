#include <limits.h>
#include <xmmintrin.h>
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
typedef int esp_now_send_status_t;
typedef int esp_now_send_info_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_NOW_SEND_SUCCESS 0
#define ESP_NOW_SEND_FAIL 1
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_LOGD(...) ((void)0)
#define ESP_ERROR_CHECK(e) ((void)(e))
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define CONFIG_PTP_SIMULATED_MOUSE_MODE 1
#define SENSITIVITY 1
#define BSP_STATUS_OK 0
#define BSP_STATUS_FAIL 1
#define SURFACE_FW_NUM_WAVES 78
static unsigned parser_budget, wifi_budget;
static void (*parser_hook)(void), (*wifi_hook)(void);
static uint32_t now;
static int64_t esp_timer_get_time(void) { return (int64_t)now * 1000; }
static void xTaskNotifyGive(TaskHandle_t task) { (void)task; }
static TaskHandle_t xTaskGetCurrentTaskHandle(void) { return (void *)1; }
static void ulTaskNotifyTake(int clear, unsigned timeout) { (void)clear;(void)timeout;if(wifi_hook)wifi_hook(); }
static void vTaskDelay(unsigned ticks) { now += ticks; }
static int esp_timer_stop(esp_timer_handle_t timer) { (void)timer;return 0; }
static int esp_timer_start_once(esp_timer_handle_t timer, uint64_t us) { (void)timer;(void)us;return 0; }
static int esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) { (void)args;*out=(void*)1;return 0; }
static const char *esp_err_to_name(int err) { (void)err;return "test"; }
float sqrtf(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }
float fabsf(float x) { return x < 0 ? -x : x; }
int _fltused = 0;
/* PRODUCTION_TYPES */
static input_frame_t raw_queue[16];
static unsigned raw_count;
static QueueHandle_t tp_data_queue;
static QueueHandle_t xQueueCreate(unsigned n, unsigned size) { (void)n;(void)size;raw_count=0;return (void*)1; }
static int xQueueSend(QueueHandle_t q,const void *data,unsigned ticks) {
    (void)q;(void)ticks;if(raw_count==16)return 0;raw_queue[raw_count++]=*(const input_frame_t*)data;return pdPASS;
}
static int xQueueReceive(QueueHandle_t q,void *data,unsigned ticks) {
    (void)q;(void)ticks;if(!raw_count)return 0;*(input_frame_t*)data=raw_queue[0];--raw_count;
    memmove(raw_queue,raw_queue+1,raw_count*sizeof(raw_queue[0]));return pdPASS;
}
int32_t current_mode;
uint8_t current_tp_mode, ptp_button_press_threshold=2;
uint8_t click_light_weight_threshold=80, click_midium_weight_threshold=100, click_strong_weight_threshold=130;
static device_config_t config;
static bool fail_mode, config_change;
static unsigned mode_writes;
static uint16_t device_config_x_max(void) { return 2302; }
static uint16_t device_config_y_max(void) { return 1532; }
static uint8_t device_config_rotation(void) { return 0; }
static void device_config_get(device_config_t *out) { *out=config; }
static bool device_config_parser_boundary(void) {
    if(config_change){config_change=false;input_source_recover("config");}return false;
}
static int touchpad_mode_set(bool ptp) { (void)ptp;++mode_writes;return fail_mode?ESP_FAIL:ESP_OK; }
static surface_haptic_runtime_t haptic;
static unsigned cancellations, played_press, played_release, hardware_errors;
static uint8_t mapped_press, mapped_release;
static uint8_t ptp_haptic_click_intensity_get(void) { return config.bytes[0]; }
static void cs40l25_surface_cancel_click(void) { ++cancellations;surface_runtime_cancel(&haptic); }
static void cs40l25_surface_button_update(bool down,uint8_t setting) { surface_runtime_button(&haptic,down,setting,now); }
static surface_haptic_state_t cs40l25_surface_get_state(void) { return haptic.state; }
static uint32_t bsp_dut_apply_haptic_mapping(uint8_t press,uint8_t release,uint32_t a,uint32_t b,bool gpio) {
    if(a||b||gpio)++hardware_errors;mapped_press=press;mapped_release=release;return BSP_STATUS_OK;
}
static uint32_t bsp_dut_trigger_haptic(uint8_t index,uint32_t duration) {
    if(duration||index>=78)++hardware_errors;return BSP_STATUS_OK;
}
static uint8_t receiver_mac[6]={1,2,3,4,5,6};
static esp_err_t esp_now_send(const uint8_t *mac,const uint8_t *packet,unsigned length);
static void wireless_make_heartbeat(wireless_msg_t *packet) { *packet=(wireless_msg_t){.type=ALIVE_MODE}; }
