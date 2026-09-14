"""Compile unchanged pressure/gesture function bodies with the production event core.

Only ESP-IDF includes and hardware are replaced. Structs and function bodies are
read from this checkout for every run; no copied pressure or gesture algorithm.
"""
import ctypes as C
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def run_input_tests():
    header = (ROOT / "main/SYS/hid_msg.h").read_text()
    structs = re.findall(r"typedef struct[^;]*?\{.*?\}\s*(\w+);", header, re.S)
    needed = ["tp_finger_t", "tp_multi_msg_t", "mouse_hid_report_t"]
    declarations = []
    for block in re.findall(r"typedef struct[^;]*?\{.*?\}\s*\w+;", header, re.S):
        if re.search(r"}\s*(\w+);", block)[1] in needed: declarations.append(block)
    assert all(name in structs for name in needed)
    queue = (ROOT / "main/I2C/TP/i2c_queue.c").read_text()
    pressure = queue[queue.index("typedef struct {\n    bool tracking_contact;"):queue.index("void update_simulated_scan_time")]
    gesture = (ROOT / "main/I2C/TP/ptp_simulated_mouse_gesture.c").read_text()
    gesture = re.sub(r'^#include[^\n]*\n', '', gesture, flags=re.M)
    preamble = '''
#include <stdint.h>
#include <stdbool.h>
#include "I2C/SUB_DEV/surface_haptic_runtime.h"
#ifdef _WIN32
#define EXPORT __declspec(dllexport)
int _fltused = 0;
#else
#define EXPORT
#endif
#define CONFIG_PTP_SIMULATED_MOUSE_MODE 1
#define FORCE_CLICK_PRESS_STABLE_FRAMES 2
#define FORCE_CLICK_RELEASE_STABLE_FRAMES 2
#define FORCE_CLICK_MOVE_DEADZONE 45
#define CLICK_REGION_SPLIT_X 1150
#define SENSITIVITY 2.0f
#define PTP_MODE 1
#define MOUSE_MODE 0
static uint8_t current_tp_mode = PTP_MODE, ptp_button_press_threshold = 2;
static uint8_t click_light_weight_threshold = 80, click_midium_weight_threshold = 100, click_strong_weight_threshold = 130;
static uint16_t device_config_x_max(void) { return 2302; }
static int abs(int x) { return x < 0 ? -x : x; }
float sqrtf(float);
float fabsf(float);
static surface_haptic_runtime_t input_runtime;
static uint32_t time_ms;
uint8_t ptp_haptic_click_intensity_get(void) { return 63; }
void cs40l25_surface_button_update(bool down, uint8_t setting) {
    surface_runtime_button(&input_runtime, down, setting, time_ms);
}
'''
    wrapper = '''
static void zero_bytes(void *ptr, unsigned int size) {
    volatile unsigned char *p = ptr;
    for (unsigned int i = 0; i < size; ++i) p[i] = 0;
}
EXPORT void input_test_reset(void) {
    zero_bytes(&input_runtime, sizeof(input_runtime));
    zero_bytes(&ptp_force_click_state, sizeof(ptp_force_click_state));
    zero_bytes(&m_state, sizeof(m_state));
    surface_runtime_state(&input_runtime, SURFACE_READY);
}
EXPORT int input_test_frame(unsigned int mask, unsigned int force, int simulated, unsigned int x, unsigned int time) {
    tp_multi_msg_t msg = {0};
    int count = 0;
    time_ms = time / 10;
    msg.scan_time = time;
    for (unsigned int i = 0; i < 5; ++i) {
        if (mask & (1U << i)) {
            ++count;
            msg.fingers[i].tip_switch = msg.fingers[i].confidence = 1;
            msg.fingers[i].pressure_z = force;
            msg.fingers[i].x = x; msg.fingers[i].y = 400;
        }
    }
    if (simulated) {
        mouse_hid_report_t report;
        msg.button_mask = force;
        parse_ptp_simulated_mouse_report(&msg, &report);
        (void)ptp_simulated_mouse_click_needs_release();
        return report.buttons;
    }
    ptp_update_force_click_button(&msg, count);
    cs40l25_surface_button_update(msg.button_mask != 0, 63);
    return msg.button_mask;
}
EXPORT int input_test_pop(void) {
    surface_haptic_event_t event;
    if (!surface_runtime_pop(&input_runtime, time_ms, &event)) return -1;
    return event.release ? 256 + event.pair.release_index : event.pair.press_index;
}
'''
    with tempfile.TemporaryDirectory(prefix="surface_input_") as temp:
        source = Path(temp) / "input.c"
        source.write_text(preamble + '\n'.join(declarations) + pressure + gesture + wrapper, encoding="utf-8")
        dll = Path(temp) / ("input.dll" if sys.platform == "win32" else "input.so")
        cmd = [shutil.which("clang") or "clang", "-std=c11", "-O2", "-fno-math-errno", "-Wall", "-Wextra", "-Werror", "-shared"]
        if sys.platform == "win32": cmd += ["-nostdlib", "-fuse-ld=lld", "-fno-stack-protector", "-Xlinker", "/noentry"]
        else: cmd += ["-fPIC"]
        cmd += ["-I", str(ROOT / "main"), str(source), str(HERE / "policy_host_stub.c")]
        cmd += [str(ROOT / "main/I2C/SUB_DEV" / ("surface_haptic_" + name + ".c")) for name in ["policy", "runtime"]]
        cmd += ["-o", str(dll)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode: raise RuntimeError(result.stdout + result.stderr)
        lib = C.CDLL(str(dll))
        frame, pop, reset = lib.input_test_frame, lib.input_test_pop, lib.input_test_reset
        cases = 0
        try:
            for changed_mask in [0, 3, 2]:
                reset()
                for t in [0, 10, 20]: frame(1, 150, 0, 400, t)
                assert pop() == 21
                for t in [30, 40, 50]: frame(1, 150, 0, 500, t)
                assert pop() == -1 # moving/holding cannot repeat pressure feedback
                frame(changed_mask, 150, 0, 500, 60)
                assert pop() == 271 and pop() == -1
                cases += 1
            reset()
            for t in [0, 10, 20]: frame(1, 150, 0, 400, t)
            assert pop() == 21
            for t in range(30, 180, 10): frame(1, 0, 0, 400, t)
            assert pop() == 271 and pop() == -1
            cases += 1
            for mask, button in [(1, 1), (3, 2), (7, 4)]:
                reset(); frame(mask, 0, 1, 400, 100)
                assert pop() == -1
                assert frame(0, 0, 1, 400, 200) == button
                assert [pop(), pop(), pop()] == [21, 271, -1]
                cases += 1
            reset(); frame(1, 0, 1, 400, 100); frame(0, 0, 1, 400, 200)
            assert [pop(), pop()] == [21, 271]
            frame(1, 0, 1, 400, 300)
            assert frame(1, 0, 1, 440, 2100) == 1
            assert pop() == 21
            frame(1, 0, 1, 460, 2150); assert pop() == -1
            frame(0, 0, 1, 460, 2200); assert pop() == 271
            cases += 1
        finally:
            if sys.platform == "win32":
                kernel = C.WinDLL("kernel32", use_last_error=True)
                kernel.FreeLibrary.argtypes = [C.c_void_p]
                kernel.FreeLibrary(lib._handle)
        print(f"input C scenarios: {cases} passed", file=sys.stderr)
        return dict(passed=cases, production_function_bodies=True,
                    coverage="pressure debounce/hold/release, no finger, multi-finger, tracked contact, simulated tap/right/middle click and drag")


if __name__ == "__main__": print(run_input_tests())
