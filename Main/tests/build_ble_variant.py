"""Compile/link both BLE mouse variants using VS Code's existing SDK objects and compile commands.

Does not edit sdkconfig or replace the default archive/firmware. All Main project
objects are recompiled per variant, including controller mode selection and the
input parser; only SDK libraries are reused. Run the normal IDF build first.
"""
import ctypes
import json
import re
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
BUILD=ROOT/'build'

def split_command(command):
    count=ctypes.c_int()
    fn=ctypes.windll.shell32.CommandLineToArgvW
    fn.argtypes=[ctypes.c_wchar_p,ctypes.POINTER(ctypes.c_int)]
    fn.restype=ctypes.POINTER(ctypes.c_wchar_p)
    p=fn(command,ctypes.byref(count))
    try:return [p[i] for i in range(count.value)]
    finally:
        free=ctypes.windll.kernel32.LocalFree
        free.argtypes=[ctypes.c_void_p];free(p)

def build_variant(variant, config):
    OUT=BUILD/'validation'/variant
    OUT.mkdir(parents=True,exist_ok=True)
    overlay=OUT/'profile.h'
    overlay.write_text('#include "'+(BUILD/'config/sdkconfig.h').as_posix()+'"\n'
                       '#undef CONFIG_ORI_MOUSE_MODE\n#undef CONFIG_PTP_SIMULATED_MOUSE_MODE\n'
                       '#define '+config+' 1\n',encoding='utf-8')
    entries=json.loads((BUILD/'compile_commands.json').read_text(encoding='utf-8'))
    objects=[]
    for entry in entries:
        source=Path(entry['file']).resolve()
        if not source.is_relative_to(ROOT/'main'):continue
        args=entry.get('arguments') or split_command(entry['command'])
        obj=OUT/'objects'/source.relative_to(ROOT/'main').with_suffix('.c.obj')
        obj.parent.mkdir(parents=True,exist_ok=True)
        out=[];i=0
        while i<len(args):
            if args[i] in ('-o','-MF','-MT','-MQ'):i+=2;continue
            if args[i] in ('-MD','-MMD','-MP'):i+=1;continue
            out.append(args[i]);i+=1
        out+=['-include',str(overlay),'-o',str(obj)]
        subprocess.run(out,cwd=entry['directory'],check=True)
        objects.append(obj)
        print('Compiled '+source.name,flush=True)
    assert objects
    compiler=Path(split_command(entries[0]['command'])[0])
    toolbin=compiler.parent
    ar=next(toolbin.glob('xtensa-esp32s3-elf-ar.exe'))
    archive=OUT/'libmain.a'
    archive.unlink(missing_ok=True)
    subprocess.run([str(ar),'qc',str(archive),*map(str,objects)],check=True)
    subprocess.run([str(ar),'s',str(archive)],check=True)
    cache=(BUILD/'CMakeCache.txt').read_text(encoding='utf-8')
    ninja=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('CMAKE_MAKE_PROGRAM:'))
    project=json.loads((BUILD/'project_description.json').read_text(encoding='utf-8'))
    elf=project['app_elf']
    commands=subprocess.check_output([ninja,'-C',str(BUILD),'-t','commands',elf],text=True)
    link=commands.splitlines()[-1].split(' && ',1)[1].rsplit(' && ',1)[0]
    args=split_command(link)
    for i,arg in enumerate(args):
        if arg.startswith('@') and arg.endswith('.rsp'):
            rsp_path=BUILD/arg[1:]
            if rsp_path.exists():
                rsp=rsp_path.read_text(encoding='utf-8')
            else:
                # Ninja removes response files after a successful link. Recreate
                # this rule's $in $LINK_PATH $LINK_LIBRARIES from the cached graph.
                graph=(BUILD/'build.ninja').read_text(encoding='utf-8')
                block=next(b for b in graph.split('\n\n') if b.startswith('build '+elf+': '))
                inputs=block.splitlines()[0].split(': ',1)[1].split(' ',1)[1].split(' |')[0]
                variables=dict(re.findall(r'^  (\w+) = (.*)$',block,re.M))
                rsp=inputs+' '+variables.get('LINK_PATH','')+' '+variables['LINK_LIBRARIES']
                rsp=rsp.replace('$:',':').replace('$ ',' ').replace('$$','$')
            assert 'esp-idf/main/libmain.a' in rsp
            rsp=rsp.replace('esp-idf/main/libmain.a',archive.as_posix())
            target=OUT/'link.rsp';target.write_text(rsp,encoding='utf-8');args[i]='@'+str(target)
        elif arg.startswith('-Wl,--Map='):args[i]='-Wl,--Map='+str(OUT/'firmware.map')
        elif i and args[i-1]=='-o':args[i]=str(OUT/elf)
    subprocess.run(args,cwd=BUILD,check=True)
    python=next(line.split('=',1)[1] for line in cache.splitlines() if line.startswith('PYTHON:'))
    subprocess.run([python,'-m','esptool','--chip','esp32s3','elf2image','--flash-mode','dio',
                    '--flash-freq','80m','--flash-size','4MB','-o',str(OUT/'firmware.bin'),str(OUT/elf)],check=True)
    subprocess.run([python,str(Path(project['idf_path'])/'components/partition_table/check_sizes.py'),
                    '--offset','0x8000','partition','--type','app',
                    str(BUILD/'partition_table/partition-table.bin'),str(OUT/'firmware.bin')],check=True)
    (OUT/'result.json').write_text(json.dumps({'passed':True,'idf':project['idf_path'],
        'mouse_config':config,'compiled_project_objects':len(objects),'reused_sdk_build':str(BUILD)},indent=2)+'\n',encoding='utf-8')
    print(variant+' firmware linked using existing SDK libraries: '+str(OUT/'firmware.bin'))


if __name__ == '__main__':
    build_variant('ble-simulated-mouse', 'CONFIG_PTP_SIMULATED_MOUSE_MODE')
    build_variant('ble-native-mouse', 'CONFIG_ORI_MOUSE_MODE')
