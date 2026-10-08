/* Deterministic RTOS/USB boundary. TinyUSB callbacks execute in FIFO order. */
typedef struct { unsigned count, size; uint8_t data[16][128]; } test_queue_t;
static test_queue_t queues[4];
static unsigned queue_count, usb_budget;
static int config_budget; /* Both nested worker loops consume this budget. */
static QueueHandle_t tp_data_queue;
static QueueHandle_t xQueueCreate(unsigned n, unsigned size)
{
    (void)n; test_queue_t *q=&queues[queue_count++];memset(q,0,sizeof(*q));q->size=size;return q;
}
static int xQueueSend(QueueHandle_t handle,const void *data,unsigned ticks)
{
    (void)ticks;test_queue_t *q=handle;if(q->count==16)return 0;
    memcpy(q->data[q->count++],data,q->size);return pdPASS;
}
static int xQueueReceive(QueueHandle_t handle,void *data,unsigned ticks)
{
    (void)ticks;test_queue_t *q=handle;if(!q->count)return 0;
    memcpy(data,q->data[0],q->size);--q->count;memmove(q->data,q->data+1,q->count*128);return pdPASS;
}
static int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out)
{ (void)fn;(void)name;(void)stack;(void)arg;(void)priority;*out=(void*)2;return pdPASS; }
static bool device_config_ready(void) { return true; }
static int device_config_set_legacy(unsigned id,unsigned value,bool persist) { (void)id;(void)value;(void)persist;return ESP_OK; }
static uint16_t device_config_save(const device_config_t *c) { (void)c;return RSTP_OK; }
static uint32_t test_capabilities=0x3fff;
static uint32_t device_config_capabilities(void) { return test_capabilities; }
void enter_dfu_mode(void) {}

#define REPORTID_TOUCHPAD 1
#define REPORTID_MOUSE 2
#define REPORTID_MAX_COUNT 3
#define REPORTID_PTPHQA 4
#define REPORTID_FEATURE 5
#define REPORTID_BUTTON_PRESS_THRESHOLD 0x40
#define REPORTID_HAPTIC_INTENSITY 0x41
#define REPORTID_DFU_CMD 0xff
typedef unsigned hid_report_type_t;
enum { HID_REPORT_TYPE_INPUT=1, HID_REPORT_TYPE_OUTPUT=2, HID_REPORT_TYPE_FEATURE=3 };
typedef struct { unsigned id; } tinyusb_event_t;
enum { TINYUSB_EVENT_ATTACHED, TINYUSB_EVENT_DETACHED, TINYUSB_EVENT_SUSPENDED, TINYUSB_EVENT_RESUMED };
typedef struct { unsigned unused; } tusb_desc_interface_t;
typedef struct {
    const char *name; void (*init)(void); void (*reset)(uint8_t);
    uint16_t (*open)(uint8_t,const tusb_desc_interface_t*,uint16_t);
} usbd_class_driver_t;
static bool mounted, suspended, endpoint_busy[3], endpoint_reject[3], in_usb;
static unsigned submitted[3], last_id[3], tx_errors, deferred_count;
static uint8_t last_data[3][64];
static void (*deferred)(void *);
static void *deferred_arg;
static bool defer_reject;
#define USBD_EVENT_FUNC_CALL 8
void tud_event_hook_cb(uint8_t rhport,uint32_t eventid,bool in_isr);
static bool tud_mounted(void) { return mounted; }
static bool tud_suspended(void) { return suspended; }
static void tud_disconnect(void) { mounted=false; }
static bool tud_hid_n_ready(unsigned instance) { return mounted&&!suspended&&!endpoint_busy[instance]; }
static bool tud_hid_n_report(unsigned instance,unsigned id,const void *data,unsigned len)
{
    if(!in_usb)++tx_errors;
    if(!tud_hid_n_ready(instance)||endpoint_reject[instance])return false;
    endpoint_busy[instance]=true;++submitted[instance];last_id[instance]=id;
    memcpy(last_data[instance],data,len);return true;
}
static void usbd_defer_func(void (*fn)(void *),void *arg,bool isr)
{
    if(defer_reject)return;
    if(isr||deferred)++tx_errors;deferred=fn;deferred_arg=arg;++deferred_count;
    tud_event_hook_cb(0,USBD_EVENT_FUNC_CALL,isr);
}
