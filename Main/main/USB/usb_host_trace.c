#include "usb_host_trace.h"

#if USB_HOST_TRACE_ENABLED
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tusb.h"
#include "device/dcd.h"
#include "class/hid/hid_device.h"

/* Link wrappers observe the installed TinyUSB without modifying its sources.
 * Producers only copy bounded records; UART printing stays in a worker task.
 * This is a diagnostic trace, not a bus analyzer or an OS detector. */
#define TRACE_BYTES 32
#define TRACE_DEPTH 128
enum { TRACE_SETUP, TRACE_BUS, TRACE_EP0, TRACE_STALL, TRACE_GET, TRACE_SET };
typedef struct {
    int64_t time_us;
    uint32_t seq, length;
    uint16_t requested;
    uint8_t kind, port, instance, report_id, report_type, copied;
    uint8_t bytes[TRACE_BYTES];
} usb_trace_record_t;

static QueueHandle_t trace_queue;
static atomic_uint trace_sequence, trace_lost;
_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "USB ISR trace counters must be lock-free");

static void trace_send(usb_trace_record_t *record, bool in_isr)
{
    if (!trace_queue) return;
    record->time_us = esp_timer_get_time();
    record->seq = atomic_fetch_add_explicit(&trace_sequence, 1, memory_order_relaxed) + 1;
    BaseType_t ok = in_isr ? xQueueSendFromISR(trace_queue, record, NULL) :
                            xQueueSend(trace_queue, record, 0);
    if (ok != pdTRUE) atomic_fetch_add_explicit(&trace_lost, 1, memory_order_relaxed);
}

static void trace_report(uint8_t kind, uint8_t instance, uint8_t id, uint8_t type,
                         const uint8_t *data, uint16_t length, uint16_t requested)
{
    usb_trace_record_t record = {
        .kind = kind, .instance = instance, .report_id = id, .report_type = type,
        .length = length, .requested = requested,
    };
    record.copied = data ? (length < TRACE_BYTES ? length : TRACE_BYTES) : 0;
    if (record.copied) memcpy(record.bytes, data, record.copied);
    trace_send(&record, false);
}

void __real_dcd_event_handler(dcd_event_t const *event, bool in_isr);
void __wrap_dcd_event_handler(dcd_event_t const *event, bool in_isr)
{
    usb_trace_record_t record = {.port = event->rhport};
    bool capture = true;
    switch (event->event_id) {
    case DCD_EVENT_SETUP_RECEIVED:
        record.kind = TRACE_SETUP;
        record.copied = sizeof(event->setup_received);
        memcpy(record.bytes, &event->setup_received, record.copied);
        break;
    case DCD_EVENT_BUS_RESET:
    case DCD_EVENT_UNPLUGGED:
    case DCD_EVENT_SUSPEND:
    case DCD_EVENT_RESUME:
        record.kind = TRACE_BUS;
        record.report_id = event->event_id;
        break;
    case DCD_EVENT_XFER_COMPLETE:
        capture = (event->xfer_complete.ep_addr & 0x7f) == 0;
        record.kind = TRACE_EP0;
        record.report_id = event->xfer_complete.ep_addr;
        record.report_type = event->xfer_complete.result;
        record.length = event->xfer_complete.len;
        break;
    default:
        capture = false;
        break;
    }
    if (capture) trace_send(&record, in_isr);
    __real_dcd_event_handler(event, in_isr);
}

void __real_dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr);
void __wrap_dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
    if ((ep_addr & 0x7f) == 0) {
        usb_trace_record_t record = {.kind = TRACE_STALL, .port = rhport, .report_id = ep_addr};
        trace_send(&record, xPortInIsrContext());
    }
    __real_dcd_edpt_stall(rhport, ep_addr);
}

uint16_t __real_tud_hid_get_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                                     uint8_t *buffer, uint16_t reqlen);
uint16_t __wrap_tud_hid_get_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                                     uint8_t *buffer, uint16_t reqlen)
{
    uint16_t length = __real_tud_hid_get_report_cb(instance, id, type, buffer, reqlen);
    trace_report(TRACE_GET, instance, id, type, buffer, length, reqlen);
    return length;
}

void __real_tud_hid_set_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                                 const uint8_t *buffer, uint16_t length);
void __wrap_tud_hid_set_report_cb(uint8_t instance, uint8_t id, hid_report_type_t type,
                                 const uint8_t *buffer, uint16_t length)
{
    /* Capture before application filters reject/ignore unfamiliar reports. */
    trace_report(TRACE_SET, instance, id, type, buffer, length, length);
    __real_tud_hid_set_report_cb(instance, id, type, buffer, length);
}

static unsigned trace_u16(const uint8_t *data)
{
    return data[0] | ((unsigned)data[1] << 8);
}

static void trace_print(const usb_trace_record_t *record)
{
    char hex[TRACE_BYTES * 2 + 1];
    static const char digits[] = "0123456789ABCDEF";
    for (unsigned i = 0; i < record->copied; ++i) {
        hex[2*i] = digits[record->bytes[i] >> 4];
        hex[2*i+1] = digits[record->bytes[i] & 15];
    }
    hex[2*record->copied] = 0;
    char detail[240];
    if (record->kind == TRACE_SETUP) {
        const uint8_t *b = record->bytes;
        unsigned value = trace_u16(b + 2);
        const char *hint = "none";
        if (b[0] == 0xa1 && b[1] == 1 && value == 0x03db)
            hint = "apple_property_candidate";
        else if (b[0] == 0x21 && b[1] == 9 && value == 0x0302)
            hint = "apple_mt_candidate_check_payload";
        else if (b[0] == 0x21 && b[1] == 9 && value == 0x0305)
            hint = "ptp_input_mode_candidate_check_payload";
        snprintf(detail, sizeof(detail),
                 "SETUP port=%u bm=%02X req=%02X value=%04X index=%04X length=%u raw=%s hint=%s",
                 record->port, b[0], b[1], value, trace_u16(b + 4), trace_u16(b + 6), hex, hint);
    } else if (record->kind == TRACE_GET || record->kind == TRACE_SET) {
        snprintf(detail, sizeof(detail),
                 "%s instance=%u type=%u id=%02X requested=%u length=%" PRIu32
                 " captured=%u data=%s%s",
                 record->kind == TRACE_GET ? "GET_CB" : "SET_CB", record->instance,
                 record->report_type, record->report_id, record->requested, record->length,
                 record->copied, hex, record->length > record->copied ? " truncated=1" : "");
    } else if (record->kind == TRACE_BUS) {
        const char *name = record->report_id == DCD_EVENT_BUS_RESET ? "RESET" :
                           record->report_id == DCD_EVENT_UNPLUGGED ? "UNPLUG" :
                           record->report_id == DCD_EVENT_SUSPEND ? "SUSPEND" : "RESUME";
        snprintf(detail, sizeof(detail), "%s port=%u", name, record->port);
    } else {
        snprintf(detail, sizeof(detail), "%s port=%u ep=%02X result=%u length=%" PRIu32,
                 record->kind == TRACE_EP0 ? "EP0_COMPLETE" : "EP0_STALL",
                 record->port, record->report_id, record->report_type, record->length);
    }
    // printf("USBTRACE t_us=%" PRIi64 " seq=%" PRIu32 " %s\n", record->time_us, record->seq, detail);
}

static void trace_task(void *arg)
{
    (void)arg;
    unsigned previous_lost = 0;
    // printf("USBTRACE enabled: device=0D00:072C profile=PTP+mouse host_os=unknown; "
    //        "Apple hints are protocol candidates, not OS identification; payload_cap=32\n");
    for (;;) {
        usb_trace_record_t record;
        if (xQueueReceive(trace_queue, &record, pdMS_TO_TICKS(1000)) == pdTRUE) trace_print(&record);
        unsigned lost = atomic_load_explicit(&trace_lost, memory_order_relaxed);
        if (lost != previous_lost) {
            // printf("USBTRACE dropped=%u capture_incomplete=1\n", lost);
            previous_lost = lost;
        }
    }
}

void usb_host_trace_init(void)
{
    if (trace_queue) return;
    trace_queue = xQueueCreate(TRACE_DEPTH, sizeof(usb_trace_record_t));
    if (!trace_queue) {
        // ESP_LOGE("USBTRACE", "Queue allocation failed; trace disabled");
        return;
    }
    if (xTaskCreate(trace_task, "usb_trace", 4096, NULL, 1, NULL) != pdPASS) {
        vQueueDelete(trace_queue);
        trace_queue = NULL;
        // ESP_LOGE("USBTRACE", "Worker allocation failed; trace disabled");
    }
}
#endif
