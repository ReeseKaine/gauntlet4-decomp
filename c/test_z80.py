"""
Differential test: gen_z80/g4_z80.c (the Z80 sample player in C) vs. the
original Z80 code in the z80 emulator (pip install z80).

Each run starts from reset with the uploaded Z80 RAM image plus random
mailbox state: a sample command at $010A (mostly real directory entries),
a queued command at $010B, the playing priority $02E1, pan $0114 and timer
bits $0115, and random registers. YM2612 status reads get the same scripted
values on both sides (timer A overflow random, busy sometimes). The bank
window reads 68000 memory (the ROM) at bank << 15 on both sides. Both run
exactly K instructions (K = 50..20000; the C build counts them with
-DG4_Z80_COUNT); then every register (shadow set too),
all of Z80 RAM, and the log of YM reads/writes and bank writes must match.

Build: gcc -std=c99 -O1 -DG4_Z80_COUNT -shared -fPIC -o libz80.so gen_z80/g4_z80.c z80_shim.c
Usage: python3 test_z80.py <rom.bin> <libz80.so> [runs]
"""
import ctypes, os, random, sys
import z80

ROM = open(sys.argv[1], "rb").read()
lib = ctypes.CDLL(os.path.abspath(sys.argv[2]))
RUNS = int(sys.argv[3]) if len(sys.argv) > 3 else 500
IMAGE = ROM[0x3D470:0x3D470 + 0x2000]

class Z80(ctypes.Structure):
    _fields_ = [(n, ctypes.c_uint8) for n in ("a", "f", "b", "c", "d", "e", "h", "l",
                                              "a_", "f_", "b_", "c_", "d_", "e_", "h_", "l_")] + \
               [("ix", ctypes.c_uint16), ("iy", ctypes.c_uint16), ("sp", ctypes.c_uint16),
                ("iff", ctypes.c_uint8), ("im", ctypes.c_uint8), ("pc", ctypes.c_uint16), ("budget", ctypes.c_int32)]
YMR = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int)
YMW = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.c_int)
BNK = ctypes.CFUNCTYPE(None, ctypes.c_int)
lib.z80_test_run.argtypes = [ctypes.POINTER(Z80), YMR, YMW, BNK, ctypes.c_long]
MEM = (ctypes.c_uint8 * 0x1000000)(); ctypes.memmove(MEM, ROM, len(ROM))
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
CRAM = (ctypes.c_uint8 * 0x2000).in_dll(lib, "g4z80_ram")
CBANK = ctypes.c_uint16.in_dll(lib, "g4z80_bank")

def header(k):
    p = IMAGE[0x2F2 + 2 * k] | IMAGE[0x2F3 + 2 * k] << 8
    return p if 0 < p < 0x1FF8 else None
# the game's samples (all mode 3, the 4-bit DPCM player); slots after the directory hold other data,
# some of which reads as pointers into the code -- never commands the 68000 sends
DIRCMDS = []
for k in range(0x20, 0x100):                       # the directory proper: entry n -> header $0444 + 8n, in order
    if header(k) != 0x0444 + 8 * (k - 0x20): break
    DIRCMDS.append(k)

class Script:
    def __init__(self, seed, writes):
        self.r = random.Random(seed); self.left = writes; self.log = []
    def ym_read(self, port):
        v = (0x80 if self.r.random() < 0.1 else 0) | (1 if self.r.random() < 0.5 else 0)
        self.log.append(("r", port, v)); return v
    def ym_write(self, port, v):
        self.log.append(("w", port, v)); return 0
    def bank(self, v):
        self.log.append(("b", v & 1))

def start_state(seed):
    r = random.Random(seed)
    ram = bytearray(IMAGE)
    if r.random() < 0.9:
        k = r.choice(DIRCMDS); ram[0x10A] = k
        if r.random() < 0.5:                                  # the other playback routines (PCM, 2x, interpolated):
            ram[header(k) + 1] = r.randrange(4)                # in the program, unused by the game's samples
        if r.random() < 0.4:                                  # a short sample, so it ends inside the run
            n = r.randint(1, 40); ram[header(k) + 6] = n & 0xFF; ram[header(k) + 7] = n >> 8
    else: ram[0x10A] = 0
    if r.random() < 0.4: ram[0x10B:0x10E] = bytes([r.choice(DIRCMDS), r.randrange(256), r.randrange(256)])
    ram[0x2E1] = r.choice([0, 0, 1, 5, 0x80, 0xFF, r.randrange(256)])
    ram[0x114] = r.choice([0xC0, 0x80, 0x40, r.randrange(256)])
    ram[0x115] = r.randrange(256)
    regs = {n: r.randrange(256) for n in ("a", "f", "b", "c", "d", "e", "h", "l", "a_", "f_", "b_", "c_", "d_", "e_", "h_", "l_")}
    regs.update(ix=r.randrange(0x10000), iy=r.randrange(0x10000), sp=r.randrange(0x10000))
    return ram, regs, r.randrange(0x200), r.choice([r.randint(50, 500), r.randint(500, 5000), r.randint(5000, 20000)])

COVERED = set()

def step(m):
    """One whole instruction: the emulator stops after a lone DD/FD prefix byte, so continue then."""
    pc = m.pc; COVERED.add(pc)
    m.ticks_to_stop = 1; m.run()
    while m.memory[pc] in (0xDD, 0xFD) and m.pc == (pc + 1) & 0xFFFF:
        pc = m.pc; m.ticks_to_stop = 1; m.run()

def run_emulator(seed, ram, regs, bank0, writes):
    s = Script(seed, writes); m = z80.Z80Machine()
    m.set_memory_block(0, bytes(ram))
    for n, v in regs.items():
        if n.endswith("_"): continue
        setattr(m, n, v)
    m.alt_af = (regs["a_"] << 8) | regs["f_"]; m.alt_bc = (regs["b_"] << 8) | regs["c_"]
    m.alt_de = (regs["d_"] << 8) | regs["e_"]; m.alt_hl = (regs["h_"] << 8) | regs["l_"]
    m.pc = 0
    st = {"bank": bank0, "stop": False}
    m.mark_addrs(0x2000, 0x2000, m.READ_MARK | m.WRITE_MARK)    # Z80 RAM mirror
    m.mark_addrs(0x4000, 0x2000, m.READ_MARK | m.WRITE_MARK)    # YM2612 (mirrored)
    m.mark_addrs(0x6000, 0x100, m.WRITE_MARK)                    # bank register
    m.mark_addrs(0x8000, 0x8000, m.READ_MARK | m.WRITE_MARK)    # bank window
    def rd(a):
        if 0x2000 <= a < 0x4000: return m.memory[a & 0x1FFF]          # RAM mirror
        if 0x4000 <= a < 0x6000: return s.ym_read(a & 3)
        if a >= 0x8000:
            x = (st["bank"] << 15) | (a & 0x7FFF)
            return ROM[x] if x < len(ROM) else 0
        return 0xFF
    def wr(a, v):
        if 0x2000 <= a < 0x4000: m.memory[a & 0x1FFF] = v; return
        if 0x4000 <= a < 0x6000:
            if s.ym_write(a & 3, v): st["stop"] = True
        elif 0x6000 <= a < 0x6100:
            st["bank"] = ((st["bank"] >> 1) | ((v & 1) << 8)) & 0x1FF; s.bank(v)
    m.set_read_callback(rd); m.set_write_callback(wr)
    steps = 0
    while steps < writes:                                    # exactly `writes` instructions
        step(m); steps += 1
    st["stop"] = True
    regs_out = {n: getattr(m, n) for n in ("a", "f", "b", "c", "d", "e", "h", "l", "ix", "iy", "sp")}
    for hi, lo, pair in (("a_", "f_", "alt_af"), ("b_", "c_", "alt_bc"), ("d_", "e_", "alt_de"), ("h_", "l_", "alt_hl")):
        w = getattr(m, pair); regs_out[hi] = w >> 8; regs_out[lo] = w & 0xFF
    return st["stop"], regs_out, bytes(m.memory[0:0x2000]), s.log, st["bank"]

def run_c(seed, ram, regs, bank0, writes):
    s = Script(seed, writes)
    ctypes.memmove(CRAM, bytes(ram), 0x2000); CBANK.value = bank0
    z = Z80(**{n: v for n, v in regs.items()})
    cbs = (YMR(s.ym_read), YMW(s.ym_write), BNK(s.bank))
    res = lib.z80_test_run(ctypes.byref(z), *cbs, writes)
    regs_out = {n: getattr(z, n) for n in ("a", "f", "b", "c", "d", "e", "h", "l", "ix", "iy", "sp",
                                           "a_", "f_", "b_", "c_", "d_", "e_", "h_", "l_")}
    return res, regs_out, bytes(CRAM), s.log, CBANK.value

TRACE = int(os.environ["G4Z80TRACE"]) if os.environ.get("G4Z80TRACE") else None
if TRACE is not None:
    TR = ctypes.CFUNCTYPE(None, ctypes.POINTER(Z80), ctypes.c_int)
    CTRACE = []
    _NAMES = ("a", "f", "b", "c", "d", "e", "h", "l", "a_", "f_", "b_", "c_", "d_", "e_", "h_", "l_")
    _ctr = TR(lambda zp, pc: CTRACE.append((pc, tuple(getattr(zp.contents, n) for n in _NAMES))))
    lib.z80_set_trace.argtypes = [TR]; lib.z80_set_trace(_ctr)
    ram, regs, bank0, k = start_state(TRACE)
    ETRACE = []
    _orig = z80.Z80Machine.run
    m_steps = []
    def run_emulator_traced(seed, ram, regs, bank0, writes):
        global ETRACE
        res = None
        return res
    # trace the emulator by stepping and recording before each instruction
    s = Script(TRACE, k); m = z80.Z80Machine(); m.set_memory_block(0, bytes(ram))
    for n, v in regs.items():
        if not n.endswith("_"): setattr(m, n, v)
    m.alt_af = (regs["a_"] << 8) | regs["f_"]; m.alt_bc = (regs["b_"] << 8) | regs["c_"]
    m.alt_de = (regs["d_"] << 8) | regs["e_"]; m.alt_hl = (regs["h_"] << 8) | regs["l_"]; m.pc = 0
    st = {"bank": bank0}
    m.mark_addrs(0x2000, 0x2000, m.READ_MARK | m.WRITE_MARK)
    m.mark_addrs(0x4000, 0x2000, m.READ_MARK | m.WRITE_MARK); m.mark_addrs(0x6000, 0x100, m.WRITE_MARK)
    m.mark_addrs(0x8000, 0x8000, m.READ_MARK | m.WRITE_MARK)
    def rd(a):
        if 0x2000 <= a < 0x4000: return m.memory[a & 0x1FFF]
        if 0x4000 <= a < 0x6000: return s.ym_read(a & 3)
        if a >= 0x8000:
            x = (st["bank"] << 15) | (a & 0x7FFF); return ROM[x] if x < len(ROM) else 0
        return 0xFF
    def wr(a, v):
        if 0x2000 <= a < 0x4000: m.memory[a & 0x1FFF] = v; return
        if 0x4000 <= a < 0x6000: s.ym_write(a & 3, v)
        elif 0x6000 <= a < 0x6100: st["bank"] = ((st["bank"] >> 1) | ((v & 1) << 8)) & 0x1FF
    m.set_read_callback(rd); m.set_write_callback(wr)
    for _ in range(k):
        ETRACE.append((m.pc, tuple(getattr(m, n) for n in ("a", "f", "b", "c", "d", "e", "h", "l"))
                       + (m.alt_af >> 8, m.alt_af & 0xFF, m.alt_bc >> 8, m.alt_bc & 0xFF, m.alt_de >> 8, m.alt_de & 0xFF, m.alt_hl >> 8, m.alt_hl & 0xFF)))
        step(m)
    run_c(TRACE, ram, regs, bank0, k)
    j = next((i for i, (x, y) in enumerate(zip(ETRACE, CTRACE)) if x != y), None)
    print("first difference at step", j)
    for i in range(max(0, (j or 0) - 4), (j or 0) + 2):
        print(f"  {i:5d} Z80 {ETRACE[i][0]:04x} {[hex(v) for v in ETRACE[i][1]]}")
        print(f"        C   {CTRACE[i][0]:04x} {[hex(v) for v in CTRACE[i][1]]}")
    sys.exit()

ok = True; fails = []; stopped = 0; samples = set()
for seed in range(RUNS):
    ram, regs, bank0, writes = start_state(seed)
    E = run_emulator(seed, ram, regs, bank0, writes)
    if not E[0]: continue                                    # idle (no sample to play): nothing to compare,
                                                             # and the C side would loop forever
    C = run_c(seed, ram, regs, bank0, writes)
    stopped += 1
    if C[0] != 1: fails.append((seed, f"C stopped at an untranslatable jump to {-1 - C[0]:#06x}")); continue
    why = []
    for n in E[1]:
        if E[1][n] != C[1][n]: why.append(f"{n}: Z80 {E[1][n]:#x} C {C[1][n]:#x}")
    if E[2] != C[2]:
        k = next(i for i in range(0x2000) if E[2][i] != C[2][i]); why.append(f"RAM {k:#06x}: Z80 {E[2][k]:#x} C {C[2][k]:#x}")
    if E[3] != C[3]:
        k = next((i for i, (x, y) in enumerate(zip(E[3], C[3])) if x != y), min(len(E[3]), len(C[3])))
        why.append(f"io #{k}: Z80 {E[3][k] if k < len(E[3]) else None} C {C[3][k] if k < len(C[3]) else None}")
    if E[4] != C[4]: why.append(f"bank Z80 {E[4]:#x} C {C[4]:#x}")
    if why: fails.append((seed, "; ".join(why)))
    if ram[0x10A] in DIRCMDS: samples.add(ram[0x10A])
modes = {}
print(f"{RUNS} runs compared, {len(samples)} of the {len(DIRCMDS)} samples started")
for seed, why in fails[:6]: print(f"FAIL seed {seed}: {why}")
import re as _re
ALL = {int(a, 16) for a in _re.findall(r"/\* ([0-9a-f]{4})  ", open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "gen_z80", "g4_z80.c")).read())}
miss = sorted(ALL - COVERED)
print(f"instruction coverage: {len(ALL) - len(miss)}/{len(ALL)}" + (f"; never run: {[hex(a) for a in miss]}" if miss else ""))
print("ALL PASS" if not fails else f"{len(fails)} FAILED")
