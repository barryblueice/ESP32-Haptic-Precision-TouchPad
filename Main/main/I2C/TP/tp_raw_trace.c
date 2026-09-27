#include "tp_raw_trace.h"
#include <inttypes.h>
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

static void raw_trace_task(void *arg)
{
    (void)arg;
    raw_trace_frame_t frame;
    uint8_t previous_mask = 0;
    uint32_t previous_generation = 0;
    printf("TPTRACE enabled: t=capture_ms seq=read_sequence gen=source out=output lost=trace_queue_drops; "
           "TPF fields: t seq slot status x y pressure major minor area\n");
    for (;;) {
        if (xQueueReceive(trace_queue, &frame, portMAX_DELAY) != pdTRUE) continue;
        unsigned length = frame.bytes[0] | ((unsigned)frame.bytes[1] << 8);
        printf("TPRAW t=%" PRIu32 " seq=%" PRIu32 " gen=%" PRIu32 " out=%" PRIu32
               " lost=%" PRIu32 " len=%u\n", frame.time_ms, frame.sequence,
               frame.generation, frame.output_generation,
               (uint32_t)atomic_load_explicit(&trace_lost, memory_order_relaxed), length);
        if (frame.generation != previous_generation) previous_mask = 0;
        previous_generation = frame.generation;
        if (length != 64) { previous_mask = 0; continue; }
        uint8_t mask = 0;
        for (unsigned i = 0; i < 5; ++i) if (frame.bytes[4 + 8*i] & 1) mask |= 1U << i;
        for (unsigned i = 0; i < 5; ++i) if ((mask | previous_mask) & (1U << i)) {
            const uint8_t *f = frame.bytes + 4 + 8*i;
            printf("TPF t=%" PRIu32 " seq=%" PRIu32 " slot=%u status=%02X x=%u y=%u"
                   " pressure=%u major=%u minor=%u area=%u\n", frame.time_ms, frame.sequence,
                   i, (unsigned)f[0], (unsigned)f[1] | ((unsigned)f[2] << 8),
                   (unsigned)f[3] | ((unsigned)f[4] << 8), (unsigned)f[5],
                   (unsigned)f[6], (unsigned)f[7], (unsigned)f[6]*f[7]);
        }
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
