"""Incremental verification in the existing VS Code ESP-IDF environment. No flashing."""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    settings = json.loads((ROOT / '.vscode/settings.json').read_text())
    cache = (ROOT / 'build/CMakeCache.txt').read_text()
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:[^=]+=(.+)$', cache, re.M)[1]
    python = re.search(r'^PYTHON:[^=]+=(.+)$', cache, re.M)[1]
    commands = json.loads((ROOT / 'build/compile_commands.json').read_text())
    from check_variants import arguments
    compiler = arguments(next(c['command'] for c in commands if c['file'].endswith('main.c')))[0]
    env = os.environ.copy()
    host_clang = shutil.which('clang')
    if host_clang and 'esp-clang' in host_clang.lower():
        # Native DLL tests require a Windows-target compiler. ESP-IDF's clang
        # defaults to an embedded target and cannot build the host test DLLs.
        host_paths = os.pathsep.join(p for p in env.get('PATH', '').split(os.pathsep) if 'esp-clang' not in p.lower())
        host_clang = shutil.which('clang', path=host_paths)
    if not host_clang:
        raise RuntimeError('Native Windows Clang is required for the host tests')
    activation = None
    eim_config = Path(python).parents[4] / 'eim_idf.json'
    if eim_config.exists():
        installs = json.loads(eim_config.read_text())['idfInstalled']
        setup = next(item for item in installs if Path(item['path']).resolve() == Path(settings['idf.currentSetup']).resolve())
        activation = setup['activationScript']
        # -e prints the installed environment and returns before changing EIM's
        # selected setup or activating an interactive PowerShell session.
        exported = subprocess.run(['powershell', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                   '-File', activation, '-e'], capture_output=True, text=True, check=True)
        for line in exported.stdout.splitlines():
            key, separator, value = line.partition('=')
            if separator and re.fullmatch(r'[A-Z_]+', key) and key != 'SYSTEM_PATH':
                env[key] = value + os.pathsep + env.get('PATH', '') if key == 'PATH' else value
    env['IDF_PATH'] = settings['idf.currentSetup']
    env['PATH'] = os.pathsep.join([str(Path(p).parent) for p in [python, compiler, ninja]]) + os.pathsep + env['PATH']
    host_env = env.copy()
    host_env['PATH'] = str(Path(host_clang).parent) + os.pathsep + env['PATH']
    logs = ROOT / 'build/input_validation'
    logs.mkdir(exist_ok=True)
    config_before = digest(ROOT / 'sdkconfig')
    result = {'time_utc': datetime.now(timezone.utc).isoformat(), 'idf_path': env['IDF_PATH'],
              'python': python, 'compiler': compiler, 'ninja': ninja,
              'activation_script': activation, 'esp_rom_elf_dir': env.get('ESP_ROM_ELF_DIR'),
              'host_clang': host_clang,
              'clean_performed': False, 'full_variant_builds': False, 'flashed': False,
              'board_validation': 'not performed', 'checks': {}}
    steps = [
        ('surface_regression', [python, '-B', str(ROOT / 'tools/surface_fw/verify.py'), '--self-test']),
        ('input_regression', [python, '-B', str(HERE / 'test_pipeline.py')]),
        ('config_regression', [python, '-B', str(HERE / 'test_config.py')]),
        ('incremental_build', [ninja, '-j', '4', '-C', str(ROOT / 'build')]),
        ('variant_syntax', [python, '-B', str(HERE / 'check_variants.py'), '--report', str(HERE / 'variant_checks.json')]),
    ]
    for name, command in steps:
        print(f'Running {name}...', flush=True)
        selected_env = host_env if name in ['surface_regression', 'input_regression', 'config_regression'] else env
        try:
            completed = subprocess.run(command, cwd=ROOT, env=selected_env, capture_output=True,
                                       timeout=600 if name == 'incremental_build' else 180)
        except subprocess.TimeoutExpired as error:
            completed = subprocess.CompletedProcess(command, 124, error.stdout or b'',
                (error.stderr or b'') + b'\nVerification step timed out.\n')
        log = logs / f'{name}.log'
        log.write_bytes(completed.stdout + completed.stderr)
        result['checks'][name] = {'passed': completed.returncode == 0, 'log': str(log.relative_to(ROOT))}
        if completed.returncode:
            result['failed_step'] = name
            (HERE / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
            print((completed.stdout + completed.stderr).decode(errors='replace')[-10000:])
            return completed.returncode
        print(f'{name}: passed', flush=True)
    result['sdkconfig_unchanged'] = digest(ROOT / 'sdkconfig') == config_before
    result['existing_surface_checks'] = 65
    result['input_scenarios'] = len(re.findall(r'EXPORT int test_\w+\(void\)', (HERE / 'host_cases.c').read_text()))
    result['config_scenarios'] = len(re.findall(r'EXPORT int test_\w+\(void\)', (HERE / 'config_cases.c').read_text()))
    result['variant_syntax'] = json.loads((HERE / 'variant_checks.json').read_text())
    result['artifacts'] = []
    for name in ['ESP32_HAPTIC_PRECISION_TOUCHPAD.bin', 'ESP32_HAPTIC_PRECISION_TOUCHPAD.elf']:
        artifact = ROOT / 'build' / name
        result['artifacts'].append({'path': str(artifact.relative_to(ROOT)), 'bytes': artifact.stat().st_size,
                                    'sha256': digest(artifact)})
    # Source fingerprint excludes this generated report and build artifacts.
    fingerprint = hashlib.sha256()
    for file in sorted((ROOT / 'main').rglob('*')):
        if file.is_file() and file.suffix in ['.c', '.h', '.txt', '.yml']:
            fingerprint.update(file.relative_to(ROOT).as_posix().encode())
            fingerprint.update(file.read_bytes())
    result['source_sha256'] = fingerprint.hexdigest()
    (HERE / 'validation.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(f'All checks passed; {result["input_scenarios"]} input scenarios; sdkconfig unchanged={result["sdkconfig_unchanged"]}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
