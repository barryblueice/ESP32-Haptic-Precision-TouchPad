#include "driver/i2c_master.h"
#include "esp_log.h"

#include "I2C/SUB_DEV/sub_dev.h"

#define TAG "MP28167"

esp_err_t mp28167_set_vref_mv(uint16_t mv) {
    esp_err_t err;

    uint16_t v_int = (uint16_t)((mv / 0.8f) + 0.5f);
    if (v_int > 2047) v_int = 2047;

    uint8_t v_h = (uint8_t)((v_int >> 3) & 0xFF);
    uint8_t v_l = (uint8_t)(v_int & 0x07);

    uint8_t data_l[] = {MP28167_REG_VREF_L, v_l};
    uint8_t data_h[] = {MP28167_REG_VREF_H, v_h};
    uint8_t data_go[] = {MP28167_REG_VREF_GO, 0x01};

    err = i2c_master_transmit(sub_dev_mp28167_handle, data_l, 2, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to write VREF_L: 0x%x", err);
        return err;
    }

    err = i2c_master_transmit(sub_dev_mp28167_handle, data_h, 2, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to write VREF_H: 0x%x", err);
        return err;
    }

    err = i2c_master_transmit(sub_dev_mp28167_handle, data_go, 2, -1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "failed to write VREF_GO: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG, "VREF set to %d mV (Raw: 0x%03X)", mv, v_int);

    return ESP_OK;

}

float mp28167_get_vref_mv() {
    uint8_t addr_h = MP28167_REG_VREF_H;
    uint8_t addr_l = MP28167_REG_VREF_L;
    uint8_t val_h, val_l;

    i2c_master_transmit_receive(sub_dev_mp28167_handle, &addr_h, 1, &val_h, 1, -1);
    i2c_master_transmit_receive(sub_dev_mp28167_handle, &addr_l, 1, &val_l, 1, -1);

    uint16_t v_raw = ((uint16_t)val_h << 3) | (val_l & 0x07);
    return v_raw * 0.8f;

}
