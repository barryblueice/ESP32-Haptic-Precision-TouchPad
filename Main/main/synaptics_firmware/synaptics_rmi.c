/* Wire encoding: SAM DF238/DF1F0/DF09A/DEF8A. 7-bit address 0x2C.
 * Port differences: finite deadlines, strict complete payloads, no automatic
 * replay of an accepted read request (SAM can resend before its first payload).
 * A5 fill is diagnostic only. ESP-IDF provides no actual received byte count.
 */
#include "sdkconfig.h"
#if CONFIG_SURFACE_SYNAPTICS_UPDATE_MODE
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "I2C/I2C_handle.h"
#include "synaptics_rmi.h"
#define TAG "SYNAPTICS_RMI"
#define IO_MS 100
static i2c_master_dev_handle_t device;
static synaptics_rmi_fault_t fault;
static uint16_t le16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
static void pause_tick(void) { vTaskDelay(1); }
static int timeout_ms(int64_t deadline)
{
    int64_t remaining=deadline-esp_timer_get_time();
    if (remaining<=0) return 0;
    int64_t ms=(remaining+999)/1000;return ms>IO_MS ? IO_MS : (int)ms;
}
void synaptics_rmi_bind(i2c_master_dev_handle_t dev) { device=dev;fault=RMI_FAULT_NONE; }
synaptics_rmi_fault_t synaptics_rmi_fault(void) { return fault; }
static esp_err_t tx(const uint8_t *raw,size_t size,int timeout)
{
    esp_err_t err=i2c_master_transmit(device,raw,size,timeout);
    if (err!=ESP_OK) {
        fault=RMI_FAULT_TX;
        ESP_LOGE(TAG,"TX addr7=2C len=%u ret=%s",(unsigned)size,esp_err_to_name(err));
        ESP_LOG_BUFFER_HEX_LEVEL(TAG,raw,size,ESP_LOG_ERROR);
    }
    return err;
}
esp_err_t synaptics_rmi_mode(bool enabled)
{
    const uint8_t raw[]={0x22,0,0x3F,3,0x0F,0x23,0,4,0,0x0F,enabled?3:0};
    fault=RMI_FAULT_NONE;esp_err_t err=tx(raw,sizeof(raw),IO_MS);
    ESP_LOGI(TAG,"MODE value=%u ret=%s",enabled?3:0,esp_err_to_name(err));return err;
}
esp_err_t synaptics_rmi_write(uint16_t reg,const uint8_t *data,size_t size)
{
    fault=RMI_FAULT_NONE;
    if (!data || !size || size>17) return ESP_ERR_INVALID_SIZE;
    uint8_t raw[25]={0x25,0,0x17,0,9};
    raw[5]=size;raw[6]=reg;raw[7]=reg>>8;memcpy(raw+8,data,size);
    return tx(raw,sizeof(raw),IO_MS); /* Control/data writes are NEVER retried. */
}
esp_err_t synaptics_rmi_read_until(uint16_t reg,uint8_t *data,size_t size,int64_t deadline)
{
    fault=RMI_FAULT_NONE;
    if (!data || !size || size>1024) return ESP_ERR_INVALID_SIZE;
    memset(data,0xA5,size);
    int timeout=timeout_ms(deadline);
    if (!timeout) {fault=RMI_FAULT_DEADLINE;return ESP_ERR_TIMEOUT;}
    uint8_t raw[25]={0x25,0,0x17,0,0x0A};
    raw[5]=size<256 ? size : 0;raw[6]=reg;raw[7]=reg>>8;
    if (size>=256) {raw[8]=size;raw[9]=size>>8;}
    esp_err_t err=tx(raw,sizeof(raw),timeout);
    if (err!=ESP_OK) return err;
    size_t received=0;
    for (unsigned reports=0;reports<64;++reports) {
        while (gpio_get_level(TP_INT_GPIO) && esp_timer_get_time()<deadline) pause_tick();
        timeout=timeout_ms(deadline);if(!timeout) break;
        uint8_t report[64];memset(report,0xA5,sizeof(report));
        err=i2c_master_receive(device,report,sizeof(report),timeout);
        if (err!=ESP_OK) {
            fault=RMI_FAULT_RX;
            ESP_LOGE(TAG,"RX reg=%04X request=%u assembled=%u rx_actual=UNAVAILABLE ret=%s",
                reg,(unsigned)size,(unsigned)received,esp_err_to_name(err));return err;
        }
        if (le16(report)==0 || (le16(report)==64 && report[2]==0x0C)) continue;
        size_t n=report[3];
        if (le16(report)!=64 || report[2]!=0x0B || !n || n>60 || n>size-received) {
            fault=RMI_FAULT_FRAME;
            ESP_LOGE(TAG,"FRAME_INVALID reg=%04X request=%u assembled=%u",reg,(unsigned)size,(unsigned)received);
            ESP_LOG_BUFFER_HEX_LEVEL(TAG,report,sizeof(report),ESP_LOG_ERROR);return ESP_ERR_INVALID_RESPONSE;
        }
        memcpy(data+received,report+4,n);received+=n;
        if (received==size && esp_timer_get_time()<deadline) return ESP_OK;
    }
    fault=RMI_FAULT_DEADLINE;
    ESP_LOGE(TAG,"READ_INCOMPLETE reg=%04X assembled=%u/%u",reg,(unsigned)received,(unsigned)size);
    return ESP_ERR_TIMEOUT;
}
esp_err_t synaptics_rmi_read(uint16_t reg,uint8_t *data,size_t size)
{
    return synaptics_rmi_read_until(reg,data,size,esp_timer_get_time()+1000000);
}
esp_err_t synaptics_rmi_drain_until(int64_t deadline)
{
    fault=RMI_FAULT_NONE;
    int64_t quiet=0;unsigned count=0;
    while (esp_timer_get_time()<deadline && count<16) {
        if (!gpio_get_level(TP_INT_GPIO)) {
            quiet=0;uint8_t report[64];int timeout=timeout_ms(deadline);if(!timeout) break;
            esp_err_t err=i2c_master_receive(device,report,sizeof(report),timeout);
            if (err!=ESP_OK) {fault=RMI_FAULT_RX;return err;}
            ++count;
            /* Only discard recognized HID reset / RMI input. Unknown frames
             * stop recovery instead of hiding a protocol mismatch. */
            if (le16(report)!=0 && !(le16(report)==64 &&
                (report[2]==0x0C || (report[2]==0x0B && report[3]>0 && report[3]<=60)))) {
                fault=RMI_FAULT_FRAME;return ESP_ERR_INVALID_RESPONSE;
            }
            ESP_LOGW(TAG,"PENDING_INPUT_DISCARDED id=%02X",report[2]);
        } else {
            int64_t now=esp_timer_get_time();if(!quiet) quiet=now;
            if(now-quiet>=20000) return ESP_OK;
        }
        pause_tick();
    }
    fault=RMI_FAULT_DEADLINE;return ESP_ERR_TIMEOUT;
}
esp_err_t synaptics_rmi_discover(synaptics_function_t *f01, synaptics_function_t *f34)
{
    memset(f01,0,sizeof(*f01));memset(f34,0,sizeof(*f34));
    uint8_t page=0;
    esp_err_t err=synaptics_rmi_write(0x00FF,&page,1);
    if (err!=ESP_OK) return err;
    err=synaptics_rmi_read(0x00FF,&page,1);
    if (err!=ESP_OK) return err;
    if (page!=0) return ESP_ERR_INVALID_RESPONSE;
    bool found01=false,found34=false;
    for (unsigned p=0;p<20;++p) {
        for (int offset=0xE9;offset>=0xC1;offset-=6) {
            uint8_t raw[6];
            err=synaptics_rmi_read((p<<8)|offset,raw,sizeof(raw));
            if (err!=ESP_OK) return err;
            if (!raw[5]) break;
            synaptics_function_t *f=NULL;
            if (raw[5]==1) { if(found01) return ESP_ERR_INVALID_RESPONSE; f=f01;found01=true; }
            if (raw[5]==0x34) { if(found34) return ESP_ERR_INVALID_RESPONSE; f=f34;found34=true; }
            if (f) {
                *f=(synaptics_function_t){(p<<8)|raw[0],(p<<8)|raw[1],
                    (p<<8)|raw[2],(p<<8)|raw[3],(raw[4]>>5)&3};
                ESP_LOGI(TAG,"F%02X query=%04X command=%04X control=%04X data=%04X version=%u",
                    raw[5],f->query,f->command,f->control,f->data,f->version);
            }
            if (found01 && found34) return ESP_OK;
        }
    }
    ESP_LOGE(TAG,"PDT missing F01=%u F34=%u",found01,found34);
    return ESP_ERR_NOT_FOUND;
}
#endif
