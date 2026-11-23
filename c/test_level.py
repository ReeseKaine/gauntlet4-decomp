"""
Differential test: g4_level.c (set_level_ptr, map_decode and its fill /
vertical-run / remap helpers) vs. the real ROM routines in Unicorn.

map_decode is run whole (its helpers translated too, no stand-ins): on
every real level record the tables point at, and on random byte streams,
with random mode flags so all four remap tables are used. set_level_ptr is
run on random level numbers and flags. Compared afterwards: all registers,
X, and the whole 64 KB of work RAM.

Build: gcc -std=c99 -O1 -Wall -Wextra -pedantic -shared -fPIC -o libg4.so \\
           g4_bits.c g4_password.c g4_password_b.c g4_dispatch.c g4_main.c g4_level.c test_shim.c
Usage: python3 test_level.py <rom.bin> <libg4.so> [random-runs]
"""
import ctypes, random, struct, sys, os
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL
from unicorn.m68k_const import *

ROM = open(sys.argv[1], "rb").read()
lib = ctypes.CDLL(os.path.abspath(sys.argv[2]))
RUNS = int(sys.argv[3]) if len(sys.argv) > 3 else 3000

class M68K(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8), ("x", ctypes.c_uint32),
                ("z", ctypes.c_uint32), ("sr", ctypes.c_uint32), ("usp", ctypes.c_uint32),
                ("n", ctypes.c_uint32), ("v", ctypes.c_uint32), ("cf", ctypes.c_uint32)]
EXT_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(M68K), ctypes.c_uint32)
RD_CB = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
WR_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
lib.g4_test_run.argtypes = [ctypes.POINTER(M68K), ctypes.c_uint32]
lib.g4_test_set.argtypes = [EXT_CB, RD_CB, WR_CB]
MEM = (ctypes.c_uint8 * 0x1000000)()
ctypes.memmove(MEM, ROM, len(ROM))
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
ctypes.c_int.in_dll(lib, "g4_use_translated").value = 1        # helpers run as C too
unexpected = []
def ext(cp, t): unexpected.append(t); return 2                  # no external calls expected: stop
CBS = (EXT_CB(ext), RD_CB(lambda a, s: 0), WR_CB(lambda a, v, s: None))
lib.g4_test_set(*CBS)

RAM, SENTINEL, PROBE, PROBE_END, SP0 = 0xFF0000, 0xE00000, 0xE00100, 0xE00006, 0xFFFD00
DREG = [UC_M68K_REG_D0 + i for i in range(8)]; AREG = [UC_M68K_REG_A0 + i for i in range(8)]
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
try: uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
except Exception: pass
uc.mem_map(0, 0x100000, UC_PROT_ALL); uc.mem_write(0, ROM)
uc.mem_map(RAM, 0x10000, UC_PROT_ALL)
uc.mem_map(0xE00000, 0x1000, UC_PROT_ALL); uc.mem_write(SENTINEL, bytes.fromhex("40f900e00100"))

def rom_run(entry, d, a, x, sp, ram):
    uc.mem_write(RAM, bytes(ram))
    uc.reg_write(UC_M68K_REG_SR, 0x2700 | (0x10 if x else 0))
    for i in range(8): uc.reg_write(DREG[i], d[i])
    for i in range(7): uc.reg_write(AREG[i], a[i])
    uc.mem_write(sp - 4, struct.pack(">I", SENTINEL)); uc.reg_write(UC_M68K_REG_A7, sp - 4)
    uc.emu_start(entry, PROBE_END, count=5_000_000)
    srw = struct.unpack(">H", bytes(uc.mem_read(PROBE, 2)))[0]
    return [uc.reg_read(r) & 0xFFFFFFFF for r in DREG + AREG], (srw >> 4) & 1, bytes(uc.mem_read(RAM, 0x10000))

def c_run(entry, d, a, x, sp, ram):
    ctypes.memmove(ctypes.addressof(MEM) + RAM, bytes(ram), 0x10000)
    cpu = M68K()
    for i in range(8): cpu.d[i] = d[i]
    for i in range(7): cpu.a[i] = a[i]
    cpu.a[7] = sp; cpu.x = x
    assert lib.g4_test_run(ctypes.byref(cpu), entry) == 0, f"external call(s) {unexpected}"
    return list(cpu.d) + list(cpu.a), cpu.x, ctypes.string_at(ctypes.addressof(MEM) + RAM, 0x10000)

def compare(entry, r, d, a, x, sp, ram):
    A, B = rom_run(entry, d, a, x, sp, ram), c_run(entry, d, a, x, sp, ram)
    if A == B: return None, A
    if A[0] != B[0]: return f"regs ROM {[hex(v) for v in A[0]]} C {[hex(v) for v in B[0]]}", A
    if A[1] != B[1]: return f"X ROM {A[1]} C {B[1]}", A
    k = next(i for i in range(0x10000) if A[2][i] != B[2][i])
    return f"RAM at {RAM + k:#x}: ROM {A[2][k]:#x} C {B[2][k]:#x}", A

L = lambda a: struct.unpack(">I", ROM[a:a + 4])[0]
def records():
    """(name, address) of every record the three tables point at (inside the ROM)."""
    out = []
    for name, tab, base in (("BC4", 0x25BC4, L(0x4316)), ("DC4", 0x25DC4, L(0x4316)), ("FC4", 0x25FC4, L(0x42AA))):
        for i in range(128):
            adr = (base + L(tab + 4 * i)) & 0xFFFFFFFF
            if adr < len(ROM) - 16: out.append((f"{name}[{i}]", adr))
    return out

def mode_flags(r, ram):
    ram[0xE838:0xE83A] = struct.pack(">H", r.choice([0, 1, 1, 2, 3]))
    ram[0xE842:0xE844] = struct.pack(">H", r.choice([0, 1, 2]))
    ram[0xE875] = r.choice([0, 0, 0xFF])
    ram[0xE846:0xE848] = struct.pack(">H", r.choice([0, 5, 10, 11, 12, 0xFFFF]))

def decode_case(r, rec_addr, ram):
    mode_flags(r, ram)
    sp = SP0 - 4
    ram[sp - RAM:sp - RAM + 4] = struct.pack(">I", rec_addr)       # the caller's move.l ($ffe75c).l,-(SP)
    d = [r.randrange(1 << 32) for _ in range(8)]; a = [r.randrange(1 << 32) for _ in range(7)]
    a[4] = 0xFFE246                                               # map buffer, as 0x18D8C sets it
    return compare(0x019122, r, d, a, r.randint(0, 1), sp, ram)

ok = True; fails = []
recs = records(); seen_tables = set()
r = random.Random(1)
for name, adr in recs:
    ram = bytearray(r.randbytes(0x10000))
    p, res = decode_case(r, adr, ram)
    if p: fails.append((name, p))
print(f"{'PASS' if not fails else 'FAIL'} map_decode on {len(recs)} real level records")
ok &= not fails; [print("   ", f) for f in fails[:5]]

fails = []
for s in range(RUNS):
    r = random.Random(1000 + s)
    ram = bytearray(r.randbytes(0x10000))
    rec = 0xFF8000; ram[rec - RAM:rec - RAM + 0x800] = r.randbytes(0x800)   # random stream in RAM
    p, _ = decode_case(r, rec, ram)
    if p: fails.append((s, p))
print(f"{'PASS' if not fails else 'FAIL'} map_decode on {RUNS} random streams")
ok &= not fails; [print("   ", f) for f in fails[:5]]

fails = []
for s in range(RUNS):
    r = random.Random(50000 + s)
    ram = bytearray(r.randbytes(0x10000))
    ram[0xE758:0xE75A] = struct.pack(">H", r.choice([r.randint(1, 128), r.randint(0x81, 0x100), r.randrange(0x10000)]))
    ram[0xE790] = r.choice([0, 0xFF]); ram[0xE799] = r.choice([0, 0, 0x01])
    ram[0xE766:0xE768] = struct.pack(">H", r.randint(1, 128))
    d = [r.randrange(1 << 32) for _ in range(8)]; a = [r.randrange(1 << 32) for _ in range(7)]
    p, _ = compare(0x0040D8, r, d, a, r.randint(0, 1), SP0, ram)
    if p: fails.append((s, p))
print(f"{'PASS' if not fails else 'FAIL'} set_level_ptr on {RUNS} random cases")
ok &= not fails; [print("   ", f) for f in fails[:5]]
print("\nALL PASS" if ok else "\nSOME TESTS FAILED")
