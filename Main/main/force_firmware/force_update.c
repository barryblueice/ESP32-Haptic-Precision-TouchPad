/* One-shot 0x49 updater, SAM_BUSES.zh-CN.md sections 4.2/4.3.
 * Source: SurfaceForce_10.0.156.body.bin[4:], 23076 bytes (NOT full capsule/body).
 * Body SHA256: 89540e2f2b0a2bcf346f733f7b5b07826949c650687ffc22f7817ec70766d6bd.
 * Payload SHA256/CRC are in force_image.h; port timing policy in force_download.h.
 * sdkconfig: CONFIG_SURFACE_FORCE_UPDATE_MODE=y; normal/haptic entries are exclusive.
 * Forced download on every boot regardless of current version or prior runs.
 * One update per boot, then stop; no UART command interface or automatic retry.
 * Completion checks are status + identity, not flash byte-for-byte verification.
 */
#include "sdkconfig.h"
#if CONFIG_SURFACE_FORCE_UPDATE_MODE
#include <inttypes.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_crc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "GPIO/GPIO_handle.h"
#include "I2C/I2C_handle.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/SUB_DEV/surface_haptic_hw.h"
#include "force_download.h"
#include "force_image.h"

#define TAG "FORCE_UPDATE"
#define IO_TIMEOUT_MS 100
static i2c_master_dev_handle_t force;
static uint32_t transaction;
static bool power_ready;

static uint64_t now_ms(void *ctx) { (void)ctx; return esp_timer_get_time()/1000; }
static void wait_ms(void *ctx, uint32_t ms) {
    (void)ctx;
    TickType_t ticks=pdMS_TO_TICKS(ms);
    vTaskDelay(ticks ? ticks : 1);
}
static void hex(const uint8_t *bytes, size_t n, char *out) {
    const char digits[]="0123456789ABCDEF";
    for(size_t i=0;i<n;++i) { out[i*2]=digits[bytes[i]>>4]; out[i*2+1]=digits[bytes[i]&15]; }
    out[n*2]=0;
}
static int write_force(void *ctx,const uint8_t *tx,size_t n) {
    (void)ctx;
    esp_err_t err=i2c_master_transmit(force,tx,n,IO_TIMEOUT_MS);
    ++transaction;
    /* Keep complete control/status traffic; log first/last and every 64th data packet. */
    bool data=n>=3 && tx[0]==0xF8;
    uint32_t offset=data ? ((uint32_t)tx[1]<<8)|tx[2] : 0;
    if (!data || err!=ESP_OK || offset%1024==0 || offset+n-3==FORCE_IMAGE_SIZE) {
        char raw[39]; hex(tx,n,raw);
        ESP_LOGI(TAG,"TX n=%"PRIu32" ms=%"PRIu64" addr7=0x49 raw=%s len=%u ret=%s offset=%"PRIu32,
                 transaction,now_ms(NULL),raw,(unsigned)n,esp_err_to_name(err),offset);
    }
    return err;
}
static int read_force(void *ctx,const uint8_t *tx,size_t n,uint8_t *rx,size_t count) {
    (void)ctx;
    if(n>19 || count>17) return ESP_ERR_INVALID_SIZE;
    memset(rx,0xA5,count);
    esp_err_t err=i2c_master_transmit_receive(force,tx,n,rx,count,IO_TIMEOUT_MS);
    char a[39],b[35]; hex(tx,n,a);hex(rx,count,b);
    ESP_LOGI(TAG,"WR n=%"PRIu32" ms=%"PRIu64" addr7=0x49 tx=%s rx_req=%u rx_actual=UNAVAILABLE raw=%s ret=%s",
             ++transaction,now_ms(NULL),a,(unsigned)count,b,esp_err_to_name(err));
    return err; /* A5 may be legitimate data; never infer received length from it. */
}
static esp_err_t query(uint8_t reg,uint8_t *rx,size_t len) {
    const uint8_t tx[]={0xD0,reg,0};
    return read_force(NULL,tx,sizeof(tx),rx,len);
}
static int identity(const char *phase,uint32_t *id) {
    uint8_t raw[7];
    int err=query(0x10,raw,sizeof(raw));
    if(err) return err;
    err=force_identity(raw,sizeof(raw),id);
    if(err) { ESP_LOGE(TAG,"ID phase=%s invalid checksum/zero data",phase);return err; }
    ESP_LOGI(TAG,"ID phase=%s value=0x%08"PRIX32" checksum=PASS",phase,*id);
    return 0;
}
static esp_err_t snapshots(const char *phase) {
    static const struct {uint8_t reg,len;} items[]={
        {0x24,15},{0x23,15},{0x5B,5},{0xBB,5},{0xBC,3},{0xB5,3},{0xB6,3}
    };
    ESP_LOGI(TAG,"PARAMETERS phase=%s (raw evidence; no parameter writes)",phase);
    for(unsigned i=0;i<sizeof(items)/sizeof(items[0]);++i) {
        uint8_t raw[15];
        esp_err_t err=query(items[i].reg,raw,items[i].len);
        if(err) return err;
    }
    return ESP_OK;
}
#define CHECK(call) do { int e_=(call); if(e_) { ESP_LOGE(TAG,"%s failed: %d",#call,e_);return e_; } } while(0)
static int setup(void) {
    CHECK(gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN,EN_OFF));
    CHECK(gpio_set_level(TP_RESET_GPIO,1));
    const gpio_config_t gpio={.pin_bit_mask=(1ULL<<GPIO_HAPTIC_BUCK_BOOST_EN)|(1ULL<<TP_RESET_GPIO),
        .mode=GPIO_MODE_OUTPUT,.intr_type=GPIO_INTR_DISABLE,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE};
    CHECK(gpio_config(&gpio));
    CHECK(gpio_set_level(TP_RESET_GPIO,0)); wait_ms(NULL,50);
    CHECK(gpio_set_level(TP_RESET_GPIO,1)); wait_ms(NULL,150);
    i2c_master_bus_config_t bus={.i2c_port=TP_I2C_PORT,.sda_io_num=TP_I2C_SDA,.scl_io_num=TP_I2C_SCL,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=false};
    CHECK(i2c_new_master_bus(&bus,&bus_handle));
    i2c_device_config_t device={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=0x49,.scl_speed_hz=I2C_FREQ_HZ};
    CHECK(i2c_master_bus_add_device(bus_handle,&device,&force));
    device.device_address=HAPTIC_MOTOR_ADDR;
    CHECK(i2c_master_bus_add_device(bus_handle,&device,&dev_haptic_motor_handle));
    bus.i2c_port=SUB_I2C_PORT;bus.sda_io_num=SUB_I2C_SDA;bus.scl_io_num=SUB_I2C_SCL;
    CHECK(i2c_new_master_bus(&bus,&sub_bus_handle));
    device.device_address=MP28167_ADDR;
    CHECK(i2c_master_bus_add_device(sub_bus_handle,&device,&sub_dev_mp28167_handle));
    esp_err_t isr=gpio_install_isr_service(0);
    if(isr!=ESP_ERR_INVALID_STATE) CHECK(isr);
    /* Reuse working local MP28167 configuration and CS40L25 initialization. */
    if(!surface_haptic_hw_initialize()) return ESP_FAIL;
    power_ready=true;
    CHECK(i2c_master_probe(bus_handle,0x49,IO_TIMEOUT_MS));
    return 0;
}
static int update(void) {
    if(esp_rom_crc32_le(0,force_image,FORCE_IMAGE_SIZE)!=FORCE_IMAGE_CRC32) {
        ESP_LOGE(TAG,"Embedded payload CRC mismatch; refusing update");return ESP_ERR_INVALID_CRC;
    }
    CHECK(setup());
    uint32_t before;
    CHECK(identity("before",&before));
    CHECK(snapshots("before"));
    /* Current version is logged, never used to skip a forced download.
     * Identity checksum and transport checks remain mandatory.
     */
    ESP_LOGW(TAG,"FORCED_UPDATE before=0x%08"PRIX32" target=0x%08X; current version does not skip download",before,FORCE_IMAGE_ID);
    ESP_LOGW(TAG,"DOWNLOAD_BEGIN bytes=%u packets=1443 sha256=%s",
             FORCE_IMAGE_SIZE,FORCE_IMAGE_SHA256);
    ESP_LOGI(TAG,"Port timing ms: prepare=150/100 poll=50 item_budget=2000 commit_budget=10000; SAM units unconfirmed");
    force_result_t result;
    const force_io_t io={NULL,write_force,read_force,wait_ms,now_ms};
    int rc=force_download_run(&io,force_image,FORCE_IMAGE_SIZE,&result);
    if(rc) {
        ESP_LOGE(TAG,"DOWNLOAD_FAILED stage=%s offset=%"PRIu32" status=0x%02X rc=%d; no automatic retry",
                 result.stage,result.offset,result.status,rc);return rc;
    }
    ESP_LOGI(TAG,"DOWNLOAD_STATUS_OK: commit ready/nonzero; finish status=0x%02X",result.status);
    uint32_t after;
    CHECK(identity("after",&after));
    if(after!=FORCE_IMAGE_ID) {
        ESP_LOGE(TAG,"TARGET_MISMATCH expected=0x%08X actual=0x%08"PRIX32,FORCE_IMAGE_ID,after);
        return ESP_ERR_INVALID_RESPONSE;
    }
    ESP_LOGI(TAG,"DOWNLOAD_VERIFIED: finish status and identity match; cold-power persistence not yet tested");
    esp_err_t err=snapshots("after");
    if(err!=ESP_OK) ESP_LOGW(TAG,"POST_SNAPSHOT_FAILED ret=%s; download status/identity already verified",esp_err_to_name(err));
    ESP_LOGI(TAG,"UPDATE_SUCCESS before=0x%08"PRIX32" after=0x%08"PRIX32" target=10.0.156",before,after);
    return 0;
}
static void task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG,"AUTO Force 0x49 firmware updater; normal/USB/radio tasks disabled");
    int err=update();
    if(!err) ESP_LOGI(TAG,"DONE: one-shot update/identity check complete; no further sampling or I2C operations");
    else ESP_LOGE(TAG,"HALTED rc=%d; no retry, no calibration, no downstream 0x4A access",err);
    /* Keep supply unchanged after a download failure for diagnosis/recovery. */
    if(power_ready) ESP_LOGI(TAG,"Local boost retained; task stopped");
    vTaskDelete(NULL);
}
void surface_force_update_start(void) {
    if(xTaskCreate(task,"force_update",8192,NULL,5,NULL)!=pdPASS)
        ESP_LOGE(TAG,"Task allocation failed; no hardware operations");
}
#endif
