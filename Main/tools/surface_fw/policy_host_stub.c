/* Host-only BSP recorder. Compile with the production policy and BSP headers. */
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"

#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif

static uint32_t fields[10];
static uint32_t mapping_status;
static uint32_t trigger_status;

EXPORT void policy_test_reset(uint32_t mapping_result, uint32_t trigger_result)
{
    for (unsigned int i = 0; i < 10; ++i) {
        fields[i] = 0;
    }
    mapping_status = mapping_result;
    trigger_status = trigger_result;
}

EXPORT uint32_t policy_test_field(unsigned int index)
{
    return index < 10 ? fields[index] : 0xFFFFFFFFU;
}

EXPORT uint32_t policy_test_ok(void) { return BSP_STATUS_OK; }
EXPORT uint32_t policy_test_fail(void) { return BSP_STATUS_FAIL; }

uint32_t bsp_dut_apply_haptic_mapping(uint8_t press, uint8_t release,
                                     uint16_t cp, uint16_t gpi, bool gpio_enable)
{
    ++fields[0];
    fields[2] = press;
    fields[3] = release;
    fields[4] = cp;
    fields[5] = gpi;
    fields[6] = gpio_enable;
    fields[9] = fields[9] * 10 + 1;
    return mapping_status;
}

uint32_t bsp_dut_trigger_haptic(uint8_t index, uint32_t duration_ms)
{
    ++fields[1];
    fields[7] = index;
    fields[8] = duration_ms;
    fields[9] = fields[9] * 10 + 2;
    return trigger_status;
}
