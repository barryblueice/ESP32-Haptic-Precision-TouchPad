#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && !defined(ESP_PLATFORM)
#define FORCE_FORWARD_API __declspec(dllexport)
#else
#define FORCE_FORWARD_API
#endif

#define FORCE_C1_SIZE 17U
#define FORCE_FORWARD_SIZE 25U
#define FORCE_FORWARD_TIMEOUT_MS 10U
#define FORCE_FORWARD_BACKOFF_MS 100U

typedef struct {
    uint16_t extra;
    int16_t channels[6];
} force_sample_t;

/* Only the parser task owns this state. Lifecycle changes arrive as epochs. */
typedef struct {
    uint32_t epoch, failed_at;
    uint32_t sent, read_failures, checksum_failures, forward_failures;
    int last_error;
    bool selected, backoff;
} force_forward_state_t;

typedef struct {
    void *ctx;
    int (*select_c1)(void *ctx, const uint8_t *tx, size_t size);
    int (*read_c1)(void *ctx, const uint8_t *tx, size_t size, uint8_t *rx, size_t count);
    int (*forward)(void *ctx, const uint8_t *tx, size_t size);
    uint32_t (*now_ms)(void *ctx);
    /* Rechecked before each transaction, including after a successful read. */
    bool (*ready)(void *ctx);
} force_forward_io_t;

typedef enum {
    FORCE_FORWARD_SKIPPED, FORCE_FORWARD_SENT, FORCE_FORWARD_CANCELLED,
    FORCE_FORWARD_READ_ERROR, FORCE_FORWARD_CHECKSUM_ERROR, FORCE_FORWARD_WRITE_ERROR
} force_forward_result_t;

FORCE_FORWARD_API bool force_c1_decode(const uint8_t *raw, size_t size, force_sample_t *sample);
FORCE_FORWARD_API void force_forward_encode(const force_sample_t *sample,
    bool qualified_keystroke, uint8_t packet[FORCE_FORWARD_SIZE]);
FORCE_FORWARD_API force_forward_result_t force_forward_run(force_forward_state_t *state,
    const force_forward_io_t *io, const uint8_t *report, size_t capacity, uint32_t epoch);
