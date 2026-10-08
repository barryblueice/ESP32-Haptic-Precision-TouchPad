"""Check exported C inference against Python and replay all labelled raw traces."""
import ctypes
import hashlib
import json
import random
import subprocess
import sys
from run_host_tests import ROOT, body

sys.path.insert(0,str(ROOT/'main/SYS/knuckle_training'))
import train

samples = train.read_data(train.DEFAULT_DATA)['samples']
heads = train.fit(samples)
digest = hashlib.sha256(json.dumps(samples,sort_keys=True,separators=(',',':')).encode()).hexdigest()
assert (ROOT/'main/SYS/knuckle_model.h').read_text(encoding='utf-8') == train.header(heads,digest,samples)
code = (ROOT/'tests/host_runtime.h').read_text(encoding='utf-8').split('typedef int esp_err_t;')[0]
code += '#include <xmmintrin.h>\nfloat sqrtf(float x) { return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(x))); }\n'
code += '\n' + '\n'.join(body(ROOT/'main'/name) for name in (
    'SYS/knuckle_model.h','SYS/knuckle_gesture.h','SYS/knuckle_gesture.c'))
code += '''
int _fltused = 0;
static knuckle_gesture_t replay_state;
EXPORT void replay_reset(void) { knuckle_gesture_reset(&replay_state); }
EXPORT unsigned replay_frame(const uint8_t *p,uint32_t t) {
    knuckle_result_t r=knuckle_gesture_update(&replay_state,p,t);
    return r.suppress | (r.screenshot<<1) | (replay_state.pending<<2);
}
EXPORT int model_predict(unsigned head,float z,float area) {
    return knuckle_model_accepts(head?&knuckle_model_complete:&knuckle_model_onset,z,area);
}
'''
out = ROOT/'build/knuckle-model-host-tests'
out.mkdir(parents=True,exist_ok=True)
c,dll = out/'checks.c',out/'checks.dll'
c.write_text(code,encoding='utf-8')
subprocess.run([sys.argv[1],'-std=c11','-O1','-fno-builtin','-mno-stack-arg-probe',
    '-Werror=implicit-function-declaration','-shared','-nostdlib','-fuse-ld=lld',
    '-Wl,/noentry','-Wl,/nodefaultlib',str(c),'-o',str(dll)],check=True)
lib = ctypes.CDLL(str(dll))
lib.model_predict.argtypes = (ctypes.c_uint,ctypes.c_float,ctypes.c_float)
lib.replay_frame.argtypes = (ctypes.POINTER(ctypes.c_uint8),ctypes.c_uint32)
lib.replay_frame.restype = ctypes.c_uint
rng = random.Random(7)
for head in range(2):
    xs = [train.extract(s)[head] for s in samples]
    xs += [[rng.uniform(0,255),rng.uniform(0,300)] for _ in range(1000)]
    for x in xs:
        assert bool(lib.model_predict(head,*x)) == train.accepts(heads[head],x), (head,x)
print('export reproducibility and 2010 Python/C predictions: passed')
for sample in samples:
    lib.replay_reset()
    positive = sample['label']=='knock'
    for i,(dt,raw) in enumerate(sample['frames']):
        r = lib.replay_frame((ctypes.c_uint8*64).from_buffer_copy(bytes.fromhex(raw)),dt)
        assert bool(r&1)==positive and not r&2, (sample['start_ms'],i,r)
    assert bool(r&4)==positive
print('all five captured contacts through production state machine: passed')
lib.replay_reset()
pair = sorted((s for s in samples if s['session']=='followup_double_knock'),key=lambda s:s['start_ms'])
shots = 0
for sample in pair:
    for dt,raw in sample['frames']:
        r = lib.replay_frame((ctypes.c_uint8*64).from_buffer_copy(bytes.fromhex(raw)),sample['start_ms']+dt)
        assert r&1
        shots += bool(r&2)
assert shots == 1 and r&2 and not r&4
print('captured real double knock, including area=25 release tail: passed')
(out/'result.json').write_text(json.dumps({'passed':True,'model_sha256':digest,
    'parity_predictions':2010,'captured_contacts':5,'real_double_knock_screenshots':shots},indent=2)+'\n',encoding='utf-8')
