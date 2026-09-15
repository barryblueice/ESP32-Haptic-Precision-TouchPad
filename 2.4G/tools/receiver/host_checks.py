"""Run shared production-C receiver regressions, including RSTP v2 actions."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
MAIN = ROOT.parent / 'Main'

def main():
    native_path = os.pathsep.join(p for p in os.environ.get('PATH','').split(os.pathsep)
                                if 'esp-clang' not in p.lower())
    clang = os.environ.get('HOST_CLANG') or shutil.which('clang', path=native_path)
    if not clang:
        raise RuntimeError('Set HOST_CLANG to native Windows clang (not esp-clang)')
    subprocess.run([sys.executable, str(MAIN/'tests/run_receiver_tests.py'), clang], check=True)
    result = json.loads((MAIN/'build/receiver-host-tests/result.json').read_text(encoding='utf-8'))
    result.update(count=len(result['cases']), clang=clang)
    output = ROOT/'build/receiver_validation'
    output.mkdir(parents=True, exist_ok=True)
    (output/'host_result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')

if __name__ == '__main__': main()
