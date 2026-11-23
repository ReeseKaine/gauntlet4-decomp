"""
Differential test: C translation (g4_bits.c) vs. the real ROM routines run in
the Unicorn 68000 emulator. Same memory image, registers and X flag go into
both; afterwards all 16 registers, the X flag, the stack area below SP and the
bit buffer must match.

Usage: python3 test_bits.py <rom.bin> <libg4bits.so> [trials]
Needs: pip install unicorn
"""
import ctypes, random, struct, sys
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL
from unicorn.m68k_const import *

ROM = open(sys.argv[1], "rb").read()
lib = ctypes.CDLL(sys.argv[2])
TRIALS = int(sys.argv[3]) if len(sys.argv) > 3 else 3000

class M68K(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8), ("x", ctypes.c_uint32), ("z", ctypes.c_uint32),
                ("sr", ctypes.c_uint32), ("usp", ctypes.c_uint32),
                ("n", ctypes.c_uint32), ("v", ctypes.c_uint32), ("cf", ctypes.c_uint32)]

MEM = (ctypes.c_uint8 * 0x1000000)()
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
FUNCS = {}
for name, addr, rt in (("g4_get_bits", 0x12F1A, ctypes.c_uint32),
                       ("g4_put_bits", 0x12EDE, None),
                       ("g4_ck_step",  0x12F4A, None)):
    f = getattr(lib, name); f.restype = rt; f.argtypes = [ctypes.POINTER(M68K)]
    FUNCS[addr] = (name, f)

BUF, SP = 0xFFF000, 0xFFFD00
SENTINEL, PROBE, PROBE_END = 0xE00000, 0xE00100, 0xE00006
DREG = [UC_M68K_REG_D0 + i for i in range(8)]
AREG = [UC_M68K_REG_A0 + i for i in range(8)]
X_BIT = 0x10                                      # X is bit 4 of SR/CCR

uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
try: uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
except Exception: pass
uc.mem_map(0, 0x100000, UC_PROT_ALL); uc.mem_write(0, ROM)
uc.mem_map(0xFF0000, 0x10000, UC_PROT_ALL)
# Flag probe: the routine's rts lands here and the CPU itself stores SR.
# (Reading SR through the Unicorn API can return stale, lazily-computed flags.)
uc.mem_map(0xE00000, 0x1000, UC_PROT_ALL)
uc.mem_write(SENTINEL, bytes.fromhex('40f900e00100'))      # move.w sr,($e00100).l

def run(entry, d, a, x, buf):
    # --- reference: real ROM code
    uc.mem_write(0xFF0000, b"\0" * 0x10000)
    uc.mem_write(a[4], buf)
    uc.mem_write(SP, struct.pack(">I", SENTINEL))
    # SR first: changing SR can swap A7 between the user and supervisor stacks
    uc.reg_write(UC_M68K_REG_SR, 0x2700 | (X_BIT if x else 0))
    for i in range(8): uc.reg_write(DREG[i], d[i])
    for i in range(7): uc.reg_write(AREG[i], a[i])
    uc.reg_write(UC_M68K_REG_A7, SP)
    uc.emu_start(entry, PROBE_END, count=50_000_000)
    ref = ([uc.reg_read(r) & 0xFFFFFFFF for r in DREG], [uc.reg_read(r) & 0xFFFFFFFF for r in AREG],
           int(bool(struct.unpack('>H', bytes(uc.mem_read(PROBE, 2)))[0] & X_BIT)),
           bytes(uc.mem_read(SP - 0x40, 0x44)), bytes(uc.mem_read(a[4], len(buf))))
    # --- C translation. Same state: return address already at SP. The C
    # function models everything up to (not including) its final rts, so the
    # caller pops the return slot afterwards, like rts would.
    ctypes.memset(ctypes.addressof(MEM) + 0xFF0000, 0, 0x10000)
    MEM[0:len(ROM)] = list(ROM)
    for i, b in enumerate(buf): MEM[a[4] + i] = b
    for i, b in enumerate(struct.pack(">I", SENTINEL)): MEM[SP + i] = b
    m = M68K(); m.d[:] = d; m.a[:7] = a; m.a[7] = SP; m.x = x
    ret = FUNCS[entry][1](ctypes.byref(m))
    m.a[7] += 4                                   # the routine's rts
    got = (list(m.d), list(m.a), m.x, bytes(MEM[SP - 0x40:SP + 4]), bytes(MEM[a[4]:a[4] + len(buf)]))
    return ref, got, ret

rng = random.Random(1993)
cases = []
for t in range(TRIALS):
    entry = (0x12F1A, 0x12EDE, 0x12F4A)[t % 3]
    d = [rng.getrandbits(32) for _ in range(8)]
    a = [rng.getrandbits(32) & 0xFFFFFF for _ in range(7)]
    a[4] = BUF
    d[7] = (d[7] & 0xFFFF0000) | rng.randrange(0, 1200)          # bit position
    if entry in (0x12F1A, 0x12EDE):
        d[1] = (d[1] & 0xFFFF0000) | rng.choice([1, 2, 3, 4, 5, 6, 7, 10, 13, 14, 16, 17, 21, 28, 31, 32])
    else:
        d[2] = (d[2] & 0xFFFF0000) | rng.randrange(0, 2)          # bit value
    cases.append((entry, d, a, rng.randrange(2), bytes(rng.getrandbits(8) for _ in range(0x200))))

# Edge cases the game never uses (buffers placed low in RAM: Unicorn has no 24-bit wrap)
LOW = 0xFF2000
cases.append((0x12F1A, [0, 0, 0, 0, 0, 0, 0, 0x155], [0, 0, 0, 0, LOW, 0, 0], 0, bytes(range(256)) * 40))  # count 0: 65536 reads
for count in (0, 33, 40, 63, 64, 65, 96, 0x7FFF, 0x8000, 0x8001, 0xFFFF):
    for x in (0, 1):
        d = [0xDEADBEEF, 0x12340000 | count, 0x55, 0x66, 0x77, 0x88, 0x3A5, 0xFFF0]  # D7 near wrap
        cases.append((0x12EDE, d, [0, 0, 0, 0, LOW, 0, 0], x, bytes(rng.getrandbits(8) for _ in range(0x2000))))

fails = 0
for entry, d, a, x, buf in cases:
    ref, got, ret = run(entry, d, a, x, buf)
    ok = ref == got and (entry != 0x12F1A or ret == got[0][0])
    if not ok:
        fails += 1
        if fails <= 5:
            print(f"MISMATCH {FUNCS[entry][0]} d={[hex(v) for v in d]} x={x}")
            for name, r, g in (("D", ref[0], got[0]), ("A", ref[1], got[1])):
                for i, (p, q) in enumerate(zip(r, g)):
                    if p != q: print(f"   {name}{i}: rom {p:#010x}  c {q:#010x}")
            if ref[2] != got[2]: print(f"   X: rom {ref[2]}  c {got[2]}")
            if ref[3] != got[3]: print("   stack bytes differ")
            if ref[4] != got[4]: print("   buffer bytes differ")

by = {n: sum(1 for c in cases if FUNCS[c[0]][0] == n) for n, _ in FUNCS.values()}
print(f"{len(cases)} cases ({', '.join(f'{v} {k[3:]}' for k, v in by.items())}): "
      f"{len(cases)-fails} match, {fails} mismatch")
sys.exit(1 if fails else 0)
