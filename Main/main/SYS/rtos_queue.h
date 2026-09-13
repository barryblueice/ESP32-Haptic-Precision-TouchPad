#ifndef RTOS_QUEUE_H
#define RTOS_QUEUE_H

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_timer.h"

extern QueueHandle_t tp_data_queue;

#endif