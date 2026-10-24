#include "tp_raw_trace.h"
#include "tp_f12.h"
#include <inttypes.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_log.h"

#if TP_RAW_TRACE_ENABLED
typedef struct {
    uint32_t time_ms, sequence, generation, output_generation;
    uint8_t bytes[64];
} raw_trace_frame_t;
static QueueHandle_t trace_queue;
static uint32_t trace_sequence; /* Single producer: the touch IRQ reader task. */
static atomic_uint_least32_t trace_lost;

static void raw_trace_all_slots(const raw_trace_frame_t *frame, unsigned length, uint32_t lost,
                                bool dump_packet)
{
    /* One bounded line per report, including unclassified type=04 objects.
     * Zero slots use '-' to keep the UART cost down; no reports are filtered.
     * 512 bytes covers the header plus five slots at maximum field widths. */
    char line[512];
    int used = snprintf(line, sizeof(line),
        "TPALL t=%" PRIu32 " seq=%" PRIu32 " gen=%" PRIu32 " out=%" PRIu32 " lost=%" PRIu32 " len=%u rid=%02X h3=%02X",
        frame->time_ms, frame->sequence, frame->generation, frame->output_generation, lost, length,
        (unsigned)frame->bytes[2], (unsigned)frame->bytes[3]);
    if (length == 64) {
        for (unsigned i = 0; i < 5; ++i) {
            const uint8_t *f = frame->bytes + 4 + 8*i;
            unsigned any = 0;
            for (unsigned j = 0; j < 8; ++j) any |= f[j];
            if (!any) {
                used += snprintf(line + used, sizeof(line) - used, " s%u=-", i);
            } else {
                used += snprintf(line + used, sizeof(line) - used, " s%u=%02X,%u,%u,%u,%u,%u", i,
                    (unsigned)f[0], (unsigned)f[1] | ((unsigned)f[2] << 8),
                    (unsigned)f[3] | ((unsigned)f[4] << 8), (unsigned)f[5], (unsigned)f[6], (unsigned)f[7]);
            }
        }
    } else {
        used += snprintf(line + used, sizeof(line) - used, " raw=");
        for (unsigned i = 0; i < length; ++i)
            used += snprintf(line + used, sizeof(line) - used, "%02X", (unsigned)frame->bytes[i]);
    }
    printf("%s\n", line);
    if (dump_packet) {
        /* Preserve complete evidence independently of the slot view, so a
         * different controller/report layout can still be identified. */
        used = snprintf(line, sizeof(line), "TPFRAME t=%" PRIu32 " seq=%" PRIu32 " raw=",
            frame->time_ms, frame->sequence);
        for (unsigned i = 0; i < length; ++i)
            used += snprintf(line + used, sizeof(line) - used, "%02X", (unsigned)frame->bytes[i]);
        printf("%s\n", line);
    }
}

static void raw_trace_task(void *arg)
{
    (void)arg;
    raw_trace_frame_t frame;
    uint8_t previous_mask = 0;
    uint8_t previous_status[5] = {0};
    uint32_t previous_generation = 0;
    uint32_t previous_output = 0, previous_lost = 0, previous_sequence = 0;
    uint8_t previous_report_id = 0, previous_h3 = 0;
    unsigned previous_length = 0;
    printf("TPTRACE enabled: t=capture_ms seq=read_sequence gen=source out=output lost=trace_queue_drops; "
           "mode=%u (1=calibration 2=transitions 3=all_slots); "
           "TPALL sN=raw_status_hex,x,y,z,byte6,byte7; -=all_8_slot_bytes_zero\n", TP_RAW_TRACE_ENABLED);
    if (TP_RAW_TRACE_ENABLED == 3)
        printf("TPTRACE TM3651 F12 layout: bytes[0..1]=length_le bytes[2]=report_id(0C) bytes[3]=irq_status; "
               "sN uses parser_offset=4+8*N stride=8; TPFRAME preserves full packets on header/status changes; "
               "01=finger 03=palm 04=unclassified 06=gloved_finger; byte6=Wx byte7=Wy; nonzero=present, only finger types eligible\n");
    for (;;) {
        if (xQueueReceive(trace_queue, &frame, portMAX_DELAY) != pdTRUE) continue;
        unsigned length = frame.bytes[0] | ((unsigned)frame.bytes[1] << 8);
        uint32_t lost = atomic_load_explicit(&trace_lost, memory_order_relaxed);
        if (TP_RAW_TRACE_ENABLED == 3) {
            bool dump_packet = frame.generation != previous_generation ||
                frame.output_generation != previous_output || lost != previous_lost ||
                frame.sequence != previous_sequence + 1 || length != previous_length ||
                frame.bytes[2] != previous_report_id || frame.bytes[3] != previous_h3;
            for (unsigned i = 0; i < 5; ++i) {
                uint8_t status = length == 64 ? frame.bytes[4 + 8*i] : 0;
                if (status != previous_status[i]) dump_packet = true;
                previous_status[i] = status;
            }
            raw_trace_all_slots(&frame, length, lost, dump_packet);
            previous_generation = frame.generation;
            previous_output = frame.output_generation;
            previous_lost = lost;
            previous_sequence = frame.sequence;
            previous_length = length;
            previous_report_id = frame.bytes[2];
            previous_h3 = frame.bytes[3];
            continue;
        }
        bool resync = frame.generation != previous_generation || lost != previous_lost ||
            length != previous_length || frame.sequence != previous_sequence + 1;
        uint8_t mask = 0, changed = 0;
        if (length == 64) {
            for (unsigned i = 0; i < 5; ++i) {
                uint8_t status = frame.bytes[4 + 8*i];
                if (tp_f12_present(status)) mask |= 1U << i;
                if (status != previous_status[i]) changed |= 1U << i;
            }
        }
        /* Continuous movement stays off the UART in transition mode. The
         * worker still drains every frame, independently of recognition. */
        changed &= mask | previous_mask;
        bool emit = TP_RAW_TRACE_ENABLED == 1 || resync || changed ||
            frame.output_generation != previous_output;
        if (emit) {
            printf("TPRAW t=%" PRIu32 " seq=%" PRIu32 " gen=%" PRIu32 " out=%" PRIu32
                   " lost=%" PRIu32 " len=%u%s\n", frame.time_ms, frame.sequence,
                   frame.generation, frame.output_generation, lost, length,
                   TP_RAW_TRACE_ENABLED == 1 ? "" : resync ? " resync=1" : " resync=0");
        }
        previous_generation = frame.generation;
        previous_output = frame.output_generation;
        previous_lost = lost;
        previous_sequence = frame.sequence;
        previous_length = length;
        if (length != 64) { previous_mask = 0; memset(previous_status, 0, sizeof(previous_status)); continue; }
        uint8_t print_mask = (TP_RAW_TRACE_ENABLED == 1 || resync) ? mask | previous_mask : changed;
        for (unsigned i = 0; i < 5; ++i) if (print_mask & (1U << i)) {
            const uint8_t *f = frame.bytes + 4 + 8*i;
            const char *event = resync ? "sync" : !(mask & (1U << i)) ? "up" :
                !(previous_mask & (1U << i)) ? "down" : "update";
            printf("TPF t=%" PRIu32 " seq=%" PRIu32 " slot=%u status=%02X x=%u y=%u"
                   " pressure=%u major=%u minor=%u area=%u%s%s\n", frame.time_ms, frame.sequence,
                   i, (unsigned)f[0], (unsigned)f[1] | ((unsigned)f[2] << 8),
                   (unsigned)f[3] | ((unsigned)f[4] << 8), (unsigned)f[5],
                   (unsigned)f[6], (unsigned)f[7], (unsigned)f[6]*f[7],
                   TP_RAW_TRACE_ENABLED == 1 ? "" : " event=", TP_RAW_TRACE_ENABLED == 1 ? "" : event);
        }
        for (unsigned i = 0; i < 5; ++i) previous_status[i] = frame.bytes[4 + 8*i];
        previous_mask = mask;
    }
}
#endif

void tp_raw_trace_init(void)
{
#if TP_RAW_TRACE_ENABLED
    if (trace_queue) return;
    trace_queue = xQueueCreate(128, sizeof(raw_trace_frame_t));
    if (!trace_queue) {
        ESP_LOGW("TPTRACE", "No memory for temporary raw trace queue"); return;
    }
    if (xTaskCreate(raw_trace_task, "tp_raw_trace", 4096, NULL, tskIDLE_PRIORITY + 1, NULL) != pdPASS) {
        vQueueDelete(trace_queue); trace_queue = NULL;
        ESP_LOGW("TPTRACE", "Could not start temporary raw trace task");
    }
#endif
}

void tp_raw_trace_capture(const uint8_t bytes[64], uint32_t time_ms,
                          uint32_t generation, uint32_t output_generation)
{
#if TP_RAW_TRACE_ENABLED
    if (!trace_queue) return;
    unsigned length = bytes[0] | ((unsigned)bytes[1] << 8);
    if (length < 6 || length > 64) return;
    raw_trace_frame_t frame = {.time_ms = time_ms, .sequence = ++trace_sequence,
        .generation = generation, .output_generation = output_generation};
    memcpy(frame.bytes, bytes, length);
    if (xQueueSend(trace_queue, &frame, 0) != pdPASS)
        atomic_fetch_add_explicit(&trace_lost, 1, memory_order_relaxed);
#else
    (void)bytes; (void)time_ms; (void)generation; (void)output_generation;
#endif
}
