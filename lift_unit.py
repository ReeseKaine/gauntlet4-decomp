#!/usr/bin/env python3
"""
lift_unit.py -- instruction-level check of lift.py.

Every distinct straight-line instruction found in the given generated files
(gen/*.c comments) is placed alone, followed by rts, in a scratch copy of the
ROM, lifted to its own C function, and run many times from random registers,
condition codes and RAM, in C and in Unicorn; registers, all five flags and
RAM must match. Instructions that read PC-relative data are tested at their
original address (with rts patched in after them), so their meaning holds.

  python3 lift_unit.py <rom.bin> <out.so> <runs> gen/a.c [gen/b.c ...]
"""
import ctypes, os, random, re, struct, subprocess, sys, tempfile
from unicorn import Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL
from unicorn.m68k_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift

ROM = bytearray(open(sys.argv[1], "rb").read())
RUNS = int(sys.argv[3])
files = sys.argv[4:]

SKIP = ("b", "db", "jsr", "jmp", "rts", "rte", "bsr", "bra", "link", "unlk")   # control flow: tested in routines
def is_control(m):
    b = m.split(".")[0]
    if b in ("btst", "bset", "bclr", "bchg"): return False
    return b.startswith(("b", "db")) or b in ("jsr", "jmp", "rts", "rte", "bsr", "link", "unlk")

# ------------------------------------------------------------ collect instructions
seen = {}
for f in files:
    for addr, text in re.findall(r"/\* ([0-9a-f]{6})  ([^*]*?) \*/", open(f).read()):
        a = int(addr, 16)
        i = lift.insn(a)
        if is_control(i.mnemonic): continue
        key = bytes(ROM[a:a + i.size])
        uses_pc = "(pc" in i.op_str
        k = (key, a if uses_pc else None)
        if k not in seen: seen[k] = (a, i)
units = list(seen.values())

# ------------------------------------------------------------ scratch ROM
SCRATCH = 0x180000                                          # past the 1 MB ROM (memory on both sides)
# each unit: the instruction, then move.w sr,($e00100).l (the CPU stores its own flags), then rts;
# Unicorn stops before the rts and the C function returns there, so nothing is popped on either side
TAIL = b"\x40\xf9\x00\xe0\x01\x00\x4e\x75"
img = bytearray(ROM) + bytes(0x200000 - len(ROM))
placed = []
slot = SCRATCH
patched = set()                                             # bytes changed for in-place units
skipped_pc = 0
for a, i in sorted(units, key=lambda u: u[0]):
    if "(pc" in i.op_str:
        span = set(range(a, a + i.size + 8))
        if span & patched: skipped_pc += 1; continue        # neighbour already patched: can't test in place
        at = a                                              # in place, rts after it
        img[a + i.size:a + i.size + 8] = TAIL; patched |= span
    else:
        at = slot; img[at:at + i.size] = ROM[a:a + i.size]; img[at + i.size:at + i.size + 8] = TAIL; slot += 24
    placed.append((at, a, i))
assert slot < 0x200000
# PC-relative units patched an rts over the next instruction; keep only one per address range
lift.ROM = bytes(img)
lift.FOLLOW_PUSHED = False
src = ['#include "../g4_ops.h"']
names = []
for k, (at, orig, i) in enumerate(placed):
    txt, _ = lift.lift_routine(at, f"g4_unit_{k}", {})
    src.append(txt); names.append(f"g4_unit_{k}")
src.append("void (*g4_units[])(M68K *) = {" + ", ".join(names) + "};")
gdir = os.path.join(HERE, "c", "gen_unit"); os.makedirs(gdir, exist_ok=True)
open(os.path.join(gdir, "units.c"), "w").write("\n".join(src))
open(os.path.join(gdir, "stub.c"), "w").write(
    '#include "../g4_m68k.h"\nG4_IO g4_io;\nuint8_t *g4_mem;\n'
    'void g4_jsr(M68K *c, uint32_t r, uint32_t t) { (void)c; (void)r; (void)t; }\n'
    'void g4_jmp(M68K *c, uint32_t t) { (void)c; (void)t; }\n'
    'extern void (*g4_units[])(M68K *);\nvoid g4_unit_run(M68K *c, int k) { g4_units[k](c); }\n')
so = sys.argv[2]                                            # where to build the unit library
subprocess.check_call(["gcc", "-std=c99", "-O1", "-w", "-shared", "-fPIC", "-o", so,
                       os.path.join(gdir, "units.c"), os.path.join(gdir, "stub.c")])
lib = ctypes.CDLL(so)

class M68K(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8), ("x", ctypes.c_uint32), ("z", ctypes.c_uint32),
                ("sr", ctypes.c_uint32), ("usp", ctypes.c_uint32),
                ("n", ctypes.c_uint32), ("v", ctypes.c_uint32), ("cf", ctypes.c_uint32)]
lib.g4_unit_run.argtypes = [ctypes.POINTER(M68K), ctypes.c_int]
MEM = (ctypes.c_uint8 * 0x1000000)()
ctypes.memmove(MEM, bytes(img), len(img))
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)

RAM, SENT, PROBE, PROBE_END, SP0 = 0xFF0000, 0xE00000, 0xE00100, 0xE00006, 0xFFFD00
RAMBUF = (ctypes.c_uint8 * 0x10000)()
def make_uc():
    u = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    try: u.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    except Exception: pass
    u.mem_map(0, 0x200000, UC_PROT_ALL); u.mem_write(0, bytes(img))
    u.mem_map_ptr(RAM, 0x10000, UC_PROT_ALL, ctypes.addressof(RAMBUF))
    u.mem_map(0xE00000, 0x1000, UC_PROT_ALL); u.mem_write(SENT, bytes.fromhex("40f900e00100"))
    return u
uc = make_uc()
DREG = [UC_M68K_REG_D0 + i for i in range(8)]; AREG = [UC_M68K_REG_A0 + i for i in range(8)]

def rand_regs(r):
    d = [r.choice([0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, r.randrange(1 << 32), r.randrange(64)])
         for _ in range(8)]
    a = [0xFF0000 | (r.randrange(0x8000) * 2) for _ in range(7)]          # address registers: even, in RAM
    return d, a

bad = {}
ONLY = os.environ.get("G4ONLY")                              # "unit,seed" to rerun one case
for k, (at, orig, i) in enumerate(placed):
    if ONLY and k != int(ONLY.split(",")[0]): continue
    for s in range(RUNS):
        if ONLY and s != int(ONLY.split(",")[1]): continue
        r = random.Random(k * 100003 + s)
        d, a = rand_regs(r); ccr = r.randrange(32)
        if i.mnemonic.startswith("divs"):                   # Unicorn crashes on 0x80000000 / -1 (its own host
            d = [x if x != 0x80000000 else 0x80000001 for x in d]   # division traps); f_divs guards that case
        if i.op_str.endswith("sr"):                         # SR writes: no trace mode, stay in supervisor
            d = [(x & ~0x9000) | 0x2000 for x in d]          # bit 12 too: Unicorn treats it as the 68020 M bit
        ram = r.randbytes(0x10000)
        # Unicorn
        ctypes.memmove(RAMBUF, ram, 0x10000)
        uc.mem_write(SP0 - 4, struct.pack(">I", SENT))
        uc.reg_write(UC_M68K_REG_SR, 0x2700 | ccr)
        for j in range(8): uc.reg_write(DREG[j], d[j])
        for j in range(7): uc.reg_write(AREG[j], a[j])
        uc.reg_write(UC_M68K_REG_A7, SP0 - 4)
        if os.environ.get("G4TRACE"): print("U", k, s, f"{orig:06x} {i.mnemonic} {i.op_str}", [hex(x) for x in d], [hex(x) for x in a], file=sys.stderr, flush=True)
        try: uc.emu_start(at, at + i.size + 6, count=10)
        except UcError as e:
            continue                                     # e.g. address taken outside memory: not a lifter question
        sr = struct.unpack(">H", bytes(uc.mem_read(PROBE, 2)))[0]          # written by the unit's move.w sr
        A = ([uc.reg_read(x) & 0xFFFFFFFF for x in DREG + AREG], sr & 0x1F, bytes(RAMBUF))
        # C
        ctypes.memmove(ctypes.addressof(MEM) + RAM, ram, 0x10000)
        cpu = M68K()
        for j in range(8): cpu.d[j] = d[j]
        for j in range(7): cpu.a[j] = a[j]
        cpu.a[7] = SP0 - 4; cpu.sr = 0x2700
        cpu.x, cpu.n, cpu.z, cpu.v, cpu.cf = (ccr >> 4) & 1, (ccr >> 3) & 1, (ccr >> 2) & 1, (ccr >> 1) & 1, ccr & 1
        wr = ctypes.addressof(MEM) + SP0 - 4
        ctypes.memmove(wr, struct.pack(">I", SENT), 4)
        lib.g4_unit_run(ctypes.byref(cpu), k)
        B = (list(cpu.d) + list(cpu.a), (cpu.x << 4) | (cpu.n << 3) | (cpu.z << 2) | (cpu.v << 1) | cpu.cf,
             ctypes.string_at(ctypes.addressof(MEM) + RAM, 0x10000))
        if A != B:
            why = []
            if A[0] != B[0]:
                j = next(j for j in range(16) if A[0][j] != B[0][j]); why.append(f"{'DA'[j // 8]}{j % 8}: ROM {A[0][j]:#x} C {B[0][j]:#x}")
            if A[1] != B[1]: why.append(f"CCR (XNZVC) ROM {A[1]:05b} C {B[1]:05b}, start {ccr:05b}")
            if A[2] != B[2]:
                j = next(j for j in range(0x10000) if A[2][j] != B[2][j]); why.append(f"RAM {RAM + j:#x}: ROM {A[2][j]:#x} C {B[2][j]:#x}")
            bad.setdefault(f"{orig:06x} {i.mnemonic} {i.op_str}", []).append((d, ccr, "; ".join(why)))
print(f"{len(placed)} distinct instructions, {RUNS} runs each" + (f" ({skipped_pc} PC-relative ones not placeable)" if skipped_pc else ""))
for name, fails in bad.items():
    d, ccr, why = fails[0]
    print(f"FAIL {name}: {len(fails)}/{RUNS}  e.g. {why}  (D={[hex(x) for x in d]})")
print("ALL PASS" if not bad else f"{len(bad)} instructions differ")
