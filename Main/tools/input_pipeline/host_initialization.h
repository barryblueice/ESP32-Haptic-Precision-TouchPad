typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
typedef struct { uint64_t pin_bit_mask; int mode, intr_type, pull_up_en, pull_down_en; } gpio_config_t;
#define TP_INT_GPIO 1
#define BOOT_BUTTON_GPIO 2
#define TP_I2C_INT_TASK_STACK_SIZE 6144
#define GPIO_MODE_INPUT 0
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 1
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
static TaskHandle_t tp_task_handle, button_task_handle;
static esp_timer_handle_t timeout_watchdog_timer;
static int init_failure, task_creates, gpio_enables;
static void watchdog_timeout_callback(void *arg) { (void)arg; }
static void tp_i2c_int_task(void *arg) { (void)arg; }
static void button_handler_task(void *arg) { (void)arg; }
static void gpio_isr_handler(void *arg) { (void)arg; }
static void tp_modern_sleep_init(void) {}
static int esp_timer_create(const esp_timer_create_args_t *args, esp_timer_handle_t *timer) {
    (void)args; if (init_failure == 1) return ESP_ERR_NO_MEM; *timer = (void *)1; return ESP_OK;
}
static int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned stack,
    void *arg, unsigned priority, TaskHandle_t *handle, int core) {
    (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)core;
    ++task_creates; if (init_failure == 2) return 0;
    if (handle) *handle = (void *)1; return pdPASS;
}
static int gpio_config(const gpio_config_t *cfg) { (void)cfg; return init_failure == 3 ? ESP_FAIL : ESP_OK; }
static int gpio_isr_handler_add(int gpio, void (*fn)(void *), void *arg) {
    (void)gpio; (void)fn; (void)arg; return init_failure == 4 ? ESP_FAIL : ESP_OK;
}
static int gpio_set_intr_type(int gpio, int type) { (void)gpio; (void)type; return ESP_OK; }
static int gpio_intr_enable(int gpio) { (void)gpio; ++gpio_enables; return ESP_OK; }
static int gpio_install_isr_service(int flags) { (void)flags; return ESP_OK; }
