"""Test the production touch IRQ reader with an active-low controller stub."""
import ctypes
import json
import re
import subprocess
import sys
from run_host_tests import ROOT, body

runtime = (ROOT/'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime += r'''
typedef int esp_err_t;
typedef int BaseType_t;
typedef void *TaskHandle_t;
typedef void *esp_timer_handle_t;
typedef struct { void (*callback)(void *); const char *name; } esp_timer_create_args_t;
typedef struct { uint64_t pin_bit_mask; int mode, intr_type, pull_up_en, pull_down_en; } gpio_config_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define portMAX_DELAY 0xffffffffU
#define pdMS_TO_TICKS(n) (n)
#define GPIO_MODE_INPUT 1
#define GPIO_INTR_DISABLE 0
#define GPIO_INTR_NEGEDGE 2
#define GPIO_PULLUP_DISABLE 0
#define GPIO_PULLDOWN_DISABLE 0
#define TP_INT_GPIO 4
#define IRAM_ATTR
#define ESP_LOGI(...) ((void)0)
#define portYIELD_FROM_ISR() ((void)0)
static unsigned errors, pending_reports, reads, captures, failed, notifications, delays, budget;
static bool fail_read, edge_on_enable;
static void (*registered_isr)(void *);
static esp_timer_handle_t timeout_watchdog_timer;
static int dev_handle;
#define ESP_ERROR_CHECK(e) do { if ((e) != ESP_OK) ++errors; } while (0)
static void watchdog_timeout_callback(void *arg) { (void)arg; }
static int esp_timer_create(const esp_timer_create_args_t *args,esp_timer_handle_t *out) { (void)args;*out=(void*)1;return 0; }
static int xTaskCreatePinnedToCore(void (*fn)(void*),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out,int core) {
    (void)fn;(void)name;(void)stack;(void)arg;(void)priority;(void)core;*out=(void*)1;return pdPASS;
}
static int gpio_config(const gpio_config_t *config) { (void)config;return 0; }
static int gpio_isr_handler_add(int pin,void (*handler)(void*),void *arg) { (void)pin;(void)arg;registered_isr=handler;return 0; }
static int gpio_set_intr_type(int pin,int type) { (void)pin;(void)type;return 0; }
static int gpio_intr_enable(int pin) { (void)pin;if(edge_on_enable)registered_isr(0);return 0; }
static int gpio_get_level(int pin) { (void)pin;return pending_reports?0:1; }
static void xTaskNotifyGive(TaskHandle_t task) { (void)task;++notifications; }
static void vTaskNotifyGiveFromISR(TaskHandle_t task,BaseType_t *wake) { xTaskNotifyGive(task);*wake=0; }
static unsigned ulTaskNotifyTake(int clear,unsigned wait) { (void)clear;(void)wait;unsigned n=notifications;notifications=0;return n; }
static void vTaskDelay(unsigned ticks) { delays+=ticks; }
static void tp_modern_sleep_init(void) {}
static void tp_modern_sleep_record_activity(void) {}
static void tp_modern_sleep_signal_activity_from_isr(void) {}
static uint32_t input_generation(void) { return 7; }
static int64_t esp_timer_get_time(void) { return 1000; }
static int i2c_master_receive(int dev,uint8_t *data,size_t size,unsigned timeout) {
    (void)dev;(void)timeout;++reads;
    if(!pending_reports){++errors;return ESP_FAIL;}
    if(fail_read)return ESP_FAIL;
    --pending_reports;memset(data,0,size);data[0]=0x40;return ESP_OK;
}
static void input_capture(const uint8_t *data,bool success,uint32_t gen,uint32_t time) {
    (void)data;if(gen!=7||time!=1)++errors;
    if(success)++captures;else ++failed;
}
'''
code = runtime + '\n' + body(ROOT/'main/GPIO/irq_tp_int.c').replace('while (1)', 'while (budget-- > 0)')
code += r'''
static void reset_test(unsigned pending) {
    errors=reads=captures=failed=notifications=delays=0;
    pending_reports=pending;fail_read=edge_on_enable=false;
}
EXPORT int check_irq_pending_before_enable(void) {
    reset_test(1);irq_int_init();CHECK(notifications==1);
    budget=1;tp_i2c_int_task(0);CHECK(reads==1&&captures==1&&!pending_reports&&!errors);return 0;
}
EXPORT int check_irq_idle_and_duplicate_wake(void) {
    reset_test(0);irq_int_init();CHECK(!notifications);
    notifications=2;budget=1;tp_i2c_int_task(0);CHECK(!reads&&!errors);return 0;
}
EXPORT int check_irq_coalesced_reports(void) {
    reset_test(3);irq_int_init();budget=1;tp_i2c_int_task(0);
    CHECK(reads==3&&captures==3&&!pending_reports&&!errors);return 0;
}
EXPORT int check_irq_enable_edge_race(void) {
    reset_test(1);edge_on_enable=true;irq_int_init();CHECK(notifications==2);
    budget=2;tp_i2c_int_task(0);CHECK(reads==1&&captures==1&&!errors);return 0;
}
EXPORT int check_irq_batch_and_failure_retry(void) {
    reset_test(10);irq_int_init();budget=1;tp_i2c_int_task(0);
    CHECK(reads==8&&pending_reports==2&&notifications&&delays);
    budget=1;tp_i2c_int_task(0);CHECK(reads==10&&!pending_reports&&!errors);
    reset_test(1);irq_int_init();fail_read=true;budget=1;tp_i2c_int_task(0);
    CHECK(reads==1&&failed==1&&!captures&&notifications&&delays>=10);
    fail_read=false;budget=1;tp_i2c_int_task(0);CHECK(captures==1&&!pending_reports&&!errors);return 0;
}
'''
out = ROOT/'build/input-irq-host-tests'
out.mkdir(parents=True, exist_ok=True)
c, dll = out/'checks.c', out/'checks.dll'
c.write_text(code, encoding='utf-8')
subprocess.run([sys.argv[1], '-std=c11', '-O1', '-fno-builtin', '-mno-stack-arg-probe',
    '-Werror=implicit-function-declaration', '-shared', '-nostdlib', '-fuse-ld=lld',
    '-Wl,/noentry', '-Wl,/nodefaultlib', str(c), '-o', str(dll)], check=True)
lib = ctypes.CDLL(str(dll))
names = re.findall(r'EXPORT int (check_\w+)\(void\)', code)
for name in names:
    line = getattr(lib, name)()
    if line:
        raise AssertionError(f'{name}: {code.splitlines()[line-1]} (line {line})')
    print(name + ': passed')
(out/'result.json').write_text(json.dumps({'passed': True, 'cases': names}, indent=2)+'\n', encoding='utf-8')
