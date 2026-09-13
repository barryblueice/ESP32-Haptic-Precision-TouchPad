"""Run the real haptic worker with deterministic time and recording hardware."""
import ctypes as C
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def run_service_tests():
    with tempfile.TemporaryDirectory(prefix="surface_worker_") as temp:
        dll = Path(temp) / ("service.dll" if sys.platform == "win32" else "service.so")
        cmd = [shutil.which("clang") or "clang", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared"]
        if sys.platform == "win32":
            cmd += ["-nostdlib", "-fuse-ld=lld", "-fno-stack-protector", "-Xlinker", "/noentry"]
        else: cmd += ["-fPIC"]
        cmd += ["-I", str(HERE / "host_include"), "-I", str(ROOT / "main"),
                str(HERE / "service_host_stub.c"),
                str(ROOT / "main/I2C/SUB_DEV/surface_haptic_runtime.c"),
                str(ROOT / "main/I2C/SUB_DEV/surface_haptic_policy.c"), "-o", str(dll)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode: raise RuntimeError(result.stdout + result.stderr)
        lib = C.CDLL(str(dll))
        lib.service_test_run.argtypes = [C.c_uint32]
        lib.service_test_field.argtypes = [C.c_uint32]
        lib.service_test_field.restype = C.c_uint32
        field = lib.service_test_field
        scenarios = ["press_release", "initialize_failure", "playback_failure", "baseline_failure",
                     "continuous_click_heartbeat_timeout", "process_failure", "sleep_wake_no_replay",
                     "wake_failure", "immediate_disable", "task_allocation_failure", "slow_baseline_expiry"]
        try:
            for scenario, name in enumerate(scenarios):
                lib.service_test_run(scenario)
                values = [field(i) for i in range(9)]
                assert values[5:9] == [1, 4, 0, 0], (name, values) # diagnostic once, fault, empty, valid args
                assert field(3) == (0 if scenario == 9 else 1), (name, "firmware reloaded")
                assert field(1) >= 1, (name, "boost left on")
                if scenario == 0:
                    assert [field(i) for i in [0, 10, 11]] == [2, 21, 15]
                elif scenario in [1, 3, 8, 9, 10]: assert field(0) == 0, (name, values)
                elif scenario in [2, 5, 7]: assert field(0) == 1, (name, values)
                elif scenario == 4:
                    assert field(0) > 100 and field(2) == 2010, (name, values)
                elif scenario == 6:
                    assert [field(i) for i in [0, 4, 10, 11, 12]] == [3, 1, 21, 21, 15], (name, values)
                print("worker scenario: " + name + " ... ok", file=sys.stderr)
        finally:
            if sys.platform == "win32":
                kernel = C.WinDLL("kernel32", use_last_error=True)
                kernel.FreeLibrary.argtypes = [C.c_void_p]
                kernel.FreeLibrary(lib._handle)
        return dict(passed=len(scenarios), scenarios=scenarios, actual_c_worker=True,
                    time_and_hardware="simulated; no board")


if __name__ == "__main__":
    print(run_service_tests())
