/* SAM 9.101.139 variant 0: DD95E (C1), DE6EA (forward), D8E84 (report gate).
 * See tools/surface_firmware/SAM_0X49_READ_FORWARD.zh-CN.md in mcu-drivers.
 * No Force mode/configuration/calibration writes belong to this protocol.
 */
#include "force_forward_protocol.h"

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put_le16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

bool force_c1_decode(const uint8_t *raw, size_t size, force_sample_t *sample)
{
    if (!raw || !sample || size != FORCE_C1_SIZE) return false;
    int32_t sum = le16(raw + 1);
    for (unsigned i = 0; i < 6; ++i) {
        uint16_t word = le16(raw + 3 + 2 * i);
        sum += word < 0x8000U ? (int32_t)word : (int32_t)word - 65536;
    }
    /* Deliberately NOT a modulo-65536 checksum: an all-zero frame is invalid. */
    if (65536 - sum != (int32_t)le16(raw + 15)) return false;
    sample->extra = le16(raw + 1);
    for (unsigned i = 0; i < 6; ++i) {
        uint16_t word = le16(raw + 3 + 2 * i);
        sample->channels[i] = (int16_t)(word < 0x8000U ? (int32_t)word : (int32_t)word - 65536);
    }
    return true;
}

void force_forward_encode(const force_sample_t *sample, bool qualified_keystroke,
                          uint8_t packet[FORCE_FORWARD_SIZE])
{
    static const uint8_t prefix[] = {0x22, 0x00, 0x3F, 0x03, 0x23, 0x23, 0x00, 0x12, 0x00, 0x23};
    for (unsigned i = 0; i < sizeof(prefix); ++i) packet[i] = prefix[i];
    put_le16(packet + 10, sample->extra);
    for (unsigned i = 0; i < 6; ++i) put_le16(packet + 12 + 2 * i, (uint16_t)sample->channels[i]);
    /* SAM touchpad context +0x14: recent QualifiedKeystroke, not PTP mode. */
    packet[24] = qualified_keystroke ? 1 : 0;
}

static force_forward_result_t failed(force_forward_state_t *s, const force_forward_io_t *io,
                                     force_forward_result_t result, int error)
{
    s->last_error = error;
    s->failed_at = io->now_ms(io->ctx);
    s->backoff = true;
    if (result == FORCE_FORWARD_WRITE_ERROR) ++s->forward_failures;
    else {
        s->selected = false;
        if (result == FORCE_FORWARD_READ_ERROR) ++s->read_failures;
        else ++s->checksum_failures;
    }
    return result;
}

static bool ready(force_forward_state_t *s, const force_forward_io_t *io)
{
    if (io->ready(io->ctx)) return true;
    s->selected = false;
    return false;
}

force_forward_result_t force_forward_run(force_forward_state_t *s, const force_forward_io_t *io,
    const uint8_t *report, size_t capacity, uint32_t epoch)
{
    if (s->epoch != epoch) {
        s->epoch = epoch;
        s->selected = false;
    }
    if (!ready(s, io)) return FORCE_FORWARD_SKIPPED;
    if (!report || capacity < 3) return FORCE_FORWARD_SKIPPED;
    uint16_t length = le16(report);
    if (length < 6 || length > 64 || length > capacity || (report[2] != 2 && report[2] != 4))
        return FORCE_FORWARD_SKIPPED;
    if (s->backoff && (uint32_t)(io->now_ms(io->ctx) - s->failed_at) < FORCE_FORWARD_BACKOFF_MS)
        return FORCE_FORWARD_SKIPPED;
    s->backoff = false;

    int err;
    if (!s->selected) {
        const uint8_t select[] = {0xD0, 0xEC, 0x00, 0xC1};
        err = io->select_c1(io->ctx, select, sizeof(select));
        if (err) return failed(s, io, FORCE_FORWARD_READ_ERROR, err);
    }
    if (!ready(s, io)) return FORCE_FORWARD_CANCELLED;
    const uint8_t query[] = {0xD0, 0xEE, 0x00};
    uint8_t raw[FORCE_C1_SIZE];
    err = io->read_c1(io->ctx, query, sizeof(query), raw, sizeof(raw));
    if (err) return failed(s, io, FORCE_FORWARD_READ_ERROR, err);
    force_sample_t sample;
    if (!force_c1_decode(raw, sizeof(raw), &sample))
        return failed(s, io, FORCE_FORWARD_CHECKSUM_ERROR, 0);
    s->selected = true;

    uint8_t packet[FORCE_FORWARD_SIZE];
    /* This touchpad has no incoming keyboard event source. */
    force_forward_encode(&sample, false, packet);
    if (!ready(s, io)) return FORCE_FORWARD_CANCELLED;
    err = io->forward(io->ctx, packet, sizeof(packet));
    if (err) return failed(s, io, FORCE_FORWARD_WRITE_ERROR, err);
    ++s->sent;
    s->last_error = 0;
    return FORCE_FORWARD_SENT;
}
