"""Compile production boot/WSEQ functions and shared hardware flow with recording I/O.

Function bodies, register tables, macros and driver types come from this checkout.
Only bus, time, firmware loading and unrelated BSP operations are simulated.
"""
import ctypes as C
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
DRIVER = ROOT / "main/I2C/SUB_DEV/mcu-drivers/cs40l25"


def function(text, name):
    match = re.search(r"^(?:static )?(?:uint32_t|bool) " + name + r"\([^;]*?\)\s*\{", text, re.M)
    if not match:
        raise ValueError("Missing production function: " + name)
    end, depth = match.end(), 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[match.start():end]


def verify_boost_reference():
    folder = HERE / "boost_reference"
    manifest = json.loads((folder / "manifest.json").read_text())
    for name, expected in manifest["files"].items():
        if hashlib.sha256((folder / name).read_bytes()).hexdigest() != expected:
            raise ValueError("Boost reference hash mismatch: " + name)
    entries = json.loads((folder / "register_entries.json").read_text())
    assert len(entries) == 3
    for entry in entries:
        raw = bytes.fromhex(entry["raw_hex"])
        assert hashlib.sha256(raw).hexdigest() == entry["raw_sha256"]
        if entry["kind"] == "init":
            assert raw.hex() == "00003800000000aa"
        else:
            assert raw.hex() == "00380000000000aa"
        assert entry["register"] == "0x00003800" and entry["value"] == "0x000000aa"
    assert (11000 - 2550) // 50 + 1 == 0xAA
    return {"register": "0x00003800", "code": "0xAA", "parameter_mv": 11000,
            "external_boost": True, "board_measured": False}


def run_boost_tests():
    verify_boost_reference()
    sdk = (DRIVER / "cs40l25.c").read_text()
    bsp = (DRIVER / "bsp/bsp_cs40l25.c").read_text()
    hw = (ROOT / "main/I2C/SUB_DEV/surface_haptic_hw.c").read_text()
    macros = "\n".join(re.findall(r"^#define CS40L25_(?:FWID_CAL|INT2_MASK_DEFAULT|IRQ2_MASK\d_DEFAULT|IMASKSEQ_WORD_\d)[^\n]+", sdk, re.M))
    arrays = []
    for name in ["cs40l25_revb0_errata_patch", "cs40l25_wseq_regs", "cs40l25_irqmaskseq_patch"]:
        start = sdk.index("static const uint32_t " + name + "[]")
        arrays.append(sdk[start:sdk.index("};", start) + 2])
    start = bsp.index("uint32_t cs40l25_syscfg_regs[]")
    config = bsp[start:bsp.index("uint32_t bsp_dut_initialize", start)]
    bodies = "\n".join(function(sdk, name) for name in [
        "cs40l25_wseq_table_add", "cs40l25_wseq_add_block", "cs40l25_hibernate", "cs40l25_boot"])
    # Preserve the whole shared hardware implementation; replace only its includes.
    hw = re.sub(r'^#include[^\n]*\n', '', hw, flags=re.M)
    stub = (HERE / "boost_host_stub.c").read_text()
    source = stub.replace("/* PRODUCTION_SDK */", macros + "\n" + "\n".join(arrays) + config + bodies)
    source = source.replace("/* PRODUCTION_HW */", hw)
    with tempfile.TemporaryDirectory(prefix="surface_boost_") as temp:
        cfile = Path(temp) / "boost.c"
        cfile.write_text(source, encoding="utf-8")
        dll = Path(temp) / ("boost.dll" if sys.platform == "win32" else "boost.so")
        cmd = [shutil.which("clang") or "clang", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared"]
        if sys.platform == "win32":
            cmd += ["-nostdlib", "-fuse-ld=lld", "-fno-stack-protector", "-Xlinker", "/noentry"]
        else:
            cmd += ["-fPIC"]
        cmd += ["-I", str(HERE / "host_include"), "-I", str(ROOT / "main"), str(cfile), "-o", str(dll)]
        result = subprocess.run(cmd, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stdout + result.stderr)
        lib = C.CDLL(str(dll))
        lib.boost_test_run.argtypes = [C.c_uint32]
        lib.boost_test_run.restype = C.c_uint32
        scenarios = ["initialize_11v", "wake_11v", "initialize_read_failure", "initialize_mismatch",
                     "wake_read_failure", "wake_mismatch", "syscfg_write_failure", "wseq_restore_11v",
                     "wseq_entry_failure", "wseq_terminator_failure", "hibernate_command_failure",
                     "wseq_missing_symbol", "diagnostic_registers", "vbst_reserved_bits"]
        try:
            for i, name in enumerate(scenarios):
                code = lib.boost_test_run(i)
                assert code == 0, (name, "failed C assertion", code)
                print("boost scenario: " + name + " ... ok", file=sys.stderr)
        finally:
            if sys.platform == "win32":
                kernel = C.WinDLL("kernel32", use_last_error=True)
                kernel.FreeLibrary.argtypes = [C.c_void_p]
                kernel.FreeLibrary(lib._handle)
    return dict(passed=len(scenarios), scenarios=scenarios, actual_c_boot_wseq_and_hw=True,
                hardware="recording stub; no board")


if __name__ == "__main__":
    print(run_boost_tests())
