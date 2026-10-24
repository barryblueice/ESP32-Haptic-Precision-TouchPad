/* Startup-only Synaptics diagnostics. No tuning, calibration or flash writes.
 * RMI/HID parsing derived from the exercised border diagnostic fixture.
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
#include "synaptics_border.h"

#define TAG "SYNA_BORDER"
#define TRY(call) do { esp_err_t e = (call); if (e != ESP_OK) return e; } while (0)
static i2c_master_dev_handle_t probe_dev;
static uint16_t command_reg, data_reg, output_reg, input_size, output_size;
static uint16_t report_bytes[16][3]; /* input/output/feature, including ID */
static uint8_t descriptor[1024], structure[512];
static int selected_page = -1;
static int64_t probe_deadline;
static bool read_channel_failed;
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
typedef struct {
    bool found;
    uint16_t query, control, data, command;
    uint8_t version;
} rmi_function_t;
static rmi_function_t f01, f11, f12, f34, f54, f55, f51;
static unsigned irq_count;
static bool known_tm3651;
static uint8_t mapped_rx, mapped_tx;
static bool mapping_valid;
static uint8_t report_image[8192];
static uint8_t register_value[512];
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
            rmi_function_t *fn = p[5] == 1 ? &f01 : p[5] == 0x11 ? &f11 : p[5] == 0x12 ? &f12 :
                p[5] == 0x34 ? &f34 : p[5] == 0x54 ? &f54 :
                p[5] == 0x55 ? &f55 : p[5] == 0x51 ? &f51 : NULL;
            irq_count += p[4] & 7;
            if (irq_count > 256) return ESP_ERR_INVALID_SIZE;
            if (fn) {
                if (fn->found) return ESP_ERR_NOT_SUPPORTED;
                *fn = (rmi_function_t){true, base | p[0], base | p[2], base | p[3],
                                      base | p[1], (p[4] >> 5) & 3};
            }
        }
        empty = any ? 0 : empty + 1;
    }
    if (!f01.found) return ESP_ERR_NOT_FOUND;
    uint8_t id[21], status;
    TRY(rmi_read(f01.query, id, sizeof(id))); TRY(rmi_read(f01.data, &status, 1));
    char product[11]; memcpy(product, id + 11, 10); product[10] = 0;
    known_tm3651 = id[0] == 1 && memcmp(product, "TM3651-001", 10) == 0;
    ESP_LOGI(TAG, "F01 manufacturer=%u product=%.10s status=%02x", id[0], product, status);
    if (id[0] != 1 || (status & 0x40)) return ESP_ERR_INVALID_STATE;
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
/* Source layouts: Linux drivers/input/rmi4/rmi_f01.c, rmi_f12.c,
 * rmi_f34.h/rmi_f34v7.c, rmi_f54.c and rmi_f55.c (Synaptics contributions).
 * Packet registers count addresses, not payload bytes. Unknown payloads are
 * labelled RAW; descriptor presence alone does not establish their meaning.
 */
static esp_err_t read_offset(uint16_t base, unsigned offset, uint8_t *dst, size_t len)
{
    if (offset > 0xffffU - base) return ESP_ERR_INVALID_SIZE;
    return rmi_read(base + offset, dst, len);
}
static esp_err_t dump_register(const char *kind, unsigned reg, uint16_t base,
                               unsigned offset, size_t len)
{
    if (!len || len > sizeof(register_value)) {
        ESP_LOGW(TAG, "%s%u SKIPPED size=%u exceeds bounded reader", kind, reg, (unsigned)len);
        return ESP_ERR_NOT_SUPPORTED;
    }
    TRY(read_offset(base, offset, register_value, len));
    ESP_LOGI(TAG, "%s%u addr=%04x bytes=%u RAW:", kind, reg, base + offset, (unsigned)len);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, register_value, len, ESP_LOG_INFO);
    return ESP_OK;
}
static esp_err_t query_f01_details(void)
{
    uint8_t q[21], ctrl, status;
    TRY(rmi_read(f01.query, q, sizeof(q)));
    ESP_LOGI(TAG, "F01 basic query RAW:");
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, q, sizeof(q), ESP_LOG_INFO);
    TRY(rmi_read(f01.control, &ctrl, 1));
    TRY(rmi_read(f01.data, &status, 1));
    ESP_LOGI(TAG, "F01 status=%02x code=%u unconfigured=%u bootloader=%u control0=%02x sleep=%u nosleep=%u configured=%u",
        status, status & 15, !!(status & 0x80), !!(status & 0x40), ctrl,
        ctrl & 3, !!(ctrl & 4), !!(ctrl & 0x80));
    ESP_LOGI(TAG, "F01 product_info=%u date_code=20%02u-%02u-%02u (device fields)",
        ((q[2] & 0x7f) << 7) | (q[3] & 0x7f), q[5] & 31, q[6] & 15, q[7] & 31);
    unsigned irq_bytes = (irq_count + 7) / 8;
    if (irq_bytes) {
        TRY(dump_register("F01 IRQ_ENABLE", 0, f01.control, 1, irq_bytes));
        /* Reading interrupt status may acknowledge pending interrupts. No reader
         * is active yet; diagnostics are followed by a hardware reset. */
        TRY(dump_register("F01 IRQ_STATUS", 0, f01.data, 1, irq_bytes));
    }
    unsigned control_offset = 1 + irq_bytes;
    if (q[1] & 0x20) {
        TRY(dump_register("F01 DOZE_INTERVAL_WAKE_THRESHOLD", 0, f01.control, control_offset, 2));
        control_offset += 2;
    }
    if (q[1] & 4) { TRY(dump_register("F01 LTS_CONTROL", 0, f01.control, control_offset, 1)); ++control_offset; }
    if (q[1] & 0x40)
        TRY(dump_register("F01 DOZE_HOLDOFF", 0, f01.control, control_offset, 1));
    unsigned offset = 21;
    if (q[1] & 8) { TRY(dump_register("F01 SENSOR_ID", 0, f01.query, offset, 1)); ++offset; }
    bool build_read = false;
    if (q[1] & 0x80) {
        uint8_t q42; TRY(read_offset(f01.query, offset++, &q42, 1));
        ESP_LOGI(TAG, "F01 Query42=%02x", q42);
        if (q42 & 1) {
            uint8_t len; TRY(read_offset(f01.query, offset++, &len, 1));
            if (len) {
                uint8_t flags; TRY(read_offset(f01.query, offset, &flags, 1));
                ESP_LOGI(TAG, "F01 DS4 length=%u flags=%02x", len, flags);
                unsigned info = 17;
                if (flags & 1) { TRY(dump_register("F01 PACKAGE_INFO", 0, f01.query, info++, 8)); }
                if (flags & 2) {
                    uint8_t id[3]; TRY(read_offset(f01.query, info, id, 3));
                    ESP_LOGI(TAG, "F01 firmware_build_id=%lu raw=%02x%02x%02x",
                        (unsigned long)(u16(id) | ((uint32_t)id[2] << 16)), id[0], id[1], id[2]);
                    build_read = true;
                }
            }
        }
    }
    if (!build_read && known_tm3651) {
        /* This module's packet offset is also used by the existing updater;
         * never apply the module-specific fallback to an unknown product. */
        uint8_t id[3]; TRY(read_offset(f01.query, 0x12, id, 3));
        ESP_LOGI(TAG, "F01 TM3651 firmware_build_id=%lu raw=%02x%02x%02x",
            (unsigned long)(u16(id) | ((uint32_t)id[2] << 16)), id[0], id[1], id[2]);
    } else if (!build_read) ESP_LOGI(TAG, "F01 firmware build query not advertised");
    return ESP_OK;
}
static esp_err_t dump_f12_descriptor(unsigned descriptor_offset, const char *kind,
                                     uint16_t value_base, bool read_values)
{
    uint8_t plen, presence[35];
    TRY(read_offset(f12.query, descriptor_offset, &plen, 1));
    if (!plen || plen > sizeof(presence)) return ESP_ERR_INVALID_SIZE;
    TRY(read_offset(f12.query, descriptor_offset + 1, presence, plen));
    unsigned start = presence[0] ? 1 : 3;
    if (plen < start) return ESP_ERR_INVALID_SIZE;
    unsigned size = presence[0] ? presence[0] : u16(presence + 1);
    if (!size || size > sizeof(structure)) return ESP_ERR_INVALID_SIZE;
    TRY(read_offset(f12.query, descriptor_offset + 2, structure, size));
    ESP_LOGI(TAG, "%s descriptor presence RAW:", kind);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, presence, plen, ESP_LOG_INFO);
    ESP_LOGI(TAG, "%s descriptor structure RAW:", kind);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, structure, size, ESP_LOG_INFO);
    unsigned cursor = 0, address_offset = 0;
    esp_err_t result = ESP_OK;
    for (unsigned reg = 0; reg < (plen - start) * 8; ++reg) {
        if (!(presence[start + reg / 8] & (1U << (reg % 8)))) continue;
        if (cursor >= size) return ESP_ERR_INVALID_SIZE;
        uint32_t bytes = structure[cursor++];
        if (!bytes) {
            if (size - cursor < 2) return ESP_ERR_INVALID_SIZE;
            bytes = u16(structure + cursor); cursor += 2;
            if (!bytes) {
                if (size - cursor < 4) return ESP_ERR_INVALID_SIZE;
                bytes = u32(structure + cursor); cursor += 4;
            }
        }
        if (!bytes) return ESP_ERR_INVALID_SIZE;
        unsigned map_start = cursor, subpackets = 0;
        uint8_t map;
        do {
            if (cursor >= size) return ESP_ERR_INVALID_SIZE;
            map = structure[cursor++];
            for (unsigned bit = 0; bit < 7; ++bit) subpackets += !!(map & (1U << bit));
        } while (map & 0x80);
        ESP_LOGI(TAG, "%s%u offset=%u bytes=%lu subpackets=%u bitmap RAW:",
            kind, reg, address_offset, (unsigned long)bytes, subpackets);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, structure + map_start, cursor - map_start, ESP_LOG_INFO);
        if (read_values) {
            esp_err_t err = dump_register(kind, reg, value_base, address_offset, bytes);
            if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) return err;
            if (err != ESP_OK) result = err;
            /* Object records are decoded only for the documented full 8-byte
             * layout. Other layouts remain raw rather than inventing fields. */
            if (err == ESP_OK && descriptor_offset == 7 && reg == 1 &&
                subpackets && bytes == subpackets * 8) {
                for (unsigned i = 0; i < subpackets; ++i) {
                    const uint8_t *v = register_value + 8 * i;
                    ESP_LOGI(TAG, "F12 object_record=%u type=%u x=%u y=%u z=%u wx=%u wy=%u",
                        i, v[0], u16(v + 1), u16(v + 3), v[5], v[6], v[7]);
                }
            }
        }
        ++address_offset;
    }
    if (cursor != size) return ESP_ERR_INVALID_SIZE;
    return result;
}
static esp_err_t query_f12_details(void)
{
    uint8_t general; TRY(rmi_read(f12.query, &general, 1));
    if (!(general & 1)) return ESP_ERR_NOT_SUPPORTED;
    /* Use descriptor-calculated packet offsets, as for F12 Query29 in the
     * public driver; do not assume payloads begin after the descriptors. */
    const char *kinds[] = {"F12 QUERY", "F12 CONTROL", "F12 DATA"};
    const uint16_t bases[] = {f12.query, f12.control, f12.data};
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < 3; ++i) {
        esp_err_t err = dump_f12_descriptor(1 + 3 * i, kinds[i], bases[i], true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s dump INCOMPLETE: %s", kinds[i], esp_err_to_name(err)); result = err;
            if (read_channel_failed) return err;
        }
    }
    return result;
}
static esp_err_t query_f34_details(void)
{
    if (f34.version != 2) return ESP_ERR_NOT_SUPPORTED;
    uint8_t q0; TRY(rmi_read(f34.query, &q0, 1));
    ESP_LOGI(TAG, "F34 Query0=%02x version=%u", q0, f34.version);
    /* V7 Query1..7 are packet registers, with the query0 subpacket offset. */
    const uint8_t lengths[] = {2, 4, 5, 2, 2, 2, 4};
    unsigned first = (q0 & 7) + 1;
    for (unsigned i = 0; i < sizeof(lengths); ++i) {
        TRY(dump_register("F34 QUERY", i + 1, f34.query, first + i, lengths[i]));
        if (i == 0) ESP_LOGI(TAG, "F34 bootloader=%u.%u", register_value[1], register_value[0]);
        if (i == 1) ESP_LOGI(TAG, "F34 bootloader_fw_id=%lu", (unsigned long)u32(register_value));
        if (i == 2) ESP_LOGI(TAG, "F34 min_write=%u block_size=%u flash_page_size=%u",
            register_value[0], u16(register_value + 1), u16(register_value + 3));
    }
    if (q0 & 8) TRY(dump_register("F34 CONFIG_ID", 0, f34.control, 0, 32));
    else ESP_LOGI(TAG, "F34 CONFIG_ID not advertised");
    uint8_t status, command;
    TRY(rmi_read(f34.data, &status, 1)); TRY(read_offset(f34.data, 4, &command, 1));
    ESP_LOGI(TAG, "F34 status=%02x flash_status=%u bootloader=%u command=%02x; no flash commands issued",
        status, status & 31, !!(status & 0x80), command);
    return ESP_OK;
}
static esp_err_t query_f55_details(void)
{
    if (f55.version != 0) return ESP_ERR_NOT_SUPPORTED;
    uint8_t q[3]; TRY(rmi_read(f55.query, q, sizeof(q)));
    ESP_LOGI(TAG, "F55 rx=%u tx=%u flags=%02x sensor_assignment=%u", q[0], q[1], q[2], !!(q[2] & 1));
    if (!q[0] || !q[1]) return ESP_ERR_INVALID_SIZE;
    mapped_rx = q[0]; mapped_tx = q[1];
    if (q[2] & 1) {
        TRY(dump_register("F55 RX_MAP", 0, f55.control, 1, q[0]));
        mapped_rx = 0;
        for (unsigned i = 0; i < q[0]; ++i) mapped_rx += register_value[i] != 0xff;
        TRY(dump_register("F55 TX_MAP", 0, f55.control, 2, q[1]));
        mapped_tx = 0;
        for (unsigned i = 0; i < q[1]; ++i) mapped_tx += register_value[i] != 0xff;
    }
    ESP_LOGI(TAG, "F55 enabled_rx=%u enabled_tx=%u; ff map entries are disabled", mapped_rx, mapped_tx);
    if (!mapped_rx || !mapped_tx) return ESP_ERR_INVALID_SIZE;
    mapping_valid = true;
    return ESP_OK;
}
/* Only F54 report type, FIFO index and GetReport can be written here. There is
 * deliberately no generic control write, ForceCal or ForceUpdate operation. */
static esp_err_t f54_request_write(uint16_t addr, const uint8_t *data, size_t len)
{
    bool type = addr == f54.data && len == 1 &&
        (data[0] == 1 || data[0] == 2 || data[0] == 3 || data[0] == 9 || data[0] == 19 || data[0] == 20);
    bool fifo = addr == f54.data + 1U && len == 2;
    bool get = addr == f54.command && len == 1 && data[0] == 1;
    if (!f54.found || f54.version || (!type && !fifo && !get) || read_channel_failed)
        return ESP_ERR_INVALID_STATE;
    if (esp_timer_get_time() >= probe_deadline) return ESP_ERR_TIMEOUT;
    TRY(select_page(addr >> 8));
    uint8_t report[6] = {9, len}; put16(report + 2, addr);
    memcpy(report + 4, data, len);
    esp_err_t err = send_output(report, len + 4);
    if (err != ESP_OK) read_channel_failed = true;
    return err;
}
static esp_err_t capture_f54(uint8_t type, unsigned rx, unsigned tx)
{
    unsigned bytes_per_cell = type == 1 ? 1 : 2;
    size_t size = rx * tx * bytes_per_cell;
    if (!rx || !tx || !size || size > sizeof(report_image) || f54.data > 0xfffc)
        return ESP_ERR_INVALID_SIZE;
    uint8_t command;
    TRY(rmi_read(f54.command, &command, 1));
    if (command) return ESP_ERR_INVALID_STATE;
    ESP_LOGI(TAG, "F54 CAPTURE type=%u rx=%u tx=%u bytes=%u", type, rx, tx, (unsigned)size);
    TRY(f54_request_write(f54.data, &type, 1));
    command = 1; TRY(f54_request_write(f54.command, &command, 1));
    int64_t deadline = esp_timer_get_time() + 1000000;
    do {
        pause_ms(10); TRY(rmi_read(f54.command, &command, 1));
        if (esp_timer_get_time() >= deadline && command) return ESP_ERR_TIMEOUT;
    } while (command);
    for (size_t offset = 0; offset < size; offset += 32) {
        uint8_t fifo[2]; put16(fifo, offset);
        TRY(f54_request_write(f54.data + 1, fifo, sizeof(fifo)));
        size_t count = size - offset; if (count > 32) count = 32;
        TRY(rmi_read(f54.data + 3, report_image + offset, count));
    }
    ESP_LOGI(TAG, "F54 IMAGE type=%u encoding=%s rx=%u tx=%u; sensor order, not installation orientation",
        type, type == 1 ? "s8" : (type == 2 ? "s16le" : "u16le"), rx, tx);
    for (unsigned row = 0; row < tx; ++row) {
        ESP_LOGI(TAG, "F54 ROW type=%u tx_index=%u bytes=%u RAW:", type, row, rx * bytes_per_cell);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, report_image + row * rx * bytes_per_cell,
                                rx * bytes_per_cell, ESP_LOG_INFO);
    }
    ESP_LOGI(TAG, "F54 IMAGE_COMPLETE type=%u bytes=%u", type, (unsigned)size);
    return ESP_OK;
}
static esp_err_t query_f54_details(void)
{
    if (f54.version != 0 || !(f54.command & 0xff)) return ESP_ERR_NOT_SUPPORTED;
    uint8_t q[6]; TRY(rmi_read(f54.query, q, sizeof(q)));
    ESP_LOGI(TAG, "F54 rx=%u tx=%u capabilities=%02x clock_raw=%u family=%u image8=%u image16=%u baseline=%u",
        q[0], q[1], q[2], u16(q + 3), q[5], !!(q[2] & 8), !!(q[2] & 0x40), !!(q[2] & 4));
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, q, sizeof(q), ESP_LOG_INFO);
    TRY(dump_register("F54 CONTROL", 0, f54.control, 0, 1));
    unsigned rx = mapping_valid ? mapped_rx : q[0], tx = mapping_valid ? mapped_tx : q[1];
    if ((f55.found && !mapping_valid) || !rx || !tx || rx > q[0] || tx > q[1]) {
        ESP_LOGW(TAG, "F54 capture SKIPPED: electrode geometry unverified");
        return ESP_ERR_NOT_SUPPORTED;
    }
    const uint8_t types[] = {1, 2, 3, 9, 19, 20};
    for (unsigned i = 0; i < sizeof(types); ++i) {
        uint8_t type = types[i];
        bool supported = type == 1 ? !!(q[2] & 8) :
            type == 2 || type == 3 ? !!(q[2] & 0x40) :
            type == 9 ? !!((q[2] & 0x40) && (q[2] & 4)) : true;
        if (!supported) { ESP_LOGI(TAG, "F54 type=%u SKIPPED: capability not advertised", type); continue; }
        if (type >= 19) ESP_LOGI(TAG, "F54 type=%u standard diagnostic; support confirmed only on completion", type);
        esp_err_t err = capture_f54(type, rx, tx);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "F54 type=%u INCOMPLETE: %s; remaining captures stopped", type, esp_err_to_name(err));
            return err;
        }
    }
    return ESP_OK;
}
static esp_err_t query_details(void)
{
    struct query { const char *name; rmi_function_t *fn; esp_err_t (*run)(void); } queries[] = {
        {"F01", &f01, query_f01_details}, {"F12", &f12, query_f12_details},
        {"F34", &f34, query_f34_details}, {"F55", &f55, query_f55_details},
    };
    esp_err_t result = ESP_OK;
    for (unsigned i = 0; i < sizeof(queries) / sizeof(queries[0]); ++i) {
        if (!queries[i].fn->found) { ESP_LOGI(TAG, "%s ABSENT", queries[i].name); continue; }
        esp_err_t err = queries[i].run();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "%s details INCOMPLETE: %s", queries[i].name, esp_err_to_name(err)); result = err;
            if (read_channel_failed) return err;
        }
    }
    if (f51.found) {
        /* No public TM3651 F51 v2 schema/length available. One Query0 byte is
         * retained as raw evidence; aliased PDT bases do not establish sizes. */
        ESP_LOGW(TAG, "F51 version=%u schema UNKNOWN; Query0 only, remaining layout not inferred", f51.version);
        TRY(dump_register("F51 QUERY", 0, f51.query, 0, 1));
    } else ESP_LOGI(TAG, "F51 ABSENT");
    /* Diagnostic commands run last, after passive snapshots. */
    if (f54.found) {
        esp_err_t err = query_f54_details();
        if (err != ESP_OK) result = err;
    } else ESP_LOGI(TAG, "F54 ABSENT");
    return result;
}
static esp_err_t probe(void)
{
    TRY(discover_hid());
    const uint8_t power[] = {0, 8}, reset[] = {0, 1};
    TRY(hid_write(command_reg, power, sizeof(power))); pause_ms(50);
    TRY(hid_write(command_reg, reset, sizeof(reset))); pause_ms(100);
    TRY(drain_input()); TRY(set_rmi_mode(1));
    probe_deadline = esp_timer_get_time() + 60000000;
    TRY(scan_pdt());
    esp_err_t result = ESP_OK;
    if (f11.found) {
        esp_err_t err = query_f11();
        if (err != ESP_OK) { ESP_LOGW(TAG, "F11 query: %s", esp_err_to_name(err)); result = err; }
        if (err != ESP_OK && err != ESP_ERR_NOT_SUPPORTED) return err;
    }
    if (f12.found) {
        esp_err_t err = query_f12();
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "F12 border query INCOMPLETE: %s", esp_err_to_name(err)); result = err;
            if (read_channel_failed) return err;
        }
    }
    if (!f11.found && !f12.found) {
        ESP_LOGW(TAG, "RESULT: neither F11 nor F12 found"); result = ESP_ERR_NOT_FOUND;
    }
    esp_err_t details = query_details();
    return result != ESP_OK ? result : details;
}
esp_err_t synaptics_border_log_startup(i2c_master_dev_handle_t device)
{
    /* Exclusive ownership: caller has not started the input/parser tasks.
     * Caller MUST hardware-reset before normal HID init, including on failure,
     * to clear RMI mode, page selection and any late read responses.
     */
    probe_dev = device;
    selected_page = -1;
    read_channel_failed = false;
    f01 = (rmi_function_t){0};
    f11 = (rmi_function_t){0};
    f12 = (rmi_function_t){0};
    f34 = (rmi_function_t){0}; f54 = (rmi_function_t){0};
    f55 = (rmi_function_t){0}; f51 = (rmi_function_t){0};
    irq_count = 0; known_tm3651 = false;
    mapped_rx = mapped_tx = 0; mapping_valid = false;
    ESP_LOGI(TAG, "START: controller diagnostics; no tuning/calibration/flash writes");
    esp_err_t err = probe();
    if (err != ESP_OK)
        ESP_LOGW(TAG, "INCOMPLETE: %s; do not infer unsupported or zero borders", esp_err_to_name(err));
    else
        ESP_LOGI(TAG, "DONE: query complete; writability/effect/persistence NOT TESTED");
    return err;
}
