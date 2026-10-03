#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"
typedef struct {
    uint16_t query,command,control,data;
    uint8_t version;
} synaptics_function_t;
typedef enum { RMI_FAULT_NONE, RMI_FAULT_TX, RMI_FAULT_RX, RMI_FAULT_FRAME,
               RMI_FAULT_DEADLINE } synaptics_rmi_fault_t;
void synaptics_rmi_bind(i2c_master_dev_handle_t device);
esp_err_t synaptics_rmi_mode(bool enabled);
esp_err_t synaptics_rmi_write(uint16_t reg,const uint8_t *data,size_t size);
esp_err_t synaptics_rmi_read(uint16_t reg,uint8_t *data,size_t size);
/* Absolute esp_timer microseconds; no new request or receive after deadline. */
esp_err_t synaptics_rmi_read_until(uint16_t reg,uint8_t *data,size_t size,int64_t deadline);
synaptics_rmi_fault_t synaptics_rmi_fault(void);
/* Local recovery policy after request-TX error, not a SAM reset operation. */
esp_err_t synaptics_rmi_drain_until(int64_t deadline);
esp_err_t synaptics_rmi_discover(synaptics_function_t *f01,synaptics_function_t *f34);
