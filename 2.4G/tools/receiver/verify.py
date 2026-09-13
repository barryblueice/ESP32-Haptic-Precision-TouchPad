"""Production C regressions and ESP32-S2 build. Does not flash or modify Main."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def environment():
    setup = Path(json.loads((ROOT / ".vscode/settings.json").read_text())["idf.currentSetup"])
    candidates = [Path(os.environ.get("IDF_TOOLS_PATH", "")) / "eim_idf.json",
                  Path(os.environ.get("SystemDrive", "C:") + "/Espressif/tools/eim_idf.json")]
    for path in candidates:
        if not path.is_file():
            continue
        for item in json.loads(path.read_text())["idfInstalled"]:
            if Path(item["path"]).resolve() != setup.resolve():
                continue
            env = os.environ.copy()
            result = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass",
                                     "-File", item["activationScript"], "-e"],
                                    check=True, capture_output=True, text=True)
            for line in result.stdout.splitlines():
                key, sep, value = line.partition("=")
                if sep and re.fullmatch(r"[A-Z_]+", key) and key != "SYSTEM_PATH":
                    env[key] = value + os.pathsep + env.get("PATH", "") if key == "PATH" else value
            env["IDF_PATH"] = str(setup)
            return env, item["python"]
    if os.environ.get("IDF_PATH") and Path(os.environ["IDF_PATH"]).resolve() == setup.resolve():
        return os.environ.copy(), sys.executable
    raise RuntimeError("Activate the VS Code ESP-IDF environment or install its EIM metadata")


def run_logged(name, command, env, logs, cwd=ROOT):
    print("Running " + name, flush=True)
    result = subprocess.run(command, cwd=cwd, env=env, capture_output=True)
    (logs / (name + ".log")).write_bytes(result.stdout + result.stderr)
    if result.returncode:
        print((result.stdout + result.stderr).decode(errors="replace")[-12000:])
        raise RuntimeError(name + " failed; see " + str(logs / (name + ".log")))
    print(name + ": passed", flush=True)


def build_commands(env, python):
    """Reuse VS Code's existing cache; never silently switch an existing build."""
    cache_path = ROOT / "build/CMakeCache.txt"
    if cache_path.exists():
        cache = cache_path.read_text()
        project = json.loads((ROOT / "build/project_description.json").read_text())
        if (Path(project["idf_path"]).resolve() != Path(env["IDF_PATH"]).resolve()
                or Path(project["project_path"]).resolve() != ROOT.resolve()
                or project["target"] != "esp32s2"):
            raise RuntimeError("Existing build does not match VS Code's ESP-IDF/project/ESP32-S2 target; cache preserved")
        ninja = re.search(r"^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$", cache, re.M)[1]
        return [ninja, "-C", str(ROOT / "build")], [ninja, "-C", str(ROOT / "build"), "size"]
    idf = [python, str(Path(env["IDF_PATH"]) / "tools/idf.py"), "-B", "build"]
    return idf + ["-D", "IDF_TARGET=esp32s2", "build"], idf + ["size"]


def windows_arguments(command):
    count = ctypes.c_int()
    parse = ctypes.windll.shell32.CommandLineToArgvW
    parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
    argv = parse(command, ctypes.byref(count))
    try:
        return [argv[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(argv, ctypes.c_void_p))


def variants(env, logs):
    commands = json.loads((ROOT / "build/compile_commands.json").read_text())
    entry = next(c for c in commands if c["file"].replace("\\", "/").endswith("/main/usb/usb_descriptor.c"))
    args = entry.get("arguments") or windows_arguments(entry["command"])
    clean, skip = [], False
    for arg in args:
        if skip:
            skip = False
        elif arg in ["-o", "-MF", "-MT", "-MQ"]:
            skip = True
        elif arg not in ["-c", "-MD", "-MMD", "-MP"]:
            clean.append(arg)
    for legacy in [0, 1]:
        for interval in [1, 10]:
            header = logs / f"variant_{legacy}_{interval}.h"
            header.write_text('#include "sdkconfig.h"\n#undef CONFIG_LAST_GEN_DESC\n'
                              f'#define CONFIG_LAST_GEN_DESC {legacy}\n'
                              '#undef CONFIG_TOUCHPAD_USB_INPUT_INTERVAL_MS\n'
                              f'#define CONFIG_TOUCHPAD_USB_INPUT_INTERVAL_MS {interval}\n')
            run_logged(f"descriptor_{legacy}_{interval}",
                       clean + ["-fsyntax-only", "-include", str(header)], env, logs, entry["directory"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--host-only", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    options = parser.parse_args()
    logs = ROOT / "build/receiver_validation"
    logs.mkdir(parents=True, exist_ok=True)
    summary = {"time_utc": datetime.now(timezone.utc).isoformat(), "flashed": False,
               "board_validation": "not performed", "checks": {}}
    try:
        if not options.build_only:
            run_logged("host", [sys.executable, "-B", str(HERE / "host_checks.py")], os.environ.copy(), logs)
            summary["checks"]["host"] = json.loads((logs / "host_result.json").read_text())
        if not options.host_only:
            env, python = environment()
            summary.update(idf_path=env["IDF_PATH"], python=python)
            build_command, size_command = build_commands(env, python)
            summary["build_command"] = build_command
            run_logged("build", build_command, env, logs)
            summary["checks"]["build"] = True
            config_hash = hashlib.sha256((ROOT / "sdkconfig").read_bytes()).hexdigest()
            variants(env, logs)
            summary["checks"]["descriptor_variants"] = 4
            summary["checks"]["variant_config_unchanged"] = (
                hashlib.sha256((ROOT / "sdkconfig").read_bytes()).hexdigest() == config_hash)
            if not summary["checks"]["variant_config_unchanged"]:
                raise RuntimeError("Descriptor checks unexpectedly changed sdkconfig")
            run_logged("size", size_command, env, logs)
            summary["artifacts"] = [
                {"path": str(p.relative_to(ROOT)), "bytes": p.stat().st_size,
                 "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                for p in sorted((ROOT / "build").glob("ESP32-Haptic-2.4G-Receiver.*"))
                if p.suffix in [".bin", ".elf"]]
        summary["passed"] = True
    except Exception as error:
        summary.update(passed=False, error=str(error))
        raise
    finally:
        fingerprint = hashlib.sha256()
        for path in sorted((ROOT / "main").rglob("*")):
            if path.is_file():
                fingerprint.update(path.relative_to(ROOT).as_posix().encode())
                fingerprint.update(path.read_bytes())
        for name in ["CMakeLists.txt", "sdkconfig.defaults", "dependencies.lock"]:
            fingerprint.update(name.encode())
            fingerprint.update((ROOT / name).read_bytes())
        summary["source_sha256"] = fingerprint.hexdigest()
        (logs / "result.json").write_text(json.dumps(summary, indent=2) + "\n")
        if not options.host_only and not options.build_only:
            (HERE / "validation.json").write_text(json.dumps(summary, indent=2) + "\n")


if __name__ == "__main__":
    main()
