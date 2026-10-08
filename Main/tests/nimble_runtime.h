/* Host boundaries for the production adapter. Numeric constants come from
 * the selected SDK; firmware compilation checks the full SDK ABI. */
typedef int esp_err_t;
typedef int portMUX_TYPE;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define portMUX_INITIALIZER_UNLOCKED 0
#define taskENTER_CRITICAL(p) ((void)(p))
#define taskEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define GPIO_LED_3 3
#define LED_CMD_BLINK 1
#define LED_CMD_STOP 0
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define led_send_command(...) ((void)0)
typedef struct { uint8_t type; } ble_uuid_t;
typedef struct { ble_uuid_t u; uint16_t value; } ble_uuid16_t;
#define BLE_UUID16_DECLARE(v) ((ble_uuid_t *)&(ble_uuid16_t){{16}, (v)})
#define BLE_UUID16_INIT(v) {{16}, (v)}
typedef struct { uint8_t type, val[6]; } ble_addr_t;
struct os_mbuf { uint16_t len; uint8_t data[512]; };
#define OS_MBUF_PKTLEN(m) ((m)->len)
struct ble_gatt_access_ctxt { uint8_t op; struct os_mbuf *om; uint16_t offset; };
typedef int ble_gatt_access_fn(uint16_t, uint16_t, struct ble_gatt_access_ctxt *, void *);
struct ble_gatt_dsc_def { const ble_uuid_t *uuid; uint8_t att_flags; ble_gatt_access_fn *access_cb; void *arg; };
struct ble_gatt_chr_def {
    const ble_uuid_t *uuid; ble_gatt_access_fn *access_cb; void *arg;
    struct ble_gatt_dsc_def *descriptors; uint32_t flags; uint16_t *val_handle;
};
struct ble_gatt_svc_def {
    uint8_t type; const ble_uuid_t *uuid; const struct ble_gatt_svc_def **includes;
    const struct ble_gatt_chr_def *characteristics;
};
struct ble_gap_conn_desc { uint16_t conn_handle; ble_addr_t peer_id_addr; struct { bool encrypted; } sec_state; };
struct ble_gap_event {
    uint8_t type;
    union {
        struct { int status; uint16_t conn_handle; } connect, enc_change;
        struct { struct ble_gap_conn_desc conn; } disconnect;
        struct { uint16_t conn_handle, attr_handle; bool cur_notify; uint8_t reason; } subscribe;
        struct { uint16_t conn_handle; } repeat_pairing;
        struct { int status; uint16_t conn_handle, attr_handle; bool indication; } notify_tx;
    };
};
struct ble_gap_adv_params { uint8_t conn_mode, disc_mode; uint16_t itvl_min, itvl_max; };
struct ble_hs_adv_fields {
    uint8_t flags; const ble_uuid16_t *uuids16; uint8_t num_uuids16, uuids16_is_complete;
    uint16_t appearance; bool appearance_is_present;
    int8_t tx_pwr_lvl; bool tx_pwr_lvl_is_present;
    const uint8_t *name; uint8_t name_len, name_is_complete;
};
static struct {
    void (*reset_cb)(int); void (*sync_cb)(void); int (*store_status_cb)(void);
    uint8_t sm_io_cap, sm_bonding, sm_mitm, sm_sc, sm_our_key_dist, sm_their_key_dist;
} ble_hs_cfg;
struct ble_npl_event { void (*fn)(struct ble_npl_event *); bool queued; };
struct ble_npl_callout { struct ble_npl_event event; bool active; unsigned ticks; };
static struct ble_npl_event *queued_events[8];
static unsigned queued_count;
static void *nimble_port_get_dflt_eventq(void) { return (void *)1; }
static void ble_npl_event_init(struct ble_npl_event *e, void (*fn)(struct ble_npl_event *), void *arg)
{ (void)arg; e->fn=fn; e->queued=false; }
static void ble_npl_eventq_put(void *q,struct ble_npl_event *e)
{ (void)q; if (!e->queued && queued_count<8) { e->queued=true; queued_events[queued_count++]=e; } }
static void drain_events(void)
{
    while (queued_count) {
        struct ble_npl_event *e=queued_events[0]; --queued_count;
        memmove(queued_events,queued_events+1,queued_count*sizeof(*queued_events));
        e->queued=false; e->fn(e);
    }
}
static void ble_npl_callout_init(struct ble_npl_callout *c, void *q,void (*fn)(struct ble_npl_event *),void *arg)
{ (void)q; ble_npl_event_init(&c->event,fn,arg); c->active=false; }
static void ble_npl_callout_stop(struct ble_npl_callout *c) { c->active=false; }
static int ble_npl_callout_reset(struct ble_npl_callout *c,unsigned ticks) { c->active=true;c->ticks=ticks;return 0; }
static unsigned ble_npl_time_ms_to_ticks32(unsigned ms) { return ms; }

static int init_rc, count_rc, add_rc, adv_rc, fields_rc, security_rc, store_rc, notify_rc;
static unsigned count_calls, add_calls, host_starts, deinit_calls, store_inits, adv_calls, security_calls, delete_calls;
static unsigned notify_calls, mbuf_frees, completed_calls, event_calls, save_calls;
static uint32_t mock_epoch, mock_generation;
static bool mock_connected, mock_ready, mock_encrypted, fail_alloc, fail_append, fail_save, adv_active;
static uint16_t mock_conn, completed_conn, completed_handle, sent_handle;
static uint32_t completed_epoch;
static int completed_result;
static uint8_t mock_strength=63, sent_data[8], mock_aux_ready;
static uint16_t sent_length;
static ble_addr_t deleted_peer;
static struct ble_hs_adv_fields advertised, scan_response;
static struct ble_gap_adv_params advertised_params;
static uint8_t advertised_address;
static struct os_mbuf allocated_mbuf;
static int gap_event(struct ble_gap_event *event, void *arg);
static void input_request_mode(uint8_t mode) { (void)mode; }
bool input_report_current(const input_report_t *r) { return r->generation==mock_generation; }
bool aux_output_report_current(const aux_output_report_t *r) { return r->release || r->generation==mock_generation; }
uint32_t ble_input_connection(bool up,uint16_t conn)
{ mock_connected=up;mock_conn=conn;mock_ready=false;mock_aux_ready=0;return ++mock_epoch; }
void ble_input_subscription(uint16_t conn,bool enabled) { if(mock_connected && conn==mock_conn)mock_ready=enabled; }
void ble_input_aux_subscription(uint16_t conn,uint8_t mask) { if(mock_connected && conn==mock_conn)mock_aux_ready=mask; }
void ble_input_complete(uint16_t conn,uint32_t epoch,uint16_t handle,ble_tx_result_t result)
{ ++completed_calls;completed_conn=conn;completed_epoch=epoch;completed_handle=handle;completed_result=result; }
static uint8_t ptp_haptic_click_intensity_get(void) { return mock_strength; }
static esp_err_t ptp_haptic_click_intensity_set_report(const uint8_t *d,unsigned n,bool save)
{ if(fail_save||n!=1||!save)return ESP_FAIL;++save_calls;mock_strength=*d;return ESP_OK; }
static int get_battery_percentage(void) { return 82; }
static void vTaskDelay(unsigned ticks) { (void)ticks; }
static int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned pri,void *out,unsigned core)
{ (void)fn;(void)name;(void)stack;(void)arg;(void)pri;(void)out;(void)core;return pdPASS; }
static int os_mbuf_append(struct os_mbuf *m,const void *data,unsigned len)
{ if(fail_append||m->len+len>512)return -1;memcpy(m->data+m->len,data,len);m->len+=len;return 0; }
static int os_mbuf_copydata(struct os_mbuf *m,unsigned offset,unsigned len,void *out)
{ if(offset+len>m->len)return -1;memcpy(out,m->data+offset,len);return 0; }
static struct os_mbuf read_buffer;
static struct os_mbuf *ble_hs_mbuf_att_pkt(void)
{ if(fail_alloc)return NULL;read_buffer.len=0;return &read_buffer; }
static int os_mbuf_appendfrom(struct os_mbuf *to,struct os_mbuf *from,unsigned offset,unsigned length)
{ return os_mbuf_append(to,from->data+offset,length); }
static void os_mbuf_free_chain(struct os_mbuf *om) { (void)om; }
#define BLE_HS_DBG_ASSERT(c) ((void)(c))
#define BLE_HS_LOG(...) ((void)0)
static struct os_mbuf *ble_hs_mbuf_from_flat(const void *data,unsigned len)
{ if(fail_alloc)return NULL;allocated_mbuf.len=0;os_mbuf_append(&allocated_mbuf,data,len);return &allocated_mbuf; }
static int ble_gatts_notify_custom(uint16_t conn,uint16_t handle,struct os_mbuf *om)
{
    ++notify_calls;sent_handle=handle;sent_length=om->len;memcpy(sent_data,om->data,om->len>8?8:om->len);
    struct ble_gap_event e={.type=BLE_GAP_EVENT_NOTIFY_TX};
    e.notify_tx.conn_handle=conn;e.notify_tx.attr_handle=handle;e.notify_tx.status=notify_rc;
    ++event_calls;gap_event(&e,NULL);++mbuf_frees;return notify_rc;
}
static int nimble_port_init(void) { return init_rc; }
static int nimble_port_deinit(void) { ++deinit_calls;return 0; }
static void nimble_port_run(void) {}
static void nimble_port_freertos_init(void (*fn)(void *)) { (void)fn;++host_starts; }
static void nimble_port_freertos_deinit(void) {}
void ble_store_config_init(void) { ++store_inits; }
static int ble_store_util_status_rr(void) { return 0; }
static int ble_store_util_delete_peer(const ble_addr_t *addr) { ++delete_calls;deleted_peer=*addr;return store_rc; }
static void ble_svc_gap_init(void) {}
static void ble_svc_gatt_init(void) {}
static int ble_svc_gap_device_name_set(const char *name) { (void)name;return 0; }
static int ble_svc_gap_device_appearance_set(uint16_t appearance) { (void)appearance;return 0; }
static int ble_att_set_preferred_mtu(uint16_t mtu) { return mtu==64?0:1; }
static int ble_gatts_count_cfg(const struct ble_gatt_svc_def *defs) { (void)defs;++count_calls;return count_rc; }
static int ble_gatts_add_svcs(const struct ble_gatt_svc_def *defs)
{
    ++add_calls;if(add_rc)return add_rc;
    unsigned handle=10;
    for(const struct ble_gatt_svc_def *s=defs;s->type;++s) {
        ++handle;
        for(const struct ble_gatt_chr_def *c=s->characteristics;c->uuid;++c) {
            handle+=2;if(c->val_handle)*c->val_handle=handle;
            if(c->flags&BLE_GATT_CHR_F_NOTIFY)++handle;
            if(c->descriptors)for(struct ble_gatt_dsc_def *d=c->descriptors;d->uuid;++d)++handle;
        }
    }
    return 0;
}
static int ble_hs_util_ensure_addr(int prefer_random) { return prefer_random?1:0; }
static int ble_gap_adv_set_fields(const struct ble_hs_adv_fields *f) { advertised=*f;return fields_rc; }
static int ble_gap_adv_rsp_set_fields(const struct ble_hs_adv_fields *f) { scan_response=*f;return 0; }
static bool ble_gap_adv_active(void) { return adv_active; }
static int ble_gap_adv_start(uint8_t addr,const void *remote,int32_t duration,const struct ble_gap_adv_params *p,
                            int (*cb)(struct ble_gap_event *,void *),void *arg)
{ (void)remote;(void)duration;(void)cb;(void)arg;++adv_calls;advertised_address=addr;advertised_params=*p;adv_active=!adv_rc;return adv_rc; }
static int ble_gap_security_initiate(uint16_t conn) { (void)conn;++security_calls;return security_rc; }
static int ble_gap_terminate(uint16_t conn,uint8_t reason) { (void)conn;(void)reason;return 0; }
static int ble_gap_conn_find(uint16_t conn,struct ble_gap_conn_desc *out)
{ if(!mock_connected||conn!=mock_conn)return BLE_HS_ENOTCONN;memset(out,0,sizeof(*out));
  out->conn_handle=conn;out->sec_state.encrypted=mock_encrypted;out->peer_id_addr.val[0]=42;return 0; }
