#include "surface_haptic_policy.h"

#include <stddef.h>

#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/bsp_dut.h"
#include "I2C/SUB_DEV/mcu-drivers/cs40l25/bsp/surface_fw_metadata.h"

bool surface_haptic_resolve(uint8_t setting, surface_haptic_pair_t *out)
{
    if (out == NULL || setting > 100U) {
        return false;
    }

    surface_haptic_pair_t pair = {.enabled = setting != 0U};
    if (setting == 0U) {
        // Observed SAM result, not a valid index into this 78-entry wave table.
        pair.press_index = 100U;
        pair.release_index = 100U;
    } else if (setting < 39U) {
        pair.press_index = 17U;
        pair.release_index = 13U;
    } else if (setting < 64U) {
        pair.press_index = 21U;
        pair.release_index = 15U;
    } else if (setting < 89U) {
        pair.press_index = 24U;
        pair.release_index = 17U;
    } else {
        pair.press_index = 36U;
        pair.release_index = 29U;
    }
    *out = pair;
    return true;
}

uint32_t surface_haptic_play_event(const surface_haptic_pair_t *pair, bool release)
{
    if (pair == NULL) {
        return BSP_STATUS_FAIL;
    }
    if (!pair->enabled) {
        return BSP_STATUS_OK;
    }
    if (pair->press_index >= SURFACE_FW_NUM_WAVES ||
        pair->release_index >= SURFACE_FW_NUM_WAVES) {
        return BSP_STATUS_FAIL;
    }

    uint32_t status = bsp_dut_apply_haptic_mapping(pair->press_index,
                                                  pair->release_index, 0, 0, false);
    if (status != BSP_STATUS_OK) {
        return status;
    }
    return bsp_dut_trigger_haptic(release ? pair->release_index : pair->press_index, 0);
}
