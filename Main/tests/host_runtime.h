#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define EXPORT __declspec(dllexport)
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
void *memset(void *d,int c,size_t n) { volatile unsigned char *p=d; while(n--)*p++=c; return d; }
void *memcpy(void *d,const void *s,size_t n) { unsigned char *p=d; const unsigned char *q=s; while(n--)*p++=*q++;return d; }
void *memmove(void *d,const void *s,size_t n) { unsigned char *p=d;const unsigned char *q=s;if(p<q)while(n--)*p++=*q++;else while(n){--n;p[n]=q[n];}return d; }
int memcmp(const void *a,const void *b,size_t n) { const unsigned char *p=a,*q=b;while(n--){if(*p!=*q)return *p-*q;++p;++q;}return 0; }
int abs(int a) { return a<0?-a:a; }
typedef int esp_err_t;
typedef int portMUX_TYPE;
typedef void *SemaphoreHandle_t;
typedef int nvs_handle_t;
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NVS_NOT_FOUND 0x1102
#define NVS_READWRITE 1
#define NVS_READONLY 0
#define pdTRUE 1
#define portMAX_DELAY 0xffffffffU
#define ESP_LOGW(...) ((void)0)
#define SURFACE_FAULT 1
#define CONFIG_TP_ENABLE_SLEEP_MODE 1
static uint32_t test_generation=1, wake_count;
uint32_t input_generation(void) { return test_generation; }
void input_wake_sender(void) { ++wake_count; }
void input_wake_parser(void) { ++wake_count; }
void input_recover(void) { ++test_generation; }
void input_source_recover(const char *reason) { (void)reason; input_recover(); }
static int surface_fault;
int cs40l25_surface_get_state(void) { return surface_fault; }
bool device_config_parser_boundary(void);
static bool writer_busy;
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
SemaphoreHandle_t xSemaphoreCreateBinary(void) { return (void *)2; }
int xSemaphoreTake(SemaphoreHandle_t h,uint32_t wait) {
    (void)wait;
    if(h==(void *)2) { device_config_parser_boundary(); return 1; }
    if(writer_busy)return 0;writer_busy=true;return 1;
}
void xSemaphoreGive(SemaphoreHandle_t h) { if(h==(void *)1)writer_busy=false; }
static uint8_t nvs_blob[60], nvs_pending[60];
static size_t nvs_size, nvs_pending_size;
static bool fail_commit;
esp_err_t nvs_open(const char *s,int mode,nvs_handle_t *h) { (void)s;(void)mode;*h=1;return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *v,size_t n) { (void)h;(void)k;memcpy(nvs_pending,v,n);nvs_pending_size=n;return ESP_OK; }
esp_err_t nvs_commit(nvs_handle_t h) { (void)h;if(fail_commit)return ESP_FAIL;memcpy(nvs_blob,nvs_pending,nvs_pending_size);nvs_size=nvs_pending_size;return ESP_OK; }
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *v,size_t *n) { (void)h;(void)k;if(!nvs_size)return ESP_ERR_NVS_NOT_FOUND;if(*n<nvs_size)return ESP_FAIL;memcpy(v,nvs_blob,nvs_size);*n=nvs_size;return ESP_OK; }
void nvs_close(nvs_handle_t h) { (void)h; }
esp_err_t nvs_read_int(const char *k,int32_t *v) { (void)k;(void)v;return ESP_ERR_NVS_NOT_FOUND; }
uint8_t ptp_button_press_threshold,click_light_weight_threshold,click_midium_weight_threshold,click_strong_weight_threshold;
int32_t current_mode;
uint8_t current_tp_mode;
int _fltused = 0;
