"""Compile the receiver's real C bodies against SDK stubs, then execute exported cases."""
import ctypes
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import _ctypes

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def source(path):
    return re.sub(r'^#include[^\n]*\n|^#pragma once[^\n]*\n', '',
                  (ROOT / path).read_text(encoding="utf-8"), flags=re.M)


def main():
    parts = [(HERE / "host_runtime.h").read_text()]
    files = ["main/protocol.h", "main/input/report_buffer.h", "main/input/input_pipeline.h",
             "main/wireless/wireless.h", "main/usb/usbhid.h", "main/nvs/ptp_nvs.h",
             "main/protocol.c", "main/input/report_buffer.c", "main/input/input_pipeline.c",
             "main/wireless/broadcast.c", "main/wireless/heartbeat.c", "main/wireless/wifi_receive.c",
             "main/usb/usbhid.c", "main/nvs/ptp_nvs.c", "main/main.c", "tools/receiver/host_cases.c"]
    parts += [source(path) for path in files]
    code = "\n".join(parts).replace("while (true)", "while (test_steps-- > 0)")
    native_path = os.pathsep.join(p for p in os.environ.get("PATH", "").split(os.pathsep)
                                if "esp-clang" not in p.lower())
    clang = shutil.which("clang", path=native_path)
    if not clang:
        raise RuntimeError("Native Windows clang/lld is required (not esp-clang)")
    cases = re.findall(r"EXPORT int (check_\w+)\(void\)", parts[-1])
    with tempfile.TemporaryDirectory(prefix="receiver-checks-") as temp:
        c, dll = Path(temp) / "checks.c", Path(temp) / "checks.dll"
        c.write_text(code, encoding="utf-8")
        result = subprocess.run([clang, "-std=c11", "-O1", "-fno-builtin", "-mno-stack-arg-probe",
                                 "-Werror=implicit-function-declaration", "-shared", "-nostdlib",
                                 "-fuse-ld=lld", "-Wl,/noentry", "-Wl,/nodefaultlib",
                                 str(c), "-o", str(dll)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        library = ctypes.CDLL(str(dll))
        try:
            for case in cases:
                line = getattr(library, case)()
                if line:
                    context = code.splitlines()[max(0, line - 2):line + 1]
                    raise AssertionError(f"{case}: line {line}: {context}")
                print(case + ": passed")
        finally:
            _ctypes.FreeLibrary(library._handle)
    output = ROOT / "build/receiver_validation"
    output.mkdir(parents=True, exist_ok=True)
    (output / "host_result.json").write_text(json.dumps({"passed": True, "count": len(cases),
                                                       "cases": cases, "clang": clang}, indent=2) + "\n")
    print(f"{len(cases)} production-C scenarios passed")


if __name__ == "__main__":
    main()
