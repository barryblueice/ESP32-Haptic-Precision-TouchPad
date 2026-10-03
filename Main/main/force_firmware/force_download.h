#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#define FORCE_API __declspec(dllexport)
#else
#define FORCE_API
#endif
/* Zero means success; transport errors are propagated unchanged. */
enum { FORCE_INVALID = -1, FORCE_TIMEOUT = -2, FORCE_STATUS = -3 };
typedef struct {
    void *ctx;
    int (*write)(void *, const uint8_t *, size_t);
    int (*read)(void *, const uint8_t *, size_t, uint8_t *, size_t);
    void (*wait_ms)(void *, uint32_t);
    uint64_t (*now_ms)(void *);
} force_io_t;
typedef struct {
    const char *stage;
    uint32_t offset;
    uint8_t status;
} force_result_t;
/* One attempt only. Caller validates image and records an attempt before calling.
 * Port timing policy (milliseconds), NOT proven units of SAM delay parameters:
 * 150/100 ms preparation; 50 ms polls; 2 s per item; 10 s commit deadline.
 */
FORCE_API int force_download_run(const force_io_t *, const uint8_t *, size_t, force_result_t *);
FORCE_API int force_identity(const uint8_t *, size_t, uint32_t *);
