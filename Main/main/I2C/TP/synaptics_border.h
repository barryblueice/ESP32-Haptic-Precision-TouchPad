#pragma once
#include "driver/i2c_master.h"
#include "esp_err.h"

/* Temporary startup RMI diagnostics, disabled in normal operation. */
#define SYNA_STARTUP_DIAGNOSTICS_ENABLED 0

/* Startup only, with exclusive I2C/input ownership and INT configured.
 * Reads Synaptics capabilities/register snapshots and requests F54 reports.
 * Does not change sensor tuning, calibrate, or issue flash commands.
 * Caller must hardware-reset afterwards before starting normal HID readers,
 * even on failure (the query changes transport mode and page selection).
 */
esp_err_t synaptics_border_log_startup(i2c_master_dev_handle_t device);
