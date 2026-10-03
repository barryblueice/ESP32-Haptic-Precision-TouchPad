/* SYNAPTICS_AND_CONTROL.zh-CN.md sections 2/3; SAM D72DC container index.
 * Traverse descriptors, never use hard-coded content offsets or repack bytes.
 * Mirror the documented bounded top/bootloader/page/private directory rules.
 * The private 18 -> 19 edge is specific to this pinned image, not a protocol.
 */
#include "sdkconfig.h"
#if CONFIG_SURFACE_SYNAPTICS_UPDATE_MODE
#include <stdbool.h>
#include <string.h>
#include "esp_log.h"
#include "synaptics_image.h"
#define TAG "SYNAPTICS_IMAGE"
extern const uint8_t image_start[] asm("_binary_synaptics_image_start");
extern const uint8_t image_end[] asm("_binary_synaptics_image_end");
static uint16_t le16(const uint8_t *p) { return p[0]|((uint16_t)p[1]<<8); }
static uint32_t le32(const uint8_t *p) { return le16(p)|((uint32_t)le16(p+2)<<16); }
static uint32_t fletcher(const uint8_t *data,size_t size)
{
    uint32_t a=0xFFFF,b=0xFFFF;
    for (size_t i=0;i<size;i+=2) {
        a+=data[i]|(i+1<size ? (uint32_t)data[i+1]<<8 : 0);
        a=(a&0xFFFF)+(a>>16);
        b+=a;b=(b&0xFFFF)+(b>>16);
    }
    return (b<<16)|a;
}
typedef struct {
    size_t size;
    uint32_t seen[128];
    unsigned count;
    bool info_seen,boot_seen;
    synaptics_image_t *out;
} parser_t;
static bool bounds(const parser_t *p,uint32_t offset,uint32_t size)
{
    return offset<=p->size && size<=p->size-offset;
}
static esp_err_t visit(parser_t *p,uint32_t offset,int expected,unsigned depth)
{
    if (depth>8 || p->count>=128 || !bounds(p,offset,32)) return ESP_ERR_INVALID_SIZE;
    for (unsigned i=0;i<p->count;++i) if (p->seen[i]==offset) return ESP_ERR_INVALID_RESPONSE;
    p->seen[p->count++]=offset;
    const uint8_t *d=image_start+offset;
    uint16_t id=le16(d+4);
    uint32_t length=le32(d+24),address=le32(d+28);
    if ((expected>=0 && id!=expected) || !bounds(p,address,length) ||
        (le32(d+16) && !bounds(p,le32(d+20),le32(d+16)))) return ESP_ERR_INVALID_RESPONSE;
    const uint8_t *data=image_start+address;
    if (fletcher(data,length)!=le32(d)) {
        ESP_LOGE(TAG,"Container checksum failed id=%02X descriptor=%08lX",id,(unsigned long)offset);
        return ESP_ERR_INVALID_CRC;
    }
    for (unsigned i=0;i<SYNAPTICS_PARTITIONS;++i) {
        synaptics_partition_t *part=&p->out->parts[i];
        if (id==part->container) {
            if (depth!=1 || part->data || !length) return ESP_ERR_INVALID_RESPONSE;
            part->data=data;part->size=length;
        }
    }
    if (id==0x0D) {
        if (depth!=1 || p->info_seen || length<34) return ESP_ERR_INVALID_RESPONSE;
        p->info_seen=true;p->out->firmware_id=le32(data+4);
        memcpy(p->out->product,data+24,10);p->out->product[10]=0;
    }
    size_t skip=0;int child=-1;bool directory=false;
    if (id==0 || id==0x0B) {directory=true;if(id==0x0B) child=0x0C;}
    if (id==3) {
        if (depth!=1 || p->boot_seen || length<4) return ESP_ERR_INVALID_RESPONSE;
        p->boot_seen=true;p->out->bl_major=data[0];p->out->bl_minor=data[1];
        directory=true;skip=4;
    }
    if (id==0x18) {
        if (length!=4) return ESP_ERR_INVALID_SIZE;
        directory=true;child=0x19;
    }
    if (directory) {
        if ((length-skip)%4) return ESP_ERR_INVALID_SIZE;
        for (size_t i=skip;i<length;i+=4) {
            esp_err_t err=visit(p,le32(data+i),child,depth+1);
            if (err!=ESP_OK) return err;
        }
    }
    return ESP_OK;
}
esp_err_t synaptics_image_open(synaptics_image_t *image)
{
    if (!image) return ESP_ERR_INVALID_ARG;
    memset(image,0,sizeof(*image));
    image->parts[0]=(synaptics_partition_t){3,0x0F,"FLASH_CONFIG",NULL,0};
    image->parts[1]=(synaptics_partition_t){7,0x12,"CORE_CODE",NULL,0};
    image->parts[2]=(synaptics_partition_t){8,0x13,"CORE_CONFIG",NULL,0};
    parser_t parser={.size=(size_t)(image_end-image_start),.out=image};
    if (parser.size!=136432 || image_start[6]!=0 || image_start[7]!=0x10 ||
        le32(image_start)!=SYNAPTICS_IMAGE_CHECKSUM ||
        fletcher(image_start+4,parser.size-4)!=SYNAPTICS_IMAGE_CHECKSUM) return ESP_ERR_INVALID_CRC;
    esp_err_t err=visit(&parser,le32(image_start+12),0,0);
    if (err!=ESP_OK) return err;
    image->containers=parser.count;
    if (!parser.info_seen || !parser.boot_seen || parser.count!=20 ||
        memcmp(image->product,SYNAPTICS_IMAGE_PRODUCT,10) || image->firmware_id!=SYNAPTICS_IMAGE_ID ||
        image->bl_major!=8 || image->bl_minor!=5) return ESP_ERR_INVALID_RESPONSE;
    const size_t expected[]={96,112544,2048};
    for (unsigned i=0;i<SYNAPTICS_PARTITIONS;++i) {
        if (!image->parts[i].data || image->parts[i].size!=expected[i]) return ESP_ERR_INVALID_SIZE;
        ESP_LOGI(TAG,"selector=%02X container=%02X name=%s bytes=%u checksum=PASS",
            image->parts[i].partition,image->parts[i].container,image->parts[i].name,(unsigned)image->parts[i].size);
    }
    ESP_LOGI(TAG,"IMAGE_OK containers=%u product=%s firmware_id=%08lX whole_checksum=PASS",
        parser.count,image->product,(unsigned long)image->firmware_id);
    return ESP_OK;
}
#endif
