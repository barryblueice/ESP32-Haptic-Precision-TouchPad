"""Run the actual C downloader against a simulated bus; never access hardware."""
import ctypes as C
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
FW=ROOT/'main/force_firmware'
PAYLOAD=bytes(int(x,16) for x in re.findall(r'0x([0-9A-F]{2})(?=,|\n)',(FW/'force_image.c').read_text()))
U8=C.c_uint8
P=C.POINTER(U8)
WRITE=C.CFUNCTYPE(C.c_int,C.c_void_p,P,C.c_size_t)
READ=C.CFUNCTYPE(C.c_int,C.c_void_p,P,C.c_size_t,P,C.c_size_t)
WAIT=C.CFUNCTYPE(None,C.c_void_p,C.c_uint32)
NOW=C.CFUNCTYPE(C.c_uint64,C.c_void_p)
class IO(C.Structure):
    _fields_=[('ctx',C.c_void_p),('write',WRITE),('read',READ),('wait_ms',WAIT),('now_ms',NOW)]
class Result(C.Structure):
    _fields_=[('stage',C.c_char_p),('offset',C.c_uint32),('status',U8)]

class Bus:
    def __init__(self,fail_at=None,busy_reg=None,commit_zero=False,finish_bad=False):
        self.clock=0;self.calls=[];self.fail_at=fail_at
        self.busy_reg=busy_reg;self.commit_zero=commit_zero;self.finish_bad=finish_bad
        self.callbacks=(WRITE(self.write),READ(self.read),WAIT(self.wait),NOW(lambda _:self.clock))
        self.io=IO(None,*self.callbacks)
    def write(self,_,tx,n):
        self.calls.append(('W',bytes(tx[:n])))
        return 123 if len(self.calls)==self.fail_at else 0
    def read(self,_,tx,n,rx,count):
        b=bytes(tx[:n]);self.calls.append(('WR',b,count))
        if len(self.calls)==self.fail_at:return 123
        if b[:1]==b'\xf9':
            v=0x80 if b[1]==self.busy_reg else (1 if b[1]==5 else 0)
            if b[1]==5 and self.commit_zero:v=0
            rx[0]=v
        elif b==bytes.fromhex('b60074'):
            rx[0]=0xA5;rx[1]=1 if self.finish_bad else 0
        return 0
    def wait(self,_,ms):self.clock+=ms

class DownloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('FORCE_TEST_CC') or shutil.which('clang') or r'D:\Swift\Toolchains\6.0.0+Asserts\usr\bin\clang.exe'
        if not Path(compiler).exists():raise RuntimeError('Set FORCE_TEST_CC to a Windows clang/lld compiler')
        cls.tmp=tempfile.TemporaryDirectory(prefix='force_protocol_')
        dll=Path(cls.tmp.name)/'force.dll'
        shim=Path(cls.tmp.name)/'memory.c'
        shim.write_text('#include <stddef.h>\nvoid *memset(void *p,int c,size_t n) { volatile unsigned char *b=p; for(size_t i=0;i<n;++i)b[i]=(unsigned char)c; return p; }\n')
        subprocess.run([compiler,'--target=x86_64-pc-windows-msvc','-shared','-nostdlib','-fuse-ld=lld',
            '-fno-stack-protector','-Xlinker','/noentry','-Wall','-Wextra','-Werror',str(FW/'force_download.c'),str(shim),'-o',str(dll)],check=True)
        cls.lib=C.CDLL(str(dll))
        cls.lib.force_download_run.argtypes=[C.POINTER(IO),P,C.c_size_t,C.POINTER(Result)]
        cls.lib.force_identity.argtypes=[P,C.c_size_t,C.POINTER(C.c_uint32)]
        cls.image=(U8*len(PAYLOAD)).from_buffer_copy(PAYLOAD)
    @classmethod
    def tearDownClass(cls):
        # Loaded DLL remains open until process exit on Windows.
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.tmp.cleanup()
    def run_bus(self,bus,length=None):
        result=Result()
        rc=self.lib.force_download_run(C.byref(bus.io),self.image,len(PAYLOAD) if length is None else length,C.byref(result))
        return rc,result
    def test_exact_image_and_wire_sequence(self):
        self.assertEqual(len(PAYLOAD),23076)
        self.assertEqual(hashlib.sha256(PAYLOAD).hexdigest(),'bf764f18f86043798c1afd657bcc85d78cbd5804faf7fff0a0909ef4dfb3b814')
        b=Bus();rc,result=self.run_bus(b)
        expected=[('W',bytes.fromhex(h)) for h in ('f75234','b6001e38','f77445','fa7203')]
        for i in range(16):expected += [('W',bytes([0xFA,2,0x80|i])),('WR',bytes.fromhex('f902'),1)]
        for i in range(0,len(PAYLOAD),16):expected.append(('W',bytes([0xF8,i>>8,i&255])+PAYLOAD[i:i+16]))
        expected += [('W',bytes.fromhex('fa0600000000245a00')),('W',bytes.fromhex('fa05c000')),
                     ('WR',bytes.fromhex('f905'),1),('W',bytes.fromhex('f75234')),('WR',bytes.fromhex('b60074'),2)]
        self.assertEqual(rc,0);self.assertEqual(b.calls,expected)
        self.assertEqual(result.stage,b'protocol_complete')
        self.assertEqual(len([x for x in b.calls if x[0]=='W' and x[1][0]==0xF8]),1443)
        self.assertEqual(len(b.calls[-6][1]),7) # Last F8: header + four bytes, no padding.
    def test_every_transport_failure_stops_immediately(self):
        good=Bus();self.run_bus(good)
        for failure in range(1,len(good.calls)+1):
            bus=Bus(fail_at=failure);rc,_=self.run_bus(bus)
            self.assertEqual(rc,123);self.assertEqual(len(bus.calls),failure)
    def test_item_busy_deadline(self):
        b=Bus(busy_reg=2);rc,result=self.run_bus(b)
        self.assertEqual(rc,-2);self.assertEqual(result.stage,b'poll_item')
        self.assertEqual(b.clock,2250)
        self.assertFalse(any(x[1][0]==0xF8 for x in b.calls))
    def test_commit_busy_deadline(self):
        b=Bus(busy_reg=5);rc,result=self.run_bus(b)
        self.assertEqual(rc,-2);self.assertEqual(result.stage,b'poll_commit')
        self.assertFalse(any(x[1]==bytes.fromhex('b60074') for x in b.calls))
    def test_zero_commit_and_finish_error(self):
        for bus,stage in [(Bus(commit_zero=True),b'poll_commit'),(Bus(finish_bad=True),b'finish_status')]:
            rc,result=self.run_bus(bus);self.assertEqual(rc,-3);self.assertEqual(result.stage,stage)
    def test_length_bounds(self):
        for n in (0,65536):
            b=Bus();rc,_=self.run_bus(b,n)
            self.assertEqual(rc,-1);self.assertEqual(b.calls,[])
    def test_identity_checksum_and_zero_rejection(self):
        value=C.c_uint32()
        for raw,expected in [('00000a009660ff',0x0A000096),('a5000a009c5aff',0x0A00009C)]:
            data=(U8*7).from_buffer_copy(bytes.fromhex(raw))
            self.assertEqual(self.lib.force_identity(data,7,C.byref(value)),0);self.assertEqual(value.value,expected)
            data[6]^=1;self.assertNotEqual(self.lib.force_identity(data,7,C.byref(value)),0)
        zeros=(U8*7)();self.assertNotEqual(self.lib.force_identity(zeros,7,C.byref(value)),0)
        self.assertNotEqual(self.lib.force_identity(zeros,6,C.byref(value)),0)

if __name__=='__main__':unittest.main(verbosity=2)
