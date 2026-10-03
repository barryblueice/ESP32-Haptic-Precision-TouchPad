#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#define SYNAPTICS_IMAGE_ID UINT32_C(0x003540D2)
#define SYNAPTICS_IMAGE_CHECKSUM UINT32_C(0x74C396A6)
#define SYNAPTICS_IMAGE_PRODUCT "TM3651-001"
#define SYNAPTICS_IMAGE_SHA256 "08657877a9ce4709fc138162a9b4ee086b6a9b921dde6eafef4f682bc74de3ee"
#define SYNAPTICS_PARTITIONS 3

typedef struct {
    uint8_t partition;
    uint16_t container;
    const char *name;
    const uint8_t *data;
    size_t size;
} synaptics_partition_t;
typedef struct {
    synaptics_partition_t parts[SYNAPTICS_PARTITIONS];
    uint32_t firmware_id;
    char product[11];
    uint8_t bl_major,bl_minor;
    unsigned containers;
} synaptics_image_t;
/* Parse the original RMI 0x10 image; only selectors 03/07/08 are writable. */
esp_err_t synaptics_image_open(synaptics_image_t *image);
