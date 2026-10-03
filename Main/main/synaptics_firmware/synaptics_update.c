/* Synaptics TM3651-001 / Surface 4.12.139 updater, rewrite 2.
 * Sources supplied by the user, relative to the mcu-drivers repository:
 * tools/surface_firmware/SYNAPTICS_UPDATE_FLOW.zh-CN.md
 * surface/CS40L25_WAVEFORM_AND_SAM_CONTROL.zh-CN.md (section 4)
 * tools/surface_firmware/SYNAPTICS_AND_CONTROL.zh-CN.md (sections 2/3)
 * tools/surface_firmware/SYNAPTICS_CURRENT_VERSION.zh-CN.md
 *
 * Confirmed SAM operations: dynamic F01/F34, DEBCA enter, DE9EC erase,
 * DEBB4/DEAB6 select/reset offset, DEB70/DEB96 transfer parameters, DEAD2
 * batches, DE6AC F01 reset, DE8FE three-byte current ID. Selectors 03/07/08
 * map to containers 0F/12/13. Never write a bootloader/private container.
 *
 * This is an ESP32 state machine, NOT a reconstruction of SAM's event runtime.
 * IRQ is a response-availability hint; finite timer polling confirms commands.
 * Docs leave timer units, erase physical coverage and power-loss recovery open.
 * Timing below, input resync, partition-table preflight and startup-09 recovery
 * are explicitly local policies. They are not claimed to be SAM procedures.
 * The user's trace demonstrated successful partition reads from startup C9;
 * only that narrow idle-BL8 condition permits the first read, never an error
 * after a newly issued command. No repeat erase/write/reset on failure.
 */
#include "sdkconfig.h"
#if CONFIG_SURFACE_SYNAPTICS_UPDATE_MODE
#include <inttypes.h>
#include <stdbool.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "GPIO/GPIO_handle.h"
#include "I2C/I2C_handle.h"
#include "I2C/TP/i2c_hid.h"
#include "I2C/SUB_DEV/sub_dev.h"
#include "I2C/SUB_DEV/surface_haptic_hw.h"
#include "synaptics_image.h"
#include "synaptics_rmi.h"
#define TAG "SYNAPTICS_UPDATE"
#define F34_BL 0x80U
#define F34_ERRORS 0x1FU
#define F34_CRC_ERROR 9U
#define SYNAPTICS_SCL_WAIT_US 50000U
/* Local policy in milliseconds; NOT translated SAM timer constants. */
#define ERASE_SETTLE_MS 500U
#define ERASE_TIMEOUT_MS 30000U
#define COMMAND_TIMEOUT_MS 5000U
#define REBOOT_SETTLE_MS 1000U
#define REBOOT_TIMEOUT_MS 10000U
#define POLL_MS 50U
#define CHECK(call) do { esp_err_t e_=(call); if(e_!=ESP_OK) { \
    ESP_LOGE(TAG,"%s: %s",#call,esp_err_to_name(e_));return e_; } } while(0)
typedef enum {
    PREPARE, ENTER_BL, DISCOVER_BL, WAIT_BL,
    LAYOUT_BEGIN, LAYOUT_READ, LAYOUT_WAIT, LAYOUT_FETCH,
    ERASE_IMG, ERASE_WAIT, SELECT_PARTITION, WRITE_FW, WRITE_WAIT,
    EXIT_FWUPDATE, WAIT_RESET, UI_MODE, DONE
} update_state_t;
static const char *const state_names[]={
    "PREPARE","ENTER_BL","DISCOVER_BL","WAIT_BL",
    "LAYOUT_BEGIN","LAYOUT_READ","LAYOUT_WAIT","LAYOUT_FETCH",
    "ERASE_IMG","ERASE_WAIT","SELECT_PARTITION","WRITE_FW","WRITE_WAIT",
    "EXIT_FWUPDATE","WAIT_RESET","UI_MODE","DONE"
};
static synaptics_function_t f01,f34;
static synaptics_image_t firmware;
static struct {
    update_state_t state;
    int64_t deadline,next_poll,next_log;
    uint32_t before;
    uint8_t boot_id[2];
    uint16_t block_size,batch_blocks,layout_block_size;
    unsigned partition,completed_partitions,retries;
    size_t offset,pending,last_progress,table_offset;
    uint8_t table[96];
    bool recovery,in_bl,drain,layout_approved,erase_attempted,erase_confirmed;
} u;
static uint16_t le16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
static int64_t now(void) { return esp_timer_get_time(); }
static void wait_ms(uint32_t ms) { TickType_t ticks=pdMS_TO_TICKS(ms);vTaskDelay(ticks?ticks:1); }
static esp_err_t write_byte(uint16_t reg,uint8_t value) { return synaptics_rmi_write(reg,&value,1); }
static esp_err_t write_word(uint16_t reg,uint16_t value)
{
    const uint8_t raw[]={value,value>>8};return synaptics_rmi_write(reg,raw,2);
}
static void transition(update_state_t state)
{
    if (state==WRITE_FW || state==WRITE_WAIT) ESP_LOGD(TAG,"STATE %s",state_names[state]);
    else ESP_LOGI(TAG,"STATE %s",state_names[state]);
    u.state=state;
}
static void begin_wait(update_state_t state,uint32_t budget,uint32_t settle)
{
    int64_t start=now();u.deadline=start+(int64_t)budget*1000;
    u.next_poll=start+(int64_t)settle*1000;u.next_log=start+1000000;
    u.drain=false;transition(state);
}
static esp_err_t setup(void)
{
    CHECK(gpio_set_level(GPIO_HAPTIC_BUCK_BOOST_EN,EN_OFF));
    CHECK(gpio_set_level(TP_RESET_GPIO,1));
    gpio_config_t config={.pin_bit_mask=(1ULL<<GPIO_HAPTIC_BUCK_BOOST_EN)|(1ULL<<TP_RESET_GPIO),
        .mode=GPIO_MODE_OUTPUT,.intr_type=GPIO_INTR_DISABLE,
        .pull_up_en=GPIO_PULLUP_DISABLE,.pull_down_en=GPIO_PULLDOWN_DISABLE};
    CHECK(gpio_config(&config));
    config.pin_bit_mask=1ULL<<TP_INT_GPIO;config.mode=GPIO_MODE_INPUT;
    CHECK(gpio_config(&config));
    CHECK(gpio_set_level(TP_RESET_GPIO,0));wait_ms(50);
    CHECK(gpio_set_level(TP_RESET_GPIO,1));wait_ms(150);
    i2c_master_bus_config_t bus={.i2c_port=TP_I2C_PORT,.sda_io_num=TP_I2C_SDA,.scl_io_num=TP_I2C_SCL,
        .clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,.flags.enable_internal_pullup=false};
    CHECK(i2c_new_master_bus(&bus,&bus_handle));
    i2c_device_config_t device={.dev_addr_length=I2C_ADDR_BIT_LEN_7,.device_address=TP_I2C_ADDR,
        .scl_speed_hz=I2C_FREQ_HZ,.scl_wait_us=SYNAPTICS_SCL_WAIT_US};
    CHECK(i2c_master_bus_add_device(bus_handle,&device,&dev_handle));
    synaptics_rmi_bind(dev_handle);
    device.device_address=HAPTIC_MOTOR_ADDR;
    device.scl_wait_us=0; /* Retain existing CS40L25/MP28167 defaults. */
    CHECK(i2c_master_bus_add_device(bus_handle,&device,&dev_haptic_motor_handle));
    bus.i2c_port=SUB_I2C_PORT;bus.sda_io_num=SUB_I2C_SDA;bus.scl_io_num=SUB_I2C_SCL;
    CHECK(i2c_new_master_bus(&bus,&sub_bus_handle));
    device.device_address=MP28167_ADDR;
    CHECK(i2c_master_bus_add_device(sub_bus_handle,&device,&sub_dev_mp28167_handle));
    esp_err_t isr=gpio_install_isr_service(0);
    if (isr!=ESP_ERR_INVALID_STATE) CHECK(isr);
    /* Reuse the local board's working MP28167/CS40L25 power setup, no playback. */
    if (!surface_haptic_hw_initialize()) return ESP_FAIL;
    CHECK(i2c_master_probe(bus_handle,TP_I2C_ADDR,100));
    const uint8_t power_on[]={0x22,0,0,8};
    CHECK(i2c_master_transmit(dev_handle,power_on,sizeof(power_on),100));
    wait_ms(50);
    CHECK(synaptics_rmi_mode(true));wait_ms(50);
    return synaptics_rmi_discover(&f01,&f34);
}

static esp_err_t read_state(uint8_t *status,uint8_t *command)
{
    CHECK(synaptics_rmi_read(f34.data,status,1));
    CHECK(synaptics_rmi_read(f34.data+4,command,1));
    ESP_LOGI(TAG,"F34_STATE status=%02X command=%02X bootloader=%u code=%u",
        *status,*command,(unsigned)((*status&F34_BL)!=0),*status&F34_ERRORS);
    return ESP_OK;
}
static esp_err_t read_identity(bool after)
{
    uint8_t product[10],raw[3],status;
    CHECK(synaptics_rmi_read(f01.query+0x0B,product,sizeof(product)));
    CHECK(synaptics_rmi_read(f01.query+0x12,raw,sizeof(raw)));
    CHECK(synaptics_rmi_read(f01.data,&status,1));
    uint32_t id=raw[0]|((uint32_t)raw[1]<<8)|((uint32_t)raw[2]<<16);
    ESP_LOGI(TAG,"IDENTITY phase=%s product=%.10s raw=%02X%02X%02X firmware_id=%08"PRIX32" F01=%02X",
        after?"after":"before",(const char *)product,raw[0],raw[1],raw[2],id,status);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG,product,sizeof(product),ESP_LOG_INFO);
    if (memcmp(product,firmware.product,10)) {
        ESP_LOGE(TAG,"PRODUCT_MISMATCH expected=%s",firmware.product);return ESP_ERR_INVALID_RESPONSE;
    }
    uint8_t code=status&15;
    bool startup_crc=!after && u.recovery && (code==4 || code==5);
    if ((code>1 && !startup_crc) || (after && (status&0x40))) return ESP_ERR_INVALID_STATE;
    if (after) {
        if (id!=firmware.firmware_id) {
            ESP_LOGE(TAG,"TARGET_MISMATCH expected=%08"PRIX32" actual=%08"PRIX32,firmware.firmware_id,id);
            return ESP_ERR_INVALID_RESPONSE;
        }
    } else {
        u.before=id;
        if (!id || id==0xFFFFFF) ESP_LOGW(TAG,"CURRENT_ID_UNKNOWN; not a successful version check");
        ESP_LOGW(TAG,"FORCE_UPDATE: current ID never skips the update");
    }
    return ESP_OK;
}
static esp_err_t read_geometry(void)
{
    uint8_t q0,block[3],batch[2];
    CHECK(synaptics_rmi_read(f34.query,&q0,1));
    if (f34.version!=2 || (q0&7)) {
        ESP_LOGE(TAG,"UNSUPPORTED_QUERY_LAYOUT F34_version=%u query0=%02X",f34.version,q0);
        return ESP_ERR_NOT_SUPPORTED;
    }
    CHECK(synaptics_rmi_read(f34.query+1,u.boot_id,2));
    CHECK(synaptics_rmi_read(f34.query+3,block,3));
    CHECK(synaptics_rmi_read(f34.query+6,batch,2));
    uint16_t size=le16(block+1);
    /* SAM DEB96/DEBAC reads 2 bytes but uses only LDRB of byte 0. No invented
     * name for byte 1; never turn observed 01 03 into a 769-block transfer. */
    uint16_t count=batch[0];
    ESP_LOGI(TAG,"GEOMETRY BL=%u.%u query0=%02X block_raw=%02X%02X%02X batch_raw=%02X%02X block=%u blocks_per_batch=%u",
        u.boot_id[1],u.boot_id[0],q0,block[0],block[1],block[2],batch[0],batch[1],size,count);
    if (u.boot_id[1]!=firmware.bl_major || !size || size>17 || !count) return ESP_ERR_NOT_SUPPORTED;
    if (u.layout_approved && size!=u.layout_block_size) return ESP_ERR_INVALID_RESPONSE;
    for (unsigned i=0;i<SYNAPTICS_PARTITIONS;++i)
        if (firmware.parts[i].size%size || firmware.parts[i].size/size>UINT16_MAX)
            return ESP_ERR_INVALID_SIZE; /* Never pad the image to hide bad geometry. */
    u.block_size=size;u.batch_blocks=count;return ESP_OK;
}
static esp_err_t control(uint8_t partition,uint8_t command)
{
    const uint8_t raw[]={partition,0,0,0,0,command,u.boot_id[0],u.boot_id[1]};
    ESP_LOGI(TAG,"CONTROL selector=%02X command=%02X reg=%04X",partition,command,f34.data+1);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG,raw,sizeof(raw),ESP_LOG_INFO);
    return synaptics_rmi_write(f34.data+1,raw,sizeof(raw));
}
static bool transient_io(esp_err_t err) { return err==ESP_ERR_TIMEOUT || err==ESP_ERR_INVALID_RESPONSE; }
/* One timer/availability step, never resends a flash command. Only ERASE_WAIT
 * permits a failed read-request TX to be retried. Accepted-request RX errors,
 * bad frames and completed flash errors always stop. Caller controls deadline. */
static esp_err_t poll_completion(bool erase,bool *complete)
{
    *complete=false;
    if (now()>=u.deadline) return ESP_ERR_TIMEOUT;
    if (now()<u.next_poll) return ESP_OK;
    u.next_poll=now()+(int64_t)POLL_MS*1000;
    if (u.drain) {
        int64_t drain_end=now()+250000;if(drain_end>u.deadline) drain_end=u.deadline;
        esp_err_t err=synaptics_rmi_drain_until(drain_end);
        if (err!=ESP_OK) {
            if (!transient_io(err) || synaptics_rmi_fault()==RMI_FAULT_FRAME) return err;
            ++u.retries;return ESP_OK;
        }
        u.drain=false;
    }
    uint8_t command,status;
    esp_err_t err=synaptics_rmi_read_until(f34.data+4,&command,1,u.deadline);
    if (err==ESP_OK && command==0) {
        err=synaptics_rmi_read_until(f34.data,&status,1,u.deadline);
        if (err==ESP_OK) {
            if ((status&F34_ERRORS) || !(status&F34_BL)) {
                ESP_LOGE(TAG,"COMMAND_FAILED state=%s status=%02X command=00",state_names[u.state],status);
                return ESP_ERR_INVALID_RESPONSE;
            }
            *complete=true;return ESP_OK;
        }
    }
    if (err!=ESP_OK) {
        if (!erase || !transient_io(err) || synaptics_rmi_fault()!=RMI_FAULT_TX) return err;
        ++u.retries;u.drain=true;
        ESP_LOGW(TAG,"ERASE_QUERY_UNAVAILABLE ret=%s retries=%u; erase command not resent",esp_err_to_name(err),u.retries);
        return ESP_OK;
    }
    if (now()>=u.next_log) {
        ESP_LOGI(TAG,"WAIT state=%s command=%02X remaining_ms=%"PRId64,state_names[u.state],command,(u.deadline-now())/1000);
        u.next_log=now()+1000000;
    }
    return ESP_OK;
}
static esp_err_t check_layout(void)
{
    const synaptics_partition_t *flash=&firmware.parts[0];
    size_t entries=0;
    while (2+(entries+1)*8<=flash->size && (flash->data[2+entries*8]&31)) ++entries;
    if (!entries || memcmp(u.table,flash->data,2+entries*8)) {
        ESP_LOGE(TAG,"LAYOUT_MISMATCH: no erase, no layout migration");return ESP_ERR_NOT_SUPPORTED;
    }
    for (unsigned i=0;i<SYNAPTICS_PARTITIONS;++i) {
        bool found=false;
        for (size_t n=0;n<entries;++n) {
            const uint8_t *entry=u.table+2+n*8;
            if ((entry[0]&31)==firmware.parts[i].partition) {
                if (found || (size_t)le16(entry+2)*u.block_size!=firmware.parts[i].size) return ESP_ERR_INVALID_SIZE;
                found=true;
            }
        }
        if (!found) return ESP_ERR_NOT_FOUND;
    }
    u.layout_approved=true;u.layout_block_size=u.block_size;
    ESP_LOGI(TAG,"LAYOUT_MATCH entries=%u; first new READ completed with successful status",(unsigned)entries);
    return ESP_OK;
}
static esp_err_t select_partition(uint8_t selector)
{
    CHECK(write_byte(f34.data+1,selector));return write_word(f34.data+2,0);
}
static esp_err_t step(void)
{
    bool complete=false;
    switch (u.state) {
    case PREPARE: {
        CHECK(read_geometry());
        uint8_t status,command;CHECK(read_state(&status,&command));
        u.in_bl=(status&F34_BL)!=0;
        u.recovery=u.in_bl && !command && (status&F34_ERRORS)==F34_CRC_ERROR;
        if (command || ((status&F34_ERRORS) && !u.recovery)) return ESP_ERR_INVALID_STATE;
        CHECK(read_identity(false));
        if (u.recovery) ESP_LOGW(TAG,"STARTUP_CRC_RECOVERY: only product-matched idle BL8; first READ must succeed");
        transition(u.in_bl ? LAYOUT_BEGIN : ENTER_BL);return ESP_OK;
    }
    case ENTER_BL: {
        uint8_t interrupts;CHECK(synaptics_rmi_read(f01.control+1,&interrupts,1));
        if (!(interrupts&1)) CHECK(write_byte(f01.control+1,interrupts|1));
        CHECK(control(1,1));begin_wait(DISCOVER_BL,COMMAND_TIMEOUT_MS,300);return ESP_OK;
    }
    case DISCOVER_BL:
        if (now()>=u.deadline) return ESP_ERR_TIMEOUT;
        if (now()<u.next_poll) return ESP_OK;
        CHECK(synaptics_rmi_mode(true));CHECK(synaptics_rmi_discover(&f01,&f34));
        u.next_poll=now();transition(WAIT_BL);return ESP_OK;
    case WAIT_BL:
        CHECK(poll_completion(false,&complete));
        if (complete) {u.in_bl=true;transition(LAYOUT_BEGIN);}return ESP_OK;
    case LAYOUT_BEGIN: {
        CHECK(read_geometry());
        uint8_t status,command;CHECK(read_state(&status,&command));
        if (!(status&F34_BL) || command ||
            ((status&F34_ERRORS) && !(u.recovery && (status&F34_ERRORS)==F34_CRC_ERROR))) return ESP_ERR_INVALID_STATE;
        if (firmware.parts[0].size!=sizeof(u.table)) return ESP_ERR_INVALID_SIZE;
        CHECK(select_partition(3));u.table_offset=0;transition(LAYOUT_READ);return ESP_OK;
    }
    case LAYOUT_READ: {
        size_t blocks=(sizeof(u.table)-u.table_offset)/u.block_size;
        if (blocks>u.batch_blocks) blocks=u.batch_blocks;
        u.pending=blocks*u.block_size;
        CHECK(write_word(f34.data+3,blocks));CHECK(write_byte(f34.data+4,2));
        begin_wait(LAYOUT_WAIT,COMMAND_TIMEOUT_MS,10);return ESP_OK;
    }
    case LAYOUT_WAIT:
        CHECK(poll_completion(false,&complete));if(complete) transition(LAYOUT_FETCH);return ESP_OK;
    case LAYOUT_FETCH:
        CHECK(synaptics_rmi_read(f34.data+5,u.table+u.table_offset,u.pending));u.table_offset+=u.pending;
        if (u.table_offset<sizeof(u.table)) {transition(LAYOUT_READ);return ESP_OK;}
        ESP_LOGI(TAG,"CURRENT_FLASH_CONFIG bytes=%u raw:",(unsigned)sizeof(u.table));
        ESP_LOG_BUFFER_HEX_LEVEL(TAG,u.table,sizeof(u.table),ESP_LOG_INFO);
        CHECK(check_layout());transition(ERASE_IMG);return ESP_OK;
    case ERASE_IMG:
        /* Set BEFORE transmit: an I2C error does not prove no erase occurred. */
        u.erase_attempted=true;CHECK(control(3,5));
        ESP_LOGW(TAG,"ERASE_SENT completion=UNCONFIRMED; settle=%u budget=%u ms",ERASE_SETTLE_MS,ERASE_TIMEOUT_MS);
        begin_wait(ERASE_WAIT,ERASE_TIMEOUT_MS,ERASE_SETTLE_MS);return ESP_OK;
    case ERASE_WAIT:
        CHECK(poll_completion(true,&complete));
        if (complete) {
            u.erase_confirmed=true;ESP_LOGI(TAG,"ERASE_COMPLETE status=SUCCESS retries=%u",u.retries);
            u.partition=0;transition(SELECT_PARTITION);
        }
        return ESP_OK;
    case SELECT_PARTITION: {
        if (u.partition>=SYNAPTICS_PARTITIONS) {transition(EXIT_FWUPDATE);return ESP_OK;}
        const synaptics_partition_t *part=&firmware.parts[u.partition];
        CHECK(select_partition(part->partition));CHECK(read_geometry());
        u.offset=u.pending=u.last_progress=0;
        ESP_LOGI(TAG,"PARTITION_BEGIN selector=%02X container=%02X name=%s bytes=%u",
            part->partition,part->container,part->name,(unsigned)part->size);
        transition(WRITE_FW);return ESP_OK;
    }
    case WRITE_FW: {
        const synaptics_partition_t *part=&firmware.parts[u.partition];
        size_t blocks=(part->size-u.offset)/u.block_size;if(blocks>u.batch_blocks) blocks=u.batch_blocks;
        if (!blocks) return ESP_ERR_INVALID_SIZE;
        u.pending=blocks*u.block_size;
        CHECK(write_word(f34.data+3,blocks));CHECK(write_byte(f34.data+4,3));
        for (size_t i=0;i<blocks;++i) {
            CHECK(synaptics_rmi_write(f34.data+5,part->data+u.offset+i*u.block_size,u.block_size));
            if ((i&31)==31) vTaskDelay(1);
        }
        begin_wait(WRITE_WAIT,COMMAND_TIMEOUT_MS,10);return ESP_OK;
    }
    case WRITE_WAIT:
        CHECK(poll_completion(false,&complete));
        if (complete) {
            const synaptics_partition_t *part=&firmware.parts[u.partition];
            u.offset+=u.pending;u.pending=0;
            if (u.offset-u.last_progress>=4096 || u.offset==part->size) {
                ESP_LOGI(TAG,"WRITE_CONFIRMED selector=%02X bytes=%u/%u",part->partition,(unsigned)u.offset,(unsigned)part->size);
                u.last_progress=u.offset;
            }
            if (u.offset==part->size) {
                ++u.completed_partitions;++u.partition;transition(SELECT_PARTITION);
            } else transition(WRITE_FW);
        }
        return ESP_OK;
    case EXIT_FWUPDATE:
        CHECK(write_byte(f01.command,1)); /* SAM DE6AC; no shared GPIO reset. */
        begin_wait(WAIT_RESET,REBOOT_TIMEOUT_MS,REBOOT_SETTLE_MS);return ESP_OK;
    case WAIT_RESET:
        if (now()>=u.deadline) return ESP_ERR_TIMEOUT;
        if (now()<u.next_poll) return ESP_OK;
        CHECK(synaptics_rmi_mode(true));CHECK(synaptics_rmi_discover(&f01,&f34));
        u.next_poll=now();transition(UI_MODE);return ESP_OK;
    case UI_MODE: {
        if (now()>=u.deadline) return ESP_ERR_TIMEOUT;
        if (now()<u.next_poll) return ESP_OK;
        u.next_poll=now()+(int64_t)POLL_MS*1000;
        uint8_t f01_status;CHECK(synaptics_rmi_read(f01.data,&f01_status,1));
        if ((f01_status&0x40) || (f01_status&15)==6) return ESP_OK;
        if ((f01_status&15)>1) return ESP_ERR_INVALID_STATE;
        uint8_t status,command;CHECK(read_state(&status,&command));
        if (command || (status&F34_BL)) return ESP_OK;
        if (status&F34_ERRORS) return ESP_ERR_INVALID_RESPONSE;
        CHECK(read_identity(true));CHECK(synaptics_rmi_mode(false));transition(DONE);return ESP_OK;
    }
    case DONE:return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}
static void task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG,"SAM-derived three-partition state machine, forced update each boot");
    ESP_LOGI(TAG,"Local timing policy: erase=%u/%u ms, commands=%u ms, reboot=%u/%u ms; not hardware-validated",
        ERASE_SETTLE_MS,ERASE_TIMEOUT_MS,COMMAND_TIMEOUT_MS,REBOOT_SETTLE_MS,REBOOT_TIMEOUT_MS);
    memset(&u,0,sizeof(u));
    esp_err_t err=synaptics_image_open(&firmware);
    if (err==ESP_OK) err=setup();
    if (err==ESP_OK) {
        transition(PREPARE);
        while (u.state!=DONE) {
            err=step();if(err!=ESP_OK) break;
            vTaskDelay(1);
        }
    }
    if (err==ESP_OK && u.state==DONE) {
        ESP_LOGI(TAG,"UPDATE_SUCCESS before=%08"PRIX32" after=%08"PRIX32" partitions=%u recovery=%u",
            u.before,firmware.firmware_id,u.completed_partitions,(unsigned)u.recovery);
        ESP_LOGI(TAG,"Verified: command completions, UI status and post-reset ID; no full flash readback/cold-power proof");
        ESP_LOGI(TAG,"DONE: no sampling; select normal entry for normal firmware");
    } else {
        ESP_LOGE(TAG,"HALTED state=%s ret=%s erase_attempted=%u erase_confirmed=%u completed_partitions=%u partition_index=%u confirmed_bytes=%u pending_bytes=%u",
            state_names[u.state],esp_err_to_name(err),(unsigned)u.erase_attempted,(unsigned)u.erase_confirmed,
            u.completed_partitions,u.partition,(unsigned)u.offset,(unsigned)u.pending);
        ESP_LOGE(TAG,"No automatic erase/write/reset retry; power retained. A new boot starts a NEW forced update.");
    }
    vTaskDelete(NULL);
}
void surface_synaptics_update_start(void)
{
    if (xTaskCreate(task,"syn_update",10240,NULL,5,NULL)!=pdPASS)
        ESP_LOGE(TAG,"Task creation failed; no hardware operations");
}
#endif
