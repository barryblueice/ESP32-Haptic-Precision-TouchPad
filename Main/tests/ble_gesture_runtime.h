static unsigned sender_budget, send_count;
static struct { uint8_t id, length, data[8]; } sent[512];
uint16_t hid_dev_report_handle(uint8_t id) { return 100+id; }
static esp_err_t record_send(uint16_t conn,uint32_t epoch,uint8_t id,const void *data,uint8_t length)
{
    if(send_count<512) {
        sent[send_count].id=id;sent[send_count].length=length;
        memcpy(sent[send_count].data,data,length);
    }
    ++send_count;
    ble_input_complete(conn,epoch,hid_dev_report_handle(id),BLE_TX_OK);
    return ESP_OK;
}
esp_err_t ble_hid_send_mouse(uint16_t conn,uint32_t epoch,const input_report_t *r)
{ return record_send(conn,epoch,1,&r->data.mouse,5); }
esp_err_t ble_hid_send_aux(uint16_t conn,uint32_t epoch,const aux_output_report_t *r)
{ return record_send(conn,epoch,r->id==2?1:r->id,r->data,r->length); }
