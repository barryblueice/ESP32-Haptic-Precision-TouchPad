/* Temporary standalone RMI-over-HID border diagnostic.
 * Normal main.c: git 5aa4d4d110827a0ff1ff7802ab69156f66af3dcb.
 * Reversible Control8 ylo experiment; no calibration, flash or NVS writes.
 */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2C/I2C_handle.h"

#define TAG "RMI_PROBE"
#define TRY(call) do { esp_err_t e = (call); if (e != ESP_OK) return e; } while (0)
static i2c_master_bus_handle_t probe_bus;
static i2c_master_dev_handle_t probe_dev;
static uint16_t command_reg, data_reg, output_reg, input_size, output_size;
static uint16_t report_bytes[16][3]; /* input/output/feature, including ID */
static uint8_t descriptor[1024], structure[512];
static int selected_page = -1;
static int64_t probe_deadline;
static uint16_t device_pid;
static bool known_product, read_channel_failed;
static struct {
    bool valid, attempted, restored;
    uint16_t addr;
    uint8_t original[14];
} border_test;
static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1] << 8); }
static uint32_t u32(const uint8_t *p) { return u16(p) | ((uint32_t)u16(p + 2) << 16); }
static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void pause_ms(unsigned ms)
{ TickType_t t = pdMS_TO_TICKS(ms); vTaskDelay(t ? t : 1); }

static esp_err_t hid_read(uint16_t reg, void *dst, size_t len)
{
    uint8_t addr[2]; put16(addr, reg);
    return i2c_master_transmit_receive(probe_dev, addr, 2, dst, len, 100);
}
static esp_err_t hid_write(uint16_t reg, const uint8_t *src, size_t len)
{
    uint8_t packet[80];
    if (len > sizeof(packet) - 2) return ESP_ERR_INVALID_SIZE;
    put16(packet, reg); memcpy(packet + 2, src, len);
    return i2c_master_transmit(probe_dev, packet, len + 2, 100);
}

/* Interpret HID short items and global PUSH/POP, not byte-pattern matching. */
static esp_err_t parse_reports(const uint8_t *buf, size_t len)
{
    struct globals { uint32_t size, count, id; } g = {0}, stack[8];
    unsigned depth = 0;
    uint32_t bits[16][3] = {{0}};
    memset(report_bytes, 0, sizeof(report_bytes));
    for (size_t i = 0; i < len;) {
        uint8_t key = buf[i++];
        if (key == 0xfe) return ESP_ERR_NOT_SUPPORTED;
        unsigned n = key & 3U; if (n == 3) n = 4;
        if (n > len - i) return ESP_ERR_INVALID_SIZE;
        uint32_t v = 0;
        for (unsigned j = 0; j < n; ++j) v |= (uint32_t)buf[i + j] << (8 * j);
        i += n;
        switch (key & 0xfcU) {
        case 0x74: g.size = v; break;
        case 0x94: g.count = v; break;
        case 0x84: if (!v || v > 255) return ESP_ERR_INVALID_ARG; g.id = v; break;
        case 0xa4: if (depth == 8) return ESP_ERR_INVALID_SIZE; stack[depth++] = g; break;
        case 0xb4: if (!depth) return ESP_ERR_INVALID_SIZE; g = stack[--depth]; break;
        case 0x80: case 0x90: case 0xb0: {
            unsigned kind = (key & 0xfcU) == 0x80 ? 0 : (key & 0xfcU) == 0x90 ? 1 : 2;
            if (g.id >= 16) break;
            if (g.size > 32 || g.count > 8192 || g.size * g.count > 65528 - bits[g.id][kind])
                return ESP_ERR_INVALID_SIZE;
            bits[g.id][kind] += g.size * g.count;
            break;
        }
        default: break;
        }
    }
    if (depth) return ESP_ERR_INVALID_SIZE;
    for (unsigned id = 1; id < 16; ++id)
        for (unsigned k = 0; k < 3; ++k)
            if (bits[id][k]) report_bytes[id][k] = 1 + (bits[id][k] + 7) / 8;
    return ESP_OK;
}
static esp_err_t discover_hid(void)
{
    uint8_t d[30];
    /* This module's recorded HID descriptor lives at 0x0020. */
    esp_err_t err = hid_read(0x0020, d, sizeof(d));
    if (err != ESP_OK || u16(d) != 30 || u16(d + 2) != 0x0100)
        TRY(hid_read(HID_DESC_REG, d, sizeof(d)));
    if (u16(d) != 30 || u16(d + 2) != 0x0100 || u16(d + 20) != 0x06cb)
        return ESP_ERR_NOT_SUPPORTED;
    input_size = u16(d + 10); output_size = u16(d + 14);
    device_pid = u16(d + 22);
    output_reg = u16(d + 12); command_reg = u16(d + 16); data_reg = u16(d + 18);
    ESP_LOGI(TAG, "HID VID:PID=%04x:%04x version=%04x input=%04x/%u output=%04x/%u command=%04x data=%04x",
        u16(d + 20), u16(d + 22), u16(d + 24), u16(d + 8), input_size,
        output_reg, output_size, command_reg, data_reg);
    size_t len = u16(d + 4);
    if (!len || len > sizeof(descriptor) || input_size < 4 || input_size > 64 ||
        output_size < 8 || output_size > 64) return ESP_ERR_INVALID_SIZE;
    TRY(hid_read(u16(d + 6), descriptor, len));
    TRY(parse_reports(descriptor, len));
    ESP_LOGI(TAG, "RMI HID sizes: write09=%u read0a=%u reply0b=%u attn0c=%u mode0f=%u",
        report_bytes[9][1], report_bytes[10][1], report_bytes[11][0],
        report_bytes[12][0], report_bytes[15][2]);
    if (report_bytes[9][1] < 5 || report_bytes[10][1] < 6 || report_bytes[11][0] < 3 ||
        report_bytes[12][0] < 2 || report_bytes[15][2] < 2 || report_bytes[15][2] > 16 ||
        report_bytes[9][1] + 2 > output_size || report_bytes[10][1] + 2 > output_size ||
        report_bytes[11][0] + 2 > input_size || report_bytes[12][0] + 2 > input_size)
        return ESP_ERR_NOT_SUPPORTED;
    return ESP_OK;
}
static esp_err_t set_rmi_mode(uint8_t mode)
{
    uint8_t cmd[24] = {0x3f, 0x03, 0x0f}; /* Feature SET_REPORT, extended ID */
    put16(cmd + 3, data_reg); put16(cmd + 5, report_bytes[15][2] + 2);
    cmd[7] = 0x0f; cmd[8] = mode;
    return hid_write(command_reg, cmd, 7 + report_bytes[15][2]);
}
static esp_err_t send_output(const uint8_t *report, size_t used)
{
    uint8_t payload[64] = {0};
    if (!used || report[0] >= 16) return ESP_ERR_INVALID_ARG;
    size_t size = report_bytes[report[0]][1];
    if (size < used || size + 2 > output_size) return ESP_ERR_INVALID_SIZE;
    put16(payload, size + 2); memcpy(payload + 2, report, used);
    return hid_write(output_reg, payload, size + 2);
}
static esp_err_t drain_input(void)
{
    uint8_t packet[64];
    int64_t end = esp_timer_get_time() + 500000;
    while (gpio_get_level(TP_INT_GPIO) == 0) {
        if (esp_timer_get_time() >= end) return ESP_ERR_TIMEOUT;
        TRY(i2c_master_receive(probe_dev, packet, input_size, 100));
        pause_ms(1);
    }
    return ESP_OK;
}
static esp_err_t select_page(unsigned page)
{
    if (selected_page == (int)page) return ESP_OK;
    /* Page selection is independent of packet-register payload offsets. */
    uint8_t report[] = {0x09, 1, 0xff, 0, (uint8_t)page};
    TRY(send_output(report, sizeof(report)));
    selected_page = page;
    return ESP_OK;
}
/* No retry after timeout: replies carry no address or transaction ID. */
static esp_err_t rmi_read_transfer(uint16_t addr, uint8_t *dst, size_t len)
{
    if (!len || len > sizeof(structure)) return ESP_ERR_INVALID_SIZE;
    if (esp_timer_get_time() >= probe_deadline) return ESP_ERR_TIMEOUT;
    TRY(select_page(addr >> 8));
    uint8_t request[6] = {0x0a, 0};
    put16(request + 2, addr); put16(request + 4, len);
    TRY(send_output(request, sizeof(request)));
    int64_t end = esp_timer_get_time() + 750000;
    size_t done = 0;
    while (done < len) {
        int64_t now = esp_timer_get_time();
        if (now >= end || now >= probe_deadline) {
            ESP_LOGE(TAG, "RMI timeout addr=%04x received=%u/%u", addr, (unsigned)done, (unsigned)len);
            return ESP_ERR_TIMEOUT;
        }
        if (gpio_get_level(TP_INT_GPIO)) { pause_ms(1); continue; }
        uint8_t packet[64] = {0};
        TRY(i2c_master_receive(probe_dev, packet, input_size, 100));
        unsigned size = u16(packet);
        if (!size) return ESP_ERR_INVALID_STATE; /* Unexpected reset completion. */
        if (size == 2 || size == 0xffff) { pause_ms(1); continue; }
        if (size < 3 || size > input_size) return ESP_ERR_INVALID_SIZE;
        if (packet[2] != 0x0b) { pause_ms(1); continue; }
        if (size < 4) return ESP_ERR_INVALID_SIZE;
        size_t count = packet[3];
        if (!count || count > size - 4 || count > report_bytes[11][0] - 2 || count > len - done)
            return ESP_ERR_INVALID_SIZE;
        memcpy(dst + done, packet + 4, count); done += count;
        pause_ms(1);
    }
    return ESP_OK;
}
static esp_err_t rmi_read(uint16_t addr, uint8_t *dst, size_t len)
{
    if (read_channel_failed) return ESP_ERR_INVALID_STATE;
    esp_err_t err = rmi_read_transfer(addr, dst, len);
    if (err != ESP_OK) read_channel_failed = true;
    return err;
}
static esp_err_t write_control8(const uint8_t value[14])
{
    if (!border_test.valid || report_bytes[9][1] < 18) return ESP_ERR_INVALID_STATE;
    TRY(select_page(border_test.addr >> 8));
    uint8_t report[18] = {0x09, 14};
    put16(report + 2, border_test.addr);
    memcpy(report + 4, value, 14);
    /* Whole packet at its base address, NOT base + byte offset 8. */
    return send_output(report, sizeof(report));
}
typedef struct { bool found; uint16_t query, control, data; } rmi_function_t;
static rmi_function_t f01, f11, f12;
static esp_err_t scan_pdt(void)
{
    unsigned empty = 0;
    for (unsigned page = 0; page < 256 && empty < 2; ++page) {
        bool any = false;
        for (int offset = 0xe9; offset >= 5; offset -= 6) {
            uint8_t p[6]; uint16_t base = page << 8;
            TRY(rmi_read(base | offset, p, sizeof(p)));
            if (!p[5] || p[5] == 0xff) break;
            any = true;
            ESP_LOGI(TAG, "PDT F%02x version=%u query=%04x command=%04x control=%04x data=%04x irq=%u",
                p[5], (p[4] >> 5) & 3, base | p[0], base | p[1], base | p[2], base | p[3], p[4] & 7);
            rmi_function_t *fn = p[5] == 1 ? &f01 : p[5] == 0x11 ? &f11 : p[5] == 0x12 ? &f12 : NULL;
            if (fn && !fn->found) *fn = (rmi_function_t){true, base | p[0], base | p[2], base | p[3]};
        }
        empty = any ? 0 : empty + 1;
    }
    if (!f01.found) return ESP_ERR_NOT_FOUND;
    uint8_t id[21], status;
    TRY(rmi_read(f01.query, id, sizeof(id))); TRY(rmi_read(f01.data, &status, 1));
    char product[11]; memcpy(product, id + 11, 10); product[10] = 0;
    known_product = id[0] == 1 && memcmp(product, "TM3651-001", 10) == 0;
    ESP_LOGI(TAG, "F01 manufacturer=%u product=%.10s status=%02x", id[0], product, status);
    if (status & 0x40) return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
typedef struct { unsigned offset; uint32_t size; uint8_t subpackets; } control8_t;
/* Packet register addresses count PRESENT registers, not their byte sizes. */
static esp_err_t find_control8(const uint8_t *presence, size_t plen,
                              const uint8_t *items, size_t ilen, control8_t *out)
{
    size_t cursor = 0; unsigned offset = 0;
    for (unsigned reg = 0; reg <= 8; ++reg) {
        if (reg / 8 >= plen || !(presence[reg / 8] & (1U << (reg % 8)))) continue;
        if (cursor >= ilen) return ESP_ERR_INVALID_SIZE;
        uint32_t size = items[cursor++];
        if (!size) {
            if (ilen - cursor < 2) return ESP_ERR_INVALID_SIZE;
            size = u16(items + cursor); cursor += 2;
            if (!size) {
                if (ilen - cursor < 4) return ESP_ERR_INVALID_SIZE;
                size = u32(items + cursor); cursor += 4;
            }
        }
        if (!size) return ESP_ERR_INVALID_SIZE;
        uint8_t low = 0; bool first = true;
        for (;;) {
            if (cursor >= ilen) return ESP_ERR_INVALID_SIZE;
            uint8_t map = items[cursor++];
            if (first) low = map & 0x7f;
            first = false;
            if (!(map & 0x80)) break;
        }
        if (reg == 8) { *out = (control8_t){offset, size, low}; return ESP_OK; }
        ++offset;
    }
    return ESP_ERR_NOT_FOUND;
}
static esp_err_t query_f12(void)
{
    uint8_t general, plen, presence[35];
    TRY(rmi_read(f12.query, &general, 1));
    if (!(general & 1)) {
        ESP_LOGW(TAG, "F12 has no register descriptors; border layout UNKNOWN");
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (f12.query > 0xfff9) return ESP_ERR_INVALID_SIZE;
    /* Query descriptor +1..3, control descriptor +4..6, data descriptor +7..9. */
    TRY(rmi_read(f12.query + 4, &plen, 1));
    if (!plen || plen > sizeof(presence)) return ESP_ERR_INVALID_SIZE;
    TRY(rmi_read(f12.query + 5, presence, plen));
    unsigned start = presence[0] ? 1 : 3;
    if (plen < start) return ESP_ERR_INVALID_SIZE;
    unsigned slen = presence[0] ? presence[0] : u16(presence + 1);
    if (!slen || slen > sizeof(structure)) return ESP_ERR_INVALID_SIZE;
    TRY(rmi_read(f12.query + 6, structure, slen));
    ESP_LOGI(TAG, "F12 control presence (%u bytes), structure (%u bytes):", plen, slen);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, presence, plen, ESP_LOG_INFO);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, structure, slen, ESP_LOG_INFO);
    control8_t c;
    esp_err_t err = find_control8(presence + start, plen - start, structure, slen, &c);
    if (err == ESP_ERR_NOT_FOUND) ESP_LOGW(TAG, "F12 Control8 ABSENT; inactive border not exposed here");
    TRY(err);
    if (c.size > 64 || c.offset > 0xffffU - f12.control) return ESP_ERR_INVALID_SIZE;
    uint8_t value[64]; uint16_t addr = f12.control + c.offset;
    TRY(rmi_read(addr, value, c.size));
    ESP_LOGI(TAG, "F12 Control8 addr=%04x size=%lu subpacket0..6=%02x RAW:", addr, (unsigned long)c.size, c.subpackets);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, value, c.size, ESP_LOG_INFO);
    unsigned pos = 0;
    if (c.subpackets & 1) {
        if (c.size < 4) return ESP_ERR_INVALID_SIZE;
        ESP_LOGI(TAG, "F12 max_x=%u max_y=%u", u16(value), u16(value + 2)); pos += 4;
    }
    if (c.subpackets & 2) {
        if (c.size < pos + 4) return ESP_ERR_INVALID_SIZE;
        ESP_LOGI(TAG, "F12 pitch_x_raw=%u pitch_y_raw=%u", u16(value + pos), u16(value + pos + 2)); pos += 4;
    }
    if (!(c.subpackets & 4)) {
        ESP_LOGW(TAG, "RESULT: F12 inactive-border subpacket2 ABSENT"); return ESP_OK;
    }
    if (c.size < pos + 4) return ESP_ERR_INVALID_SIZE;
    ESP_LOGI(TAG, "RESULT: INACTIVE_BORDER xlo=%u xhi=%u ylo=%u yhi=%u unit=1/128_sensor_pitch",
        value[pos], value[pos + 1], value[pos + 2], value[pos + 3]);
    ESP_LOGI(TAG, "Border fields readable at Control8=%04x payload_offset=%u; writability NOT TESTED", addr, pos);
    /* Restrict writes and the known five-slot ATTN decoder to the user's module. */
    if (known_product && device_pid == 0xce43 && c.size == 14 &&
        c.subpackets == 0x0f && pos == 8 && addr == 0x0019 &&
        u16(value) == 2298 && u16(value + 2) == 1532 && report_bytes[9][1] >= 18) {
        border_test.valid = true; border_test.addr = addr;
        memcpy(border_test.original, value, 14);
    }
    return ESP_OK;
}
static esp_err_t query_f11(void)
{
    uint8_t q[5], gestures[2] = {0}, clip;
    TRY(rmi_read(f11.query, q, sizeof(q)));
    if (!(q[0] & 0x10)) {
        ESP_LOGI(TAG, "RESULT: F11 Query11 ABSENT; optional XY clip unavailable"); return ESP_OK;
    }
    if (q[0] & 7) {
        ESP_LOGW(TAG, "F11 multiple sensors; clip layout UNKNOWN"); return ESP_ERR_NOT_SUPPORTED;
    }
    unsigned offset = 5;
    if (q[1] & 0x10) ++offset; /* Query5 */
    if (q[1] & 0x08) ++offset; /* Query6 */
    if (q[1] & 0x20) {
        if (f11.query > 0xffffU - offset - 2) return ESP_ERR_INVALID_SIZE;
        TRY(rmi_read(f11.query + offset, gestures, 2)); offset += 2;
    }
    if (q[0] & 0x08) ++offset; /* Query9 */
    if (gestures[1] & 4) ++offset; /* Query10 */
    if (f11.query > 0xffffU - offset) return ESP_ERR_INVALID_SIZE;
    TRY(rmi_read(f11.query + offset, &clip, 1));
    ESP_LOGI(TAG, "RESULT: F11 Query11=%02x has_XY_clip=%u", clip, !!(clip & 0x40));
    if (clip & 0x40) ESP_LOGI(TAG, "F11 Ctrl46..49 advertised; width offsets/units need F11 layout verification; no writes");
    return ESP_OK;
}
static esp_err_t probe(void)
{
    TRY(discover_hid());
    const uint8_t power[] = {0, 8}, reset[] = {0, 1};
    TRY(hid_write(command_reg, power, sizeof(power))); pause_ms(50);
    TRY(hid_write(command_reg, reset, sizeof(reset))); pause_ms(100);
    TRY(drain_input()); TRY(set_rmi_mode(1));
    probe_deadline = esp_timer_get_time() + 15000000;
    TRY(scan_pdt());
    esp_err_t result = ESP_OK;
    if (f11.found) {
        esp_err_t err = query_f11();
        if (err != ESP_OK) { ESP_LOGW(TAG, "F11 query: %s", esp_err_to_name(err)); result = err; }
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) return err;
    }
    if (f12.found) { TRY(query_f12()); result = ESP_OK; }
    if (!f11.found && !f12.found) {
        ESP_LOGW(TAG, "RESULT: neither F11 nor F12 found"); return ESP_ERR_NOT_FOUND;
    }
    return result;
}
typedef struct {
    unsigned frames, samples, ignored;
    uint16_t min_x, max_x, min_y, max_y, last_x, last_y;
} touch_stats_t;

static esp_err_t collect_touch(const uint8_t *packet, touch_stats_t *s)
{
    unsigned len = u16(packet);
    if (!len) { read_channel_failed = true; return ESP_ERR_INVALID_STATE; }
    if (len == 2 || len == 0xffff) return ESP_OK;
    if (len < 3 || len > input_size) return ESP_ERR_INVALID_SIZE;
    if (packet[2] != 0x0c) { ++s->ignored; return ESP_OK; }
    /* TM3651-001's captured format: one IRQ byte, then five 8-byte objects.
     * F12 occupies IRQ bits 3/4 in the user's PDT. Reject other packed layouts. */
    if (len < 44 || !(packet[3] & 0x18) || (packet[3] & ~0x18U)) {
        ++s->ignored; return ESP_OK;
    }
    ++s->frames;
    for (unsigned slot = 0; slot < 5; ++slot) {
        const uint8_t *p = packet + 4 + 8 * slot;
        if (p[0] != 1) continue; /* Finger only, not palm/stylus/unclassified. */
        uint16_t x = u16(p + 1), y = u16(p + 3);
        if (x > 2298 || y > 1532) { ++s->ignored; continue; }
        if (!s->samples || x < s->min_x) s->min_x = x;
        if (!s->samples || x > s->max_x) s->max_x = x;
        if (!s->samples || y < s->min_y) s->min_y = y;
        if (!s->samples || y > s->max_y) s->max_y = y;
        ++s->samples; s->last_x = x; s->last_y = y;
    }
    return ESP_OK;
}
static esp_err_t observe_touch(const char *phase, unsigned seconds)
{
    touch_stats_t stats = {0};
    int64_t start = esp_timer_get_time(), next = start + 1000000;
    int64_t end = start + (int64_t)seconds * 1000000;
    ESP_LOGW(TAG, "PHASE %s: %u seconds. Sweep ONE finger along Y across both Y edges near X center; lift and re-touch; repeat the same motion each phase.", phase, seconds);
    while (esp_timer_get_time() < end) {
        if (!gpio_get_level(TP_INT_GPIO)) {
            uint8_t packet[64] = {0};
            TRY(i2c_master_receive(probe_dev, packet, input_size, 100));
            TRY(collect_touch(packet, &stats));
        }
        int64_t now = esp_timer_get_time();
        if (now >= next) {
            ESP_LOGI(TAG, "TOUCH phase=%s elapsed=%us samples=%u min_x=%u max_x=%u min_y=%u max_y=%u last_x=%u last_y=%u",
                phase, (unsigned)((now - start) / 1000000), stats.samples,
                stats.min_x, stats.max_x, stats.min_y, stats.max_y, stats.last_x, stats.last_y);
            next = now + 1000000;
        }
        pause_ms(1);
    }
    ESP_LOGI(TAG, "SUMMARY phase=%s frames=%u finger_samples=%u min_x=%u max_x=%u min_y=%u max_y=%u ignored=%u",
        phase, stats.frames, stats.samples, stats.min_x, stats.max_x, stats.min_y, stats.max_y, stats.ignored);
    if (!stats.samples) ESP_LOGW(TAG, "No finger samples in %s: functional effect cannot be inferred (min/max are placeholders)", phase);
    return ESP_OK;
}
static esp_err_t read_control8(uint8_t value[14])
{
    probe_deadline = esp_timer_get_time() + 2000000;
    return rmi_read(border_test.addr, value, 14);
}
static esp_err_t restart_rmi_session(void)
{
    /* Discard an ambiguous/late 0x0b reply before any verification retry. */
    TRY(gpio_set_level(TP_RESET_GPIO, 0)); pause_ms(50);
    TRY(gpio_set_level(TP_RESET_GPIO, 1)); pause_ms(150);
    selected_page = -1; read_channel_failed = false;
    const uint8_t power[] = {0, 8}, reset[] = {0, 1};
    TRY(hid_write(command_reg, power, sizeof(power))); pause_ms(50);
    TRY(hid_write(command_reg, reset, sizeof(reset))); pause_ms(100);
    TRY(drain_input());
    return set_rmi_mode(1);
}
static esp_err_t restore_border(void)
{
    uint8_t check[14];
    selected_page = -1;
    esp_err_t err = write_control8(border_test.original);
    if (err == ESP_OK && !read_channel_failed) {
        pause_ms(20); err = read_control8(check);
        if (err == ESP_OK && memcmp(check, border_test.original, 14)) err = ESP_FAIL;
    } else if (err == ESP_OK) err = ESP_ERR_INVALID_STATE;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Restore verification unavailable; reset session and retry ORIGINAL values once");
        TRY(restart_rmi_session());
        TRY(write_control8(border_test.original)); pause_ms(20);
        TRY(read_control8(check));
        if (memcmp(check, border_test.original, 14)) return ESP_FAIL;
    }
    border_test.restored = true;
    ESP_LOGI(TAG, "RESTORE VERIFIED: all 14 Control8 bytes match original; ylo=%u", border_test.original[10]);
    return ESP_OK;
}
static esp_err_t experiment(void)
{
    if (!border_test.valid || border_test.original[10] == 255) {
        ESP_LOGW(TAG, "Experiment skipped: unverified module/layout or ylo already 255");
        return ESP_ERR_NOT_SUPPORTED;
    }
    TRY(observe_touch("BASELINE", 20));
    uint8_t changed[14], check[14];
    TRY(read_control8(check));
    if (memcmp(check, border_test.original, 14)) {
        ESP_LOGE(TAG, "Control8 changed during baseline; no experiment write issued");
        return ESP_ERR_INVALID_STATE;
    }
    memcpy(changed, border_test.original, 14);
    unsigned target = changed[10] + 128U;
    changed[10] = target > 255 ? 255 : target;
    ESP_LOGW(TAG, "APPLY ylo %u -> %u at Control8=%04x, payload byte 10; other 13 bytes unchanged",
        border_test.original[10], changed[10], border_test.addr);
    /* Set BEFORE transmit: a bus error may still mean a partial/accepted write. */
    border_test.attempted = true;
    esp_err_t err = write_control8(changed);
    if (err == ESP_OK) { pause_ms(20); err = read_control8(check); }
    if (err == ESP_OK && memcmp(check, changed, 14)) {
        ESP_LOGW(TAG, "WRITE NOT VERIFIED: full Control8 readback differs from request; RAW:");
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, check, 14, ESP_LOG_INFO);
        err = ESP_FAIL;
    }
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WRITE VERIFIED: full Control8 matches; this proves readback, NOT touch suppression");
        err = observe_touch("MODIFIED", 30);
        if (err == ESP_OK) {
            err = read_control8(check);
            if (err == ESP_OK && memcmp(check, changed, 14)) {
                ESP_LOGW(TAG, "Modified setting did not persist through observation"); err = ESP_FAIL;
            }
        }
    }
    /* No early returns after the write attempt: restore on every error path. */
    esp_err_t restored = restore_border();
    if (restored != ESP_OK) {
        ESP_LOGE(TAG, "RESTORE NOT VERIFIED: %s; final hardware reset will follow", esp_err_to_name(restored));
        return restored;
    }
    if (err == ESP_OK) err = observe_touch("RESTORED", 20);
    return err;
}
void app_main(void)
{
    ESP_LOGW(TAG, "Standalone border diagnostic: normal USB/BLE/WiFi/haptic tasks disabled");
    ESP_LOGI(TAG, "I2C address=%02x SDA=%u SCL=%u INT=%u RESET=%u", TP_I2C_ADDR, TP_I2C_SDA, TP_I2C_SCL, TP_INT_GPIO, TP_RESET_GPIO);
    /* Keep haptic boost off; GPIO33 is the shared touchpad reset. */
    ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_14, 0));
    ESP_ERROR_CHECK(gpio_set_direction(GPIO_NUM_14, GPIO_MODE_OUTPUT));
    gpio_config_t interrupt = {.pin_bit_mask = 1ULL << TP_INT_GPIO, .mode = GPIO_MODE_INPUT,
        .intr_type = GPIO_INTR_DISABLE, .pull_up_en = GPIO_PULLUP_DISABLE, .pull_down_en = GPIO_PULLDOWN_DISABLE};
    ESP_ERROR_CHECK(gpio_config(&interrupt));
    ESP_ERROR_CHECK(gpio_set_level(TP_RESET_GPIO, 0));
    ESP_ERROR_CHECK(gpio_set_direction(TP_RESET_GPIO, GPIO_MODE_OUTPUT));
    pause_ms(50); ESP_ERROR_CHECK(gpio_set_level(TP_RESET_GPIO, 1)); pause_ms(150);
    i2c_master_bus_config_t bus = {.clk_source = I2C_CLK_SRC_DEFAULT, .i2c_port = TP_I2C_PORT,
        .sda_io_num = TP_I2C_SDA, .scl_io_num = TP_I2C_SCL, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false};
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus, &probe_bus));
    i2c_device_config_t device = {.dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TP_I2C_ADDR, .scl_speed_hz = I2C_FREQ_HZ};
    ESP_ERROR_CHECK(i2c_master_bus_add_device(probe_bus, &device, &probe_dev));
    esp_err_t err = probe();
    if (err == ESP_OK) err = experiment();
    if (err != ESP_OK) ESP_LOGE(TAG, "RESULT: diagnostic/experiment incomplete: %s", esp_err_to_name(err));
    /* Clear mode/page and outstanding reads after both success and failure. */
    ESP_ERROR_CHECK(gpio_set_level(TP_RESET_GPIO, 0)); pause_ms(50);
    ESP_ERROR_CHECK(gpio_set_level(TP_RESET_GPIO, 1)); pause_ms(150);
    ESP_LOGI(TAG, "DONE: controller reset; write_attempted=%u restore_verified=%u; reboot to repeat", border_test.attempted, border_test.restored);
}
