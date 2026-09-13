/* Include the actual service to reset its private storage between deterministic runs.
 * The hardware is simulated; all event/heartbeat/fault scheduling remains production C. */
#include "I2C/SUB_DEV/cs40l25_surface.c"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
#else
#define EXPORT
#endif
static uint32_t clock_ms, scenario, shots, off_count, fault_time, init_count, wake_count, diagnoses;
static uint32_t reads, last_shot, shot_indices[512], invalid_parameters;
static uint8_t live_strength;
int64_t esp_timer_get_time(void) { return (int64_t)clock_ms * 1000; }
uint8_t ptp_haptic_click_intensity_get(void) { return live_strength; }
bool surface_haptic_hw_initialize(void) { ++init_count; return scenario != 1; }
bool surface_haptic_hw_power_off(void) { ++off_count; return true; }
bool surface_haptic_hw_wake(void) { ++wake_count; clock_ms += 250; return scenario != 7; }
void surface_haptic_hw_diagnostics(uint8_t waveform) { (void)waveform; ++diagnoses; fault_time = clock_ms; }
uint32_t surface_haptic_hw_process(void)
{
    if ((scenario == 5 && clock_ms >= 10) || clock_ms >= (scenario == 4 ? 5000U : 1000U))
        return BSP_STATUS_FAIL; // End finite simulation by a genuine processing fault.
    return BSP_STATUS_OK;
}
uint32_t bsp_dut_has_processed(bool *changed)
{
    ++reads;
    if (scenario == 3) return BSP_STATUS_FAIL;
    if (scenario == 10 && reads == 1) clock_ms += 200; // Slow baseline makes the event stale.
    *changed = scenario != 4 && shots != last_shot;
    last_shot = shots;
    return BSP_STATUS_OK;
}
uint32_t bsp_dut_apply_haptic_mapping(uint8_t press, uint8_t release, uint16_t cp, uint16_t gpi, bool gpio)
{
    if (press != 21 || release != 15 || cp || gpi || gpio) ++invalid_parameters;
    return BSP_STATUS_OK;
}
uint32_t bsp_dut_trigger_haptic(uint8_t index, uint32_t duration)
{
    if (duration != 0) ++invalid_parameters;
    if (shots < 512) shot_indices[shots] = index;
    ++shots;
    return scenario == 2 ? BSP_STATUS_FAIL : BSP_STATUS_OK;
}
void vTaskDelete(void *task) { (void)task; }
void vTaskDelay(TickType_t ticks)
{
    clock_ms += ticks;
    if (scenario == 4) {
        cs40l25_surface_button_update((clock_ms / 10) % 2 != 0, 63);
    } else if (scenario == 6 || scenario == 7) {
        if (clock_ms == 10 || clock_ms == 310) cs40l25_surface_button_update(true, 63);
        if (clock_ms == 20) cs40l25_surface_set_modern_sleep(true);
        if (clock_ms == 30) cs40l25_surface_set_modern_sleep(false);
        if (clock_ms == 300 || clock_ms == 320) cs40l25_surface_button_update(false, 63);
    } else {
        if (clock_ms == 10) {
            cs40l25_surface_button_update(true, 63);
            if (scenario == 8) { live_strength = 0; cs40l25_surface_cancel_click(); }
        }
        if (clock_ms == 20) cs40l25_surface_button_update(false, live_strength);
    }
}
int xTaskCreatePinnedToCore(void (*fn)(void *), const char *name, unsigned int stack,
                            void *arg, unsigned int priority, void *handle, int core)
{
    (void)name; (void)stack; (void)priority; (void)handle; (void)core;
    if (scenario == 9) return 0;
    fn(arg);
    return pdPASS;
}
EXPORT void service_test_run(uint32_t which)
{
    volatile unsigned char *bytes = (volatile unsigned char *)&runtime;
    for (unsigned int i = 0; i < sizeof(runtime); ++i) bytes[i] = 0;
    started = sleep_requested = false;
    scenario = which;
    clock_ms = shots = off_count = fault_time = init_count = wake_count = diagnoses = reads = last_shot = invalid_parameters = 0;
    live_strength = 63;
    cs40l25_surface_init();
    // Sticky faults cannot restart the task or accept later clicks.
    cs40l25_surface_init();
    cs40l25_surface_set_modern_sleep(false);
    cs40l25_surface_button_update(false, 63);
    cs40l25_surface_button_update(true, 63);
}
EXPORT uint32_t service_test_field(uint32_t field)
{
    switch (field) {
    case 0: return shots;
    case 1: return off_count;
    case 2: return fault_time;
    case 3: return init_count;
    case 4: return wake_count;
    case 5: return diagnoses;
    case 6: return runtime.state;
    case 7: return runtime.count;
    case 8: return invalid_parameters;
    default: return field >= 10 && field < 522 ? shot_indices[field - 10] : 0xffffffff;
    }
}
