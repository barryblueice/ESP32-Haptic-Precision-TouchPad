"""Check trace wrappers preserve USB forwarding under truncation/drop/disabled capture."""
import ctypes
import subprocess
import sys
from run_host_tests import ROOT, body

base = (ROOT / 'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
runtime = r'''
/* The installed host Clang ships Android's stdatomic.h. Use its C11
 * builtins directly while keeping genuine atomic operations in this test. */
typedef _Atomic(unsigned) atomic_uint;
#define ATOMIC_INT_LOCK_FREE __CLANG_ATOMIC_INT_LOCK_FREE
#define memory_order_relaxed __ATOMIC_RELAXED
#define atomic_fetch_add_explicit __c11_atomic_fetch_add
#define atomic_load_explicit __c11_atomic_load
#define atomic_load(p) __c11_atomic_load(p, __ATOMIC_SEQ_CST)
#define USB_HOST_TRACE_ENABLED 1
#define pdTRUE 1
typedef int BaseType_t;
typedef void *QueueHandle_t;
typedef unsigned hid_report_type_t;
enum { DCD_EVENT_BUS_RESET=1, DCD_EVENT_UNPLUGGED=2, DCD_EVENT_SOF=3,
       DCD_EVENT_SUSPEND=4, DCD_EVENT_RESUME=5, DCD_EVENT_SETUP_RECEIVED=6,
       DCD_EVENT_XFER_COMPLETE=7 };
typedef struct { uint8_t rhport,event_id; union {
    uint8_t setup_received[8];
    struct { uint8_t ep_addr,result; uint32_t len; } xfer_complete;
}; } dcd_event_t;
static uint8_t saved[128];
static unsigned copied_size, sends, isr_sends, real_events, real_sets, real_stalls;
static bool full, isr;
static const dcd_event_t *last_event;
static const uint8_t *last_set;
static unsigned last_instance,last_id,last_type,last_length;
static int64_t esp_timer_get_time(void) { return 123456; }
static bool xPortInIsrContext(void) { return isr; }
static int xQueueSend(void *q,const void *data,unsigned wait)
{ (void)q; if(wait!=0) return 0; ++sends; if(full)return 0; memcpy(saved,data,copied_size);return 1; }
static int xQueueSendFromISR(void *q,const void *data,void *wake)
{ (void)wake; ++isr_sends;return xQueueSend(q,data,0); }
void __real_dcd_event_handler(const dcd_event_t *event,bool in_isr)
{ ++real_events;last_event=event;isr=in_isr; }
void __real_dcd_edpt_stall(uint8_t port,uint8_t ep)
{ (void)port;(void)ep;++real_stalls; }
uint16_t __real_tud_hid_get_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t *buf,uint16_t n)
{ (void)instance;(void)type; if(id==0xdb)return 0;for(unsigned i=0;i<n;i++)buf[i]=(uint8_t)i;return n; }
void __real_tud_hid_set_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,const uint8_t *buf,uint16_t n)
{ ++real_sets;last_set=buf;last_instance=instance;last_id=id;last_type=type;last_length=n; }
'''
# Test the actual producers and wrappers; printing/task allocation are checked by the IDF build.
source = body(ROOT / 'main/USB/usb_host_trace.c').split('static unsigned trace_u16')[0] + '\n#endif\n'
cases = r'''
EXPORT int check_trace(void)
{
    copied_size=sizeof(usb_trace_record_t);
    CHECK(copied_size <= sizeof(saved));
    uint8_t buf[256], payload[]={1,0xdb};
    dcd_event_t event={.rhport=0,.event_id=DCD_EVENT_SETUP_RECEIVED,
                      .setup_received={0xa1,1,0xdb,3,1,0,76,0}};
    /* Capture disabled or allocation failed: original handler still runs. */
    trace_queue=NULL;
    __wrap_dcd_event_handler(&event,true);
    CHECK(real_events==1 && sends==0 && last_event==&event && isr);
    trace_queue=(void*)1;
    __wrap_dcd_event_handler(&event,true);
    usb_trace_record_t record;memcpy(&record,saved,sizeof(record));
    CHECK(real_events==2 && isr_sends==1 && record.kind==TRACE_SETUP);
    CHECK(record.time_us==123456 && record.copied==8);
    CHECK(memcmp(record.bytes,event.setup_received,8)==0);
    /* Continuous input completion must not fill the diagnostic queue. */
    unsigned before=sends;
    event.event_id=DCD_EVENT_XFER_COMPLETE;event.xfer_complete.ep_addr=0x82;
    __wrap_dcd_event_handler(&event,true);
    CHECK(sends==before && real_events==3);
    event.xfer_complete.ep_addr=0x80;event.xfer_complete.len=64;
    __wrap_dcd_event_handler(&event,true);
    memcpy(&record,saved,sizeof(record));
    CHECK(record.kind==TRACE_EP0 && record.length==64 && real_events==4);
    /* Large Feature replies remain intact; only the trace is truncated. */
    CHECK(__wrap_tud_hid_get_report_cb(1,4,3,buf,sizeof(buf))==sizeof(buf));
    CHECK(buf[0]==0 && buf[255]==255);
    memcpy(&record,saved,sizeof(record));
    CHECK(record.kind==TRACE_GET && record.length==256 && record.copied==32);
    CHECK(record.bytes[31]==31 && record.requested==256);
    CHECK(__wrap_tud_hid_get_report_cb(1,0xdb,3,NULL,0)==0);
    memcpy(&record,saved,sizeof(record));
    CHECK(record.length==0 && record.copied==0);
    /* Unrecognized reports reach the application unchanged even when full. */
    full=true;
    __wrap_tud_hid_set_report_cb(2,0x99,3,payload,sizeof(payload));
    CHECK(real_sets==1 && last_set==payload && last_instance==2 && last_id==0x99);
    CHECK(last_type==3 && last_length==2 && payload[1]==0xdb);
    CHECK(atomic_load(&trace_lost)==1);
    __wrap_dcd_event_handler(&event,true);
    CHECK(real_events==5 && atomic_load(&trace_lost)==2);
    __wrap_dcd_edpt_stall(0,0x80);
    CHECK(real_stalls==1 && atomic_load(&trace_lost)==3);
    return 0;
}
'''
code = base + runtime + source + cases
out = ROOT / 'build/usb-host-trace-tests'
out.mkdir(parents=True, exist_ok=True)
c, dll = out / 'checks.c', out / 'checks.dll'
c.write_text(code, encoding='utf-8')
subprocess.run([sys.argv[1], '-std=c11', '-O1', '-fno-builtin', '-mno-stack-arg-probe',
                '-Werror=implicit-function-declaration', '-shared', '-nostdlib', '-fuse-ld=lld',
                '-Wl,/noentry', '-Wl,/nodefaultlib', str(c), '-o', str(dll)], check=True)
line = ctypes.CDLL(str(dll)).check_trace()
if line:
    raise AssertionError(f'{code.splitlines()[line-1]} (line {line})')
print('USB trace forwarding, ISR capture, filtering, truncation and overflow checks passed')
