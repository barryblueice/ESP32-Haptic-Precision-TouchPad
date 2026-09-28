"""Production C regressions and ESP32-S2 build. Does not flash or modify Main."""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
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


def build_commands(env, python, build):
    """Reuse VS Code's existing cache; never silently switch an existing build."""
    cache_path = build / "CMakeCache.txt"
    legacy_cache = ROOT / "build/CMakeCache.txt"
    legacy_ccache = (re.search(r"^CCACHE_ENABLE:[^=]+=(.+)$", legacy_cache.read_text(), re.M)
                     if legacy_cache.exists() else None)
    desired_ccache = None if not legacy_ccache else legacy_ccache[1].strip().lower() in ["true", "on", "1", "yes"]
    ninja = shutil.which("ninja", path=env["PATH"])
    if cache_path.exists():
        cache = cache_path.read_text()
        project = json.loads((build / "project_description.json").read_text())
        if (Path(project["idf_path"]).resolve() != Path(env["IDF_PATH"]).resolve()
                or Path(project["project_path"]).resolve() != ROOT.resolve()
                or project["target"] != "esp32s2"):
            raise RuntimeError("Existing build does not match VS Code's ESP-IDF/project/ESP32-S2 target; cache preserved")
        ninja = re.search(r"^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$", cache, re.M)[1]
        current_ccache = re.search(r"^CCACHE_ENABLE:[^=]+=(.+)$", cache, re.M)
        current_ccache = bool(current_ccache and current_ccache[1].strip().lower() in ["true", "on", "1", "yes"])
        if desired_ccache is None or current_ccache == desired_ccache:
            return [ninja, "-C", str(build)], [ninja, "-C", str(build), "size"]
    if not ninja:
        raise RuntimeError("Ninja is missing from the selected ESP-IDF environment")
    idf = [python, str(Path(env["IDF_PATH"]) / "tools/idf.py"), "-B", str(build)]
    # idf.py appends CCACHE_ENABLE after -D options; use its actual CLI switch.
    if desired_ccache is not None:
        idf += ["--ccache" if desired_ccache else "--no-ccache"]
    definitions = ["-D", "IDF_TARGET=esp32s2"]
    if build != (ROOT / "build").resolve():
        # Isolate SDK migration outputs from the existing development build.
        for name in ["sdkconfig", "dependencies.lock"]:
            destination = build / name
            if not destination.exists():
                shutil.copyfile(ROOT / name, destination)
        definitions += ["-D", "SDKCONFIG=" + (build / "sdkconfig").as_posix(),
                        "-D", "RECEIVER_DEPENDENCIES_LOCK=" + (build / "dependencies.lock").as_posix()]
    return idf + definitions + ["build"], [ninja, "-C", str(build), "size"]


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


def variants(env, logs, build):
    commands = json.loads((build / "compile_commands.json").read_text())
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


def hashes(paths):
    return {p.relative_to(ROOT.parent).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
            for p in sorted(paths) if p.is_file()}


def sources():
    paths = [p for p in (ROOT / "main").rglob("*") if p.suffix in [".c", ".h", ".inc", ".yml"]
             or p.name in ["CMakeLists.txt", "Kconfig.projbuild"]]
    paths += [ROOT / name for name in ["CMakeLists.txt", "sdkconfig", "sdkconfig.defaults", "dependencies.lock"]]
    paths += [p for p in HERE.iterdir() if p.suffix in [".py", ".c", ".h"]]
    paths += [ROOT.parent / "Main/main/SYS" / name for name in
              ["aux_output.c", "aux_output.h", "aux_descriptor.inc", "wireless_extension.h", "wireless_probe.h"]]
    return hashes(paths)


def main():
    parser = argparse.ArgumentParser()
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--host-only", action="store_true")
    mode.add_argument("--build-only", action="store_true")
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--host-clang", default=os.environ.get("HOST_CLANG"))
    parser.add_argument("--host-clang-arg", action="append", default=[])
    options = parser.parse_args()
    build = options.build_dir.resolve()
    if not build.is_relative_to(ROOT) or build == ROOT:
        parser.error("--build-dir must be a subdirectory of the receiver project")
    logs = build / "receiver_validation"
    logs.mkdir(parents=True, exist_ok=True)
    protected = [ROOT / name for name in ["sdkconfig", "sdkconfig.defaults", "dependencies.lock"]]
    if build != (ROOT / "build").resolve():
        protected += [ROOT / "build" / name for name in ["CMakeCache.txt", "project_description.json", "build.ninja"]]
    protected_before = hashes(protected)
    source_before = sources()
    summary = {"time_utc": datetime.now(timezone.utc).isoformat(), "flashed": False,
               "build_directory": build.relative_to(ROOT).as_posix(),
               "board_validation": "not performed", "checks": {}, "passed": False}
    try:
        if not options.build_only:
            command = [sys.executable, "-B", str(HERE / "host_checks.py"), "--build-dir", str(build)]
            if options.host_clang:
                command += ["--clang", options.host_clang]
            command += ["--clang-arg=" + arg for arg in options.host_clang_arg]
            run_logged("host", command, os.environ.copy(), logs)
            summary["checks"]["host"] = json.loads((build / "receiver-host-tests/result.json").read_text())
        if not options.host_only:
            env, python = environment()
            summary.update(idf_path=env["IDF_PATH"], python=python)
            build_command, size_command = build_commands(env, python, build)
            summary["build_command"] = build_command
            run_logged("build", build_command, env, logs)
            summary["checks"]["build"] = True
            project = json.loads((build / "project_description.json").read_text())
            config = Path(project["config_file"])
            config_hash = hashlib.sha256(config.read_bytes()).hexdigest()
            variants(env, logs, build)
            summary["checks"]["descriptor_variants"] = 4
            summary["checks"]["variant_config_unchanged"] = (
                hashlib.sha256(config.read_bytes()).hexdigest() == config_hash)
            if not summary["checks"]["variant_config_unchanged"]:
                raise RuntimeError("Descriptor checks unexpectedly changed sdkconfig")
            run_logged("size", size_command, env, logs)
            summary["artifacts"] = [
                {"path": str(p.relative_to(ROOT)), "bytes": p.stat().st_size,
                 "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                for p in sorted(build.glob("ESP32-Haptic-2.4G-Receiver.*"))
                if p.suffix in [".bin", ".elf"]]
            if len(summary["artifacts"]) != 2:
                raise RuntimeError("Expected both receiver BIN and ELF artifacts")
            summary["build_inputs"] = hashes([config, build / "dependencies.lock", build / "CMakeCache.txt"])
            if build != (ROOT / "build").resolve():
                # Ignore only the IDF entry: other resolved versions and hashes must stay pinned.
                check_lock = (
                    "import sys,yaml; "
                    "a=yaml.safe_load(open(sys.argv[1]))['dependencies']; "
                    "b=yaml.safe_load(open(sys.argv[2]))['dependencies']; "
                    "a.pop('idf',None); b.pop('idf',None); assert a==b, 'Managed dependencies changed'"
                )
                run_logged("dependency_versions", [python, "-c", check_lock,
                           str(ROOT / "dependencies.lock"), str(build / "dependencies.lock")], env, logs)
                summary["checks"]["dependency_versions_unchanged"] = True
        summary["checks"]["original_config_and_cache_unchanged"] = hashes(protected) == protected_before
        summary["checks"]["sources_unchanged_during_validation"] = sources() == source_before
        if not all(summary["checks"][name] for name in
                   ["original_config_and_cache_unchanged", "sources_unchanged_during_validation"]):
            raise RuntimeError("Source/configuration changed during validation; results cannot be certified")
        summary["passed"] = True
    except Exception as error:
        summary.update(passed=False, error=str(error))
        raise
    finally:
        summary["source_files"] = source_before
        summary["source_sha256"] = hashlib.sha256(json.dumps(source_before, sort_keys=True).encode()).hexdigest()
        (logs / "result.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8", newline="\n")
        if not options.host_only and not options.build_only:
            (HERE / "validation.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
