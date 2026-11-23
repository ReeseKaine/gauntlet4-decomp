"""
Differential test: C translation (g4_password.c + g4_bits.c) vs. the real ROM
routines run in the Unicorn 68000 emulator, for pw_encode, pw_decode,
split_tri and join_tri. Same RAM image, registers and X flag go into both;
afterwards all registers, X, Z (pw_decode) and the whole 64 KB of work RAM
(player struct, password, bit buffer, stack frame) must match.

Valid passwords for the decoder are made by the ROM's own encoder, never by
the C code under test.

Usage: python3 test_password.py <rom.bin> <libg4.so> [trials]
Needs: pip install unicorn
"""
import ctypes, random, struct, sys
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL
from unicorn.m68k_const import *

ROM = open(sys.argv[1], "rb").read()
lib = ctypes.CDLL(sys.argv[2])
TRIALS = int(sys.argv[3]) if len(sys.argv) > 3 else 600

class M68K(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8),
                ("x", ctypes.c_uint32), ("z", ctypes.c_uint32),
                ("sr", ctypes.c_uint32), ("usp", ctypes.c_uint32),
                ("n", ctypes.c_uint32), ("v", ctypes.c_uint32), ("cf", ctypes.c_uint32)]

MEM = (ctypes.c_uint8 * 0x1000000)()
MEM[0:len(ROM)] = list(ROM)
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
FUNCS = {}
for name, addr, rt in (("g4_pw_encode", 0x12986, None), ("g4_pw_decode", 0x12C08, ctypes.c_int),
                       ("g4_split_tri", 0x12DE2, None), ("g4_join_tri", 0x12E0E, ctypes.c_uint32)):
    f = getattr(lib, name); f.restype = rt; f.argtypes = [ctypes.POINTER(M68K)]
    FUNCS[addr] = (name, f)

RAM, P0, BUF, SP = 0xFF0000, 0xFF0040, 0xFFF100, 0xFFFD00
SENTINEL, PROBE, PROBE_END = 0xE00000, 0xE00100, 0xE00006
DREG = [UC_M68K_REG_D0 + i for i in range(8)]
AREG = [UC_M68K_REG_A0 + i for i in range(8)]
X_BIT, Z_BIT = 0x10, 0x04

uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
try: uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
except Exception: pass
uc.mem_map(0, 0x100000, UC_PROT_ALL); uc.mem_write(0, ROM)
uc.mem_map(RAM, 0x10000, UC_PROT_ALL)
# Flag probe: the routine's rts lands here and the CPU itself stores SR.
# (Reading SR through the Unicorn API can return stale, lazily-computed flags.)
uc.mem_map(0xE00000, 0x1000, UC_PROT_ALL)
uc.mem_write(SENTINEL, bytes.fromhex('40f900e00100'))      # move.w sr,($e00100).l

def rom_run(entry, d, a, x, ram):
    """Run the real ROM routine; return (D, A, X, Z, RAM)."""
    uc.mem_write(RAM, ram)
    uc.mem_write(SP, struct.pack(">I", SENTINEL))
    uc.reg_write(UC_M68K_REG_SR, 0x2700 | (X_BIT if x else 0))   # SR first (can swap A7)
    for i in range(8): uc.reg_write(DREG[i], d[i])
    for i in range(7): uc.reg_write(AREG[i], a[i])
    uc.reg_write(UC_M68K_REG_A7, SP)
    uc.emu_start(entry, PROBE_END, count=50_000_000)
    sr = struct.unpack('>H', bytes(uc.mem_read(PROBE, 2)))[0]
    return ([uc.reg_read(r) & 0xFFFFFFFF for r in DREG], [uc.reg_read(r) & 0xFFFFFFFF for r in AREG],
            int(bool(sr & X_BIT)), int(bool(sr & Z_BIT)), bytes(uc.mem_read(RAM, 0x10000)))

def c_run(entry, d, a, x, ram):
    ctypes.memmove(ctypes.addressof(MEM) + RAM, ram, 0x10000)
    for i, b in enumerate(struct.pack(">I", SENTINEL)): MEM[SP + i] = b
    m = M68K(); m.d[:] = d; m.a[:7] = a; m.a[7] = SP; m.x = x; m.z = 0
    ret = FUNCS[entry][1](ctypes.byref(m))
    m.a[7] += 4                                         # the routine's rts
    return (list(m.d), list(m.a), m.x, m.z, bytes(MEM[RAM:RAM + 0x10000])), ret

rng = random.Random(1993)
def rnd_regs():
    d = [rng.getrandbits(32) for _ in range(8)]
    a = [rng.getrandbits(32) & 0xFFFFFF for _ in range(7)]
    a[0], a[4] = P0, BUF
    return d, a

def put(ram, off, size, v):
    ram[off - RAM:off - RAM + size] = (v & ((1 << (8 * size)) - 1)).to_bytes(size, "big")

def make_player(ram, in_range):
    """Fill the player struct: game-like values (in_range) or pure noise."""
    ram[P0 - RAM:P0 - RAM + 0x2C4] = bytes(rng.getrandbits(8) for _ in range(0x2C4))
    if in_range:
        r = rng.randint
        put(ram, P0 + 0x134, 4, r(2000, 99999))
        for o in range(0x96, 0xA2, 2): put(ram, P0 + o, 2, r(0, 1))
        put(ram, P0 + 0x16D, 1, r(0, 15))
        put(ram, P0 + 0x124, 4, r(0, 0xFFFF)); put(ram, P0 + 0x128, 4, r(0, 0xFFFF))
        for o in range(0x2A0, 0x2AC, 2): put(ram, P0 + o, 2, r(0, 7))
        put(ram, P0 + 0x298, 2, r(0, 7)); put(ram, P0 + 0x29A, 2, r(0, 3))
        put(ram, P0 + 0x29C, 2, r(0, 3)); put(ram, P0 + 0x29E, 2, r(0, 1))
        put(ram, P0 + 0x166, 1, r(0, 63))
        row = r(0, 12); put(ram, P0 + 0xCC, 2, row); put(ram, P0 + 0xCE, 2, r(0, 12 - row))
        put(ram, P0 + 0x1A2, 1, r(0, 7))
        put(ram, P0 + 0x20C, 4, r(0, 7000 * 60)); put(ram, P0 + 0x1FC, 4, r(0, 130000))
        put(ram, P0 + 0x200, 4, r(0, 200)); put(ram, P0 + 0xF0, 2, r(0, 3))
        put(ram, P0 + 0xB4, 2, r(0, 3)); put(ram, P0 + 0x14A, 2, r(0, 40))
    # name / key at +0x16f (16 bytes): usually a NUL-terminated name, sometimes odd
    kind = rng.random()
    if kind < 0.75:
        name = bytes(rng.choice(b"ABCDEFGHIJKLMNOPQRSTUVWXYZ ") for _ in range(rng.randint(1, 10)))
        key = name + b"\0" + bytes(rng.getrandbits(8) for _ in range(15 - len(name)))
    elif kind < 0.85:
        key = b"\0" + bytes(rng.getrandbits(8) for _ in range(15))          # empty name
    else:
        key = bytes(rng.randint(1, 255) for _ in range(16))                 # no NUL: runs into +0x17f
    ram[P0 + 0x16F - RAM:P0 + 0x17F - RAM] = key

def base_ram():
    ram = bytearray(rng.getrandbits(8) for _ in range(0x10000))
    ram[SP - RAM - 0x200:SP - RAM + 0x100] = bytes(0x300)                   # stack area
    return ram

cases = []                 # (entry, d, a, x, ram, label)
ALPHA = b"0123456789ACDEFGHJKLMPRTUWXY+-=:"
for t in range(TRIALS):
    # --- encoder
    ram = base_ram(); make_player(ram, in_range=rng.random() < 0.7)
    d, a = rnd_regs()
    cases.append((0x12986, d, a, rng.randrange(2), bytes(ram), "encode"))

    # --- decoder fed a password made by the ROM encoder
    ram = base_ram(); make_player(ram, in_range=True)
    d, a = rnd_regs()
    enc = rom_run(0x12986, d, a, 0, bytes(ram))[4]           # ROM encoder writes +0x17f
    ram = bytearray(enc)
    pw = bytes(ram[P0 + 0x17F - RAM:P0 + 0x19D - RAM])
    keep = {(0x16F, 0x19E)}
    for off in range(0, 0x2C4):                              # re-randomise everything else in the struct
        if not (0x16F <= off < 0x19E): ram[P0 + off - RAM] = rng.getrandbits(8)
    put(ram, P0 + 0xB4, 2, rng.randint(0, 3))
    mut = rng.random()
    label = "decode-valid"
    if mut < 0.15:                                           # one character changed
        i = rng.randrange(30); ram[P0 + 0x17F + i - RAM] = rng.choice([c for c in ALPHA if c != pw[i]]); label = "decode-1char"
    elif mut < 0.25:                                         # different name
        ram[P0 + 0x16F - RAM] = (ram[P0 + 0x16F - RAM] % 26) + 65 if ram[P0 + 0x16F - RAM] != 65 else 66; label = "decode-name"
    d, a = rnd_regs()
    cases.append((0x12C08, d, a, rng.randrange(2), bytes(ram), label))

    # --- decoder fed junk
    ram = base_ram(); make_player(ram, in_range=False)
    if rng.random() < 0.5:
        ram[P0 + 0x17F - RAM:P0 + 0x19D - RAM] = bytes(rng.choice(ALPHA) for _ in range(30))
    d, a = rnd_regs()
    cases.append((0x12C08, d, a, rng.randrange(2), bytes(ram), "decode-junk"))

# split_tri / join_tri directly, including out-of-range values
for v in list(range(-3, 95)) + [0x7FFF, 0x8000, 0xFFFF]:
    ram = base_ram(); d, a = rnd_regs(); d[0] = (d[0] & 0xFFFF0000) | (v & 0xFFFF)
    cases.append((0x12DE2, d, a, rng.randrange(2), bytes(ram), "split_tri"))
for _ in range(200):
    ram = base_ram()
    put(ram, P0 + 0xCC, 2, rng.choice([rng.randint(0, 12), rng.getrandbits(16)]))
    put(ram, P0 + 0xCE, 2, rng.choice([rng.randint(0, 12), rng.getrandbits(16)]))
    d, a = rnd_regs()
    cases.append((0x12E0E, d, a, rng.randrange(2), bytes(ram), "join_tri"))

from collections import Counter
fails, stats = 0, Counter()
for entry, d, a, x, ram, label in cases:
    ref = rom_run(entry, d, a, x, ram)
    got, ret = c_run(entry, d, a, x, ram)
    name = FUNCS[entry][0]
    want_z = entry == 0x12C08
    ok = (ref[0] == got[0] and ref[1] == got[1] and ref[2] == got[2] and ref[4] == got[4]
          and (not want_z or (ref[3] == got[3] and ret == got[3]))
          and (entry != 0x12E0E or ret == got[0][0]))
    if entry == 0x12C08: stats[f"{label}: {'accepted' if ref[3] else 'rejected'}"] += 1
    if not ok:
        fails += 1
        if fails <= 5:
            print(f"MISMATCH {name} ({label})")
            for nm, r, g in (("D", ref[0], got[0]), ("A", ref[1], got[1])):
                for i, (p, q) in enumerate(zip(r, g)):
                    if p != q: print(f"   {nm}{i}: rom {p:#010x}  c {q:#010x}")
            if ref[2] != got[2]: print(f"   X: rom {ref[2]}  c {got[2]}")
            if want_z and ref[3] != got[3]: print(f"   Z: rom {ref[3]}  c {got[3]}")
            diff = [i for i in range(0x10000) if ref[4][i] != got[4][i]]
            if diff: print(f"   RAM differs at {len(diff)} bytes, first {RAM + diff[0]:#x}")

print("decoder outcomes (from the ROM):", dict(sorted(stats.items())))
by = Counter(FUNCS[c[0]][0][3:] for c in cases)
print(f"{len(cases)} cases ({', '.join(f'{v} {k}' for k, v in by.items())}): {len(cases)-fails} match, {fails} mismatch")
sys.exit(1 if fails else 0)
