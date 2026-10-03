#include "force_download.h"

static int poll(const force_io_t *io, uint8_t reg, uint32_t budget,
                int reject_zero, force_result_t *result)
{
    const uint8_t tx[] = {0xF9, reg};
    const uint64_t start = io->now_ms(io->ctx);
    for (;;) {
        if (io->now_ms(io->ctx) - start >= budget) return FORCE_TIMEOUT;
        uint8_t rx = 0xA5;
        int err = io->read(io->ctx, tx, sizeof(tx), &rx, 1);
        if (err) return err;
        result->status = rx;
        if (reject_zero && rx == 0) return FORCE_STATUS;
        if (io->now_ms(io->ctx) - start >= budget) return FORCE_TIMEOUT;
        if (!(rx & 0x80)) return 0;
        io->wait_ms(io->ctx, 50);
    }
}

int force_identity(const uint8_t *raw, size_t len, uint32_t *value)
{
    if (!raw || !value || len != 7) return FORCE_INVALID;
    uint32_t sum = raw[1] + raw[2] + raw[3] + raw[4];
    uint16_t tail = (uint16_t)raw[5] | ((uint16_t)raw[6] << 8);
    if (!sum || (uint16_t)(0U - sum) != tail) return FORCE_STATUS;
    *value = ((uint32_t)raw[2] << 24) | ((uint32_t)raw[3] << 8) | raw[4];
    return 0;
}

int force_download_run(const force_io_t *io, const uint8_t *image,
                       size_t length, force_result_t *result)
{
    if (!result) return FORCE_INVALID;
    result->stage = "validate"; result->offset = 0; result->status = 0;
    if (!io || !io->write || !io->read || !io->wait_ms || !io->now_ms ||
        !image || !length || length > 0xFFFFU) return FORCE_INVALID;
    int err;
#define WRITE(stage_name, ...) do { \
    const uint8_t tx[] = {__VA_ARGS__}; result->stage = stage_name; \
    err = io->write(io->ctx, tx, sizeof(tx)); if (err) return err; \
} while (0)
    WRITE("prepare_f7", 0xF7,0x52,0x34);
    io->wait_ms(io->ctx, 150);
    WRITE("prepare_b6", 0xB6,0x00,0x1E,0x38);
    WRITE("prepare_f7_74", 0xF7,0x74,0x45);
    WRITE("prepare_fa72", 0xFA,0x72,0x03);
    io->wait_ms(io->ctx, 100);
    for (unsigned i=0; i<16; ++i) {
        result->offset = i;
        WRITE("prepare_item", 0xFA,0x02,(uint8_t)(0x80 | i));
        result->stage = "poll_item";
        err = poll(io, 0x02, 2000, 0, result);
        if (err) return err;
    }
    for (size_t offset=0; offset<length;) {
        uint8_t tx[19] = {0xF8, (uint8_t)(offset >> 8), (uint8_t)offset};
        size_t n = length-offset < 16 ? length-offset : 16;
        for (size_t j=0; j<n; ++j) tx[3+j] = image[offset+j];
        result->stage = "data_f8"; result->offset = (uint32_t)offset;
        err = io->write(io->ctx, tx, n+3);
        if (err) return err;
        offset += n;
        /* Yield without changing packet boundaries; no entire-download retry. */
        io->wait_ms(io->ctx, 1);
    }
    result->offset = (uint32_t)length;
    WRITE("commit_length", 0xFA,0x06,0,0,0,0,(uint8_t)length,(uint8_t)(length>>8),0);
    WRITE("commit_start", 0xFA,0x05,0xC0,0);
    result->stage = "poll_commit";
    err = poll(io, 0x05, 10000, 1, result);
    if (err) return err;
    WRITE("finish_f7", 0xF7,0x52,0x34);
    io->wait_ms(io->ctx, 150);
    const uint8_t tx[] = {0xB6,0,0x74};
    uint8_t rx[2] = {0xA5,0xA5};
    result->stage = "finish_status";
    err = io->read(io->ctx, tx, sizeof(tx), rx, sizeof(rx));
    if (err) return err;
    result->status = rx[1];
    if (rx[1] & 3) return FORCE_STATUS;
    result->stage = "protocol_complete";
    return 0;
#undef WRITE
}
