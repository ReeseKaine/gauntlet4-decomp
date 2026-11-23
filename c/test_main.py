"""
Differential test: g4_main.c (reset, main loop, level loop, game setup) and
g4_levelend.c (the per-mode after-level handlers) vs.
the real ROM routines in the Unicorn 68000 emulator.

Each routine is tested in isolation: every call it makes goes to a scripted
stand-in instead of the real callee, on both sides (C: g4_external; ROM: a
code hook on the callee's entry that runs the same script and then returns).
At every such call the script, driven by an RNG seeded identically for both
sides, randomly rewrites the RAM variables the routine branches on, D0/D1,
and the Z flag it returns, so the control flow is pushed down every path.

Compared: the sequence of calls (target, return address, D0-D7, A0-A7, SR
system byte, all 64 KB of work RAM), the sequence of hardware accesses
(address, size, value, read/write), and after a normal return all registers,
X and RAM. The main loop never returns; the script stops it after N calls.

Build: gcc -std=c99 -O1 -Wall -Wextra -pedantic -shared -fPIC -o libg4.so \
           g4_bits.c g4_password.c g4_password_b.c g4_dispatch.c g4_main.c g4_level.c \
           g4_levelend.c test_shim.c
Usage: python3 test_main.py <rom.bin> <libg4.so> [runs-per-routine]
Needs: pip install unicorn capstone   (capstone only for the coverage report)
"""
import ctypes, hashlib, random, re, struct, sys, os
from unicorn import Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL, UC_HOOK_CODE
from unicorn.m68k_const import *

ROM = open(sys.argv[1], "rb").read()
lib = ctypes.CDLL(os.path.abspath(sys.argv[2]))
RUNS = int(sys.argv[3]) if len(sys.argv) > 3 else 150
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = "".join(open(os.path.join(HERE, f)).read() for f in ("g4_main.c", "g4_levelend.c"))

class M68K(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8),
                ("x", ctypes.c_uint32), ("z", ctypes.c_uint32),
                ("sr", ctypes.c_uint32), ("usp", ctypes.c_uint32),
                ("n", ctypes.c_uint32), ("v", ctypes.c_uint32), ("cf", ctypes.c_uint32)]

EXT_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(M68K), ctypes.c_uint32)
RD_CB = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
WR_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
lib.g4_test_run.argtypes = [ctypes.POINTER(M68K), ctypes.c_uint32]
lib.g4_test_set.argtypes = [EXT_CB, RD_CB, WR_CB]

MEM = (ctypes.c_uint8 * 0x1000000)()
ctypes.memmove(MEM, ROM, len(ROM))
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
ctypes.c_int.in_dll(lib, "g4_use_translated").value = 0      # isolate: every call is external

RAM, SENTINEL, PROBE, PROBE_END = 0xFF0000, 0xE00000, 0xE00100, 0xE00006
SP0 = 0xFFFD00
DREG = [UC_M68K_REG_D0 + i for i in range(8)]
AREG = [UC_M68K_REG_A0 + i for i in range(8)]

# Call targets and RAM variables the translated code uses (read from the source)
TARGETS = {int(t, 16) for t in re.findall(r"JSR\(c, [^,]+, 0x([0-9A-F]+)\)", SRC)}   # any return-address expression
TARGETS |= {struct.unpack(">I", ROM[t + 4 * i:t + 4 * i + 4])[0] for t in (0xC16, 0xEF6) for i in range(4)}
TARGETS |= {struct.unpack(">I", ROM[0x1A06 + 4 * i:0x1A0A + 4 * i])[0] for i in range(2)}   # quest_next table
VARS = sorted({int(v, 16) for v in re.findall(r"0x(FF[0-9A-F]{4})\b", SRC)} - {0xFF0040, 0xFF0B50})

# ------------------------------------------------------------------ scenario
class Script:
    """Behaviour of every external call and hardware read, for one side.
    Both sides get the same seed, so as long as they make the same calls in
    the same order they see the same answers."""
    def __init__(self, seed, kind, max_calls):
        self.r, self.kind, self.n, self.max = random.Random(seed), kind, 0, max_calls
        self.busy = self.r.randint(0, 3)              # polls before the Z80 grants the bus
        self.cold = self.kind != "soft"
        self.ver = self.r.choice([0xA0, 0xA1, 0x80, 0x21])
    def io_read(self, addr, size):
        if addr in (0xA10008, 0xA1000A, 0xA1000C):
            return 0 if self.cold else (0x0100 if addr == 0xA1000C else 0)
        if addr == 0xA10000: return self.ver if size == 1 else self.ver
        if addr == 0xA10001: return self.ver
        if addr == 0xA11100:
            if self.busy: self.busy -= 1; return 1
            return 0
        if addr == 0xC00004: return 0x3400
        return self.r.randrange(1 << (8 * size))
    def ext(self, mem, regs, target):
        """mem: (read(addr,n), write(addr,bytes)); regs: dict d0, d1 (may be changed).
        Returns (z, stop)."""
        r = self.r; self.n += 1
        for _ in range(r.randint(0, 6)):
            v = r.choice(VARS)
            if v == 0xFF0B60:                          # player count - 1: always a whole, sane word
                mem[1](v, struct.pack(">H", r.choice([0xFFFF, 0, 0, 1, 2, 3]))); continue
            val = rand_val(r)
            if r.random() < 0.5: mem[1](v, bytes([val & 0xFF]))
            else: mem[1](v & ~1, struct.pack(">H", val))
        if target in (0x0041B8, 0x004086):           # return a value in D0
            regs["d0"] = r.choice([0, 1, 2, 3, 4, r.randrange(1 << 32)])
        if target == 0x004086 and r.random() < 0.3:
            regs["d1"] = r.randrange(1 << 32)
        z = r.random() < 0.5
        regs["x"] = r.randint(0, 1)                   # flags after a call are the callee's business
        if self.kind in ("cold", "soft") and target == 0x0008C2 and r.random() < 0.3:
            mem[1](0xFFE792, b"\xff")                 # game over -> back to the title loop
        return z, self.n >= self.max

# ------------------------------------------------------------------ ROM side
RAMBUF = (ctypes.c_uint8 * 0x10000)()
def make_uc():
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    try: uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    except Exception: pass
    uc.mem_map(0, 0x100000, UC_PROT_ALL); uc.mem_write(0, ROM)
    uc.mem_map_ptr(RAM, 0x10000, UC_PROT_ALL, ctypes.addressof(RAMBUF))
    uc.mem_map_ptr(0xFFFF0000, 0x10000, UC_PROT_ALL, ctypes.addressof(RAMBUF))   # 24-bit wrap of -(A6) from 0
    uc.mem_map(0xE00000, 0x1000, UC_PROT_ALL)
    uc.mem_write(SENTINEL, bytes.fromhex("40f900e00100"))  # move.w sr,($e00100).l  (flag probe)
    return uc
UC = make_uc()

def snapshot_regs_uc(uc):
    return [uc.reg_read(r) & 0xFFFFFFFF for r in DREG] + [uc.reg_read(r) & 0xFFFFFFFF for r in AREG]

def rom_run(entry, regs, ram, script):
    uc = UC
    log, io = [], []
    ctypes.memmove(RAMBUF, bytes(ram), 0x10000)
    def rd(uc_, off, size, base):
        a = base + off
        if size == 4:
            hi, lo = script.io_read(a, 2), script.io_read(a + 2, 2)
            io.append(("r", a, 2, hi)); io.append(("r", a + 2, 2, lo)); return (hi << 16) | lo
        v = script.io_read(a, size); io.append(("r", a, size, v)); return v
    def wr(uc_, off, size, value, base):
        a = base + off
        if size == 4:
            io.append(("w", a, 2, (value >> 16) & 0xFFFF)); io.append(("w", a + 2, 2, value & 0xFFFF))
        else: io.append(("w", a, size, value & ((1 << (8 * size)) - 1)))
    for base, size in ((0xA00000, 0x20000), (0xC00000, 0x1000)):
        try: uc.mem_unmap(base, size)
        except UcError: pass
        uc.mmio_map(base, size, rd, base, wr, base)
    hooks = []
    def on_call(uc_, addr, size, _):
        if addr not in TARGETS or addr == entry: return
        sp = uc_.reg_read(UC_M68K_REG_A7)
        ret = struct.unpack(">I", bytes(uc_.mem_read(sp, 4)))[0]
        sr = uc_.reg_read(UC_M68K_REG_SR)
        log.append((addr, ret, tuple(snapshot_regs_uc(uc_)), sr & 0xFF00, hashlib.md5(bytes(RAMBUF)).hexdigest()))
        regs_ = {"d0": uc_.reg_read(UC_M68K_REG_D0) & 0xFFFFFFFF, "d1": uc_.reg_read(UC_M68K_REG_D1) & 0xFFFFFFFF}
        z, stop = script.ext((lambda a, n: bytes(uc_.mem_read(a, n)), lambda a, b: uc_.mem_write(a, b)), regs_, addr)
        uc_.reg_write(UC_M68K_REG_D0, regs_["d0"]); uc_.reg_write(UC_M68K_REG_D1, regs_["d1"])
        uc_.reg_write(UC_M68K_REG_A7, sp + 4)
        uc_.reg_write(UC_M68K_REG_SR, (sr & ~0x14) | (0x04 if z else 0) | (0x10 if regs_["x"] else 0))
        if stop: uc_.emu_stop(); state["stopped"] = True
        else: uc_.reg_write(UC_M68K_REG_PC, ret)
    state = {"stopped": False}
    hooks.append(uc.hook_add(UC_HOOK_CODE, lambda u, a, sz, _: COVERED.add(a), None, 0x400, 0x1F2D))
    for t in TARGETS:
        hooks.append(uc.hook_add(UC_HOOK_CODE, on_call, None, t, t))
    sp = regs["sp"]
    uc.reg_write(UC_M68K_REG_SR, 0x2700 | (0x10 if regs["x"] else 0))
    for i in range(8): uc.reg_write(DREG[i], regs["d"][i])
    for i in range(7): uc.reg_write(AREG[i], regs["a"][i])
    if regs["push_sentinel"]:
        sp -= 4; uc.mem_write(sp, struct.pack(">I", SENTINEL))
    uc.reg_write(UC_M68K_REG_A7, sp)
    try:
        uc.emu_start(entry, PROBE_END, count=20_000_000)
    finally:
        for h in hooks: uc.hook_del(h)
    end = None
    if not state["stopped"]:
        srw = struct.unpack(">H", bytes(uc.mem_read(PROBE, 2)))[0]
        end = (tuple(snapshot_regs_uc(uc)), (srw >> 4) & 1)
    return log, io, end, bytes(RAMBUF)

# ------------------------------------------------------------------ C side
def c_run(entry, regs, ram, script):
    log, io = [], []
    ctypes.memmove(ctypes.addressof(MEM) + RAM, bytes(ram), 0x10000)
    cpu = M68K()
    for i in range(8): cpu.d[i] = regs["d"][i]
    for i in range(7): cpu.a[i] = regs["a"][i]
    cpu.a[7] = regs["sp"]; cpu.x = regs["x"]; cpu.sr = 0x2700
    def ram_now(): return ctypes.string_at(ctypes.addressof(MEM) + RAM, 0x10000)
    def ext(cp, target):
        c = cp.contents
        sp = c.a[7] & 0xFFFFFF
        ret = struct.unpack(">I", ctypes.string_at(ctypes.addressof(MEM) + sp, 4))[0]
        log.append((target, ret, tuple(list(c.d) + list(c.a)), c.sr & 0xFF00, hashlib.md5(ram_now()).hexdigest()))
        regs_ = {"d0": c.d[0], "d1": c.d[1]}
        z, stop = script.ext((lambda a, n: ctypes.string_at(ctypes.addressof(MEM) + (a & 0xFFFFFF), n),
                              lambda a, b: ctypes.memmove(ctypes.addressof(MEM) + (a & 0xFFFFFF), b, len(b))), regs_, target)
        c.d[0], c.d[1] = regs_["d0"], regs_["d1"]
        return (1 if z else 0) | (2 if stop else 0) | (4 if regs_["x"] else 0)
    def rd(a, size):
        v = script.io_read(a, size); io.append(("r", a, size, v)); return v
    def wr(a, v, size):
        io.append(("w", a, size, v & ((1 << (8 * size)) - 1)))
    cbs = (EXT_CB(ext), RD_CB(rd), WR_CB(wr))
    lib.g4_test_set(*cbs)
    if not regs["push_sentinel"]:
        raise ValueError("C runner always pushes the sentinel")
    res = lib.g4_test_run(ctypes.byref(cpu), entry)
    assert res >= 0, hex(entry)
    end = None if res == 1 else (tuple(list(cpu.d) + list(cpu.a)), cpu.x)
    return log, io, end, ram_now()

# ------------------------------------------------------------------ cases
def rand_val(r):
    return r.choice([0, 0, 0, 1, 2, 3, 0xB, 0x50, 0xFF, 0xFFFF, r.randrange(0x10000)])

def rand_ram(r, kind):
    ram = bytearray(r.randbytes(0x10000))
    for v in VARS:                                                # flags mostly 0 / small, as in the game
        if r.random() < 0.5: ram[v - RAM] = rand_val(r) & 0xFF
        else: ram[(v & ~1) - RAM:(v & ~1) - RAM + 2] = struct.pack(">H", rand_val(r))
    # 4 valid player pointers and a sane player count for start_skip
    for i in range(4):
        ram[0x0B50 + 4 * i:0x0B54 + 4 * i] = struct.pack(">I", 0xFF0040 + 0x2C4 * i)
    ram[0x0B60:0x0B62] = struct.pack(">H", r.randint(0, 3))
    ram[0xE838:0xE83A] = struct.pack(">H", r.randint(0, 3))       # valid mode for the jump tables
    ram[0xE842:0xE844] = struct.pack(">H", r.randint(0, 1))       # Quest hub flag: index into the 2-entry table 0x1A06
    for i in range(4):                                            # player flags: mostly "present, alive"
        p = 0x0040 + 0x2C4 * i
        for off in (0x60, 0x61, 0x167): ram[p + off] = r.choice([0, 0, 0, 0xFF])
        ram[p + 0x5A:p + 0x5C] = struct.pack(">H", r.choice([0, 0, 1]))
    if r.random() < 0.3: ram[0xE84E:0xE850] = struct.pack(">H", 0x1F)          # hub exit row
    if r.random() < 0.3:                                          # level number at the 0x72 wrap
        ram[0xE758:0xE75A] = struct.pack(">H", r.choice([0x71, 0x72]))
        ram[0xE75A:0xE75C] = struct.pack(">H", r.choice([0, 1]))
    ram[0xE776:0xE778] = struct.pack(">H", r.randint(0, 7))       # index into the 8-entry table at 0x2667e
    return ram

def one(entry, seed, kind, max_calls):
    r = random.Random(seed)
    ram = rand_ram(r, kind)
    regs = {"d": [r.randrange(1 << 32) for _ in range(8)], "a": [r.randrange(1 << 32) for _ in range(7)],
            "sp": SP0, "x": r.randint(0, 1), "push_sentinel": True}
    rom = rom_run(entry, regs, ram, Script(seed, kind, max_calls))
    cc = c_run(entry, regs, ram, Script(seed, kind, max_calls))
    problems = []
    if rom[0] != cc[0]:
        k = next((i for i, (x, y) in enumerate(zip(rom[0], cc[0])) if x != y), min(len(rom[0]), len(cc[0])))
        problems.append(f"call #{k}: ROM {fmt(rom[0], k)}  C {fmt(cc[0], k)}")
    if rom[1] != cc[1]:
        k = next((i for i, (x, y) in enumerate(zip(rom[1], cc[1])) if x != y), min(len(rom[1]), len(cc[1])))
        problems.append(f"io #{k}: ROM {rom[1][k] if k < len(rom[1]) else None}  C {cc[1][k] if k < len(cc[1]) else None}")
    if rom[2] != cc[2]: problems.append(f"end state: ROM {rom[2]}  C {cc[2]}")
    if rom[3] != cc[3]:
        k = next(i for i in range(0x10000) if rom[3][i] != cc[3][i])
        problems.append(f"RAM differs first at {RAM + k:#x}: ROM {rom[3][k]:#x} C {cc[3][k]:#x}")
    return problems, len(rom[0]), len(rom[1]), rom[2] is not None

def fmt(log, k):
    if k >= len(log): return "<none>"
    t, ret, regs, sr, h = log[k]
    return f"{t:#x} ret={ret:#x} D={[hex(x) for x in regs[:8]]} A={[hex(x) for x in regs[8:]]} sr={sr:#x} ram={h[:8]}"

CASES = [  # (address, name, scenario kind, calls before stop)
    (0x000400, "entry (cold boot)", "cold", 60),
    (0x000400, "entry (soft reset)", "soft", 40),
    (0x000584, "game_init", "plain", 10_000),
    (0x0008C2, "play_level", "plain", 400),
    (0x000A58, "level_hud_a58", "plain", 10_000),
    (0x000ADE, "start_skip", "plain", 10_000),
    (0x000B1E, "level_hud_b1e", "plain", 10_000),
    (0x000B5A, "game_setup", "plain", 10_000),
    (0x000C26, "setup_arcade", "plain", 10_000),
    (0x000C92, "setup_quest", "plain", 10_000),
    (0x000D3A, "setup_battle", "plain", 10_000),
    (0x000DD8, "setup_record", "plain", 10_000),
    (0x000E1C, "set_flags_e79c", "plain", 10_000),
    (0x000EA2, "hiscore_init", "plain", 10_000),
    (0x000EDC, "level_end", "plain", 10_000),
    (0x000F06, "arcade_next", "plain", 10_000),
    (0x00118C, "arcade_speed", "plain", 10_000),
    (0x0019EC, "quest_next", "plain", 10_000),
    (0x001A0E, "quest_from_hub", "plain", 10_000),
    (0x001B2E, "quest_from_stage", "plain", 10_000),
    (0x001C6E, "quest_tally", "plain", 10_000),
    (0x001ED6, "battle_next", "plain", 10_000),
    (0x001F22, "record_next", "plain", 10_000),
]

COVERED = set()
def insn_addrs():
    """Instruction addresses of the translated routines: linear sweep for the main-loop
    ranges, recursive descent (branches followed, inline tables skipped) for the rest."""
    import re as _re
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    out = set()
    for lo, hi in ((0x400, 0x48E), (0x4FA, 0x580), (0x584, 0x6BE), (0x8C2, 0xC16), (0xC26, 0xEF6)):
        out |= {i.address for i in md.disasm(ROM[lo:hi], lo)}
    for entry in (0xF06, 0x118C, 0x19EC, 0x1A0E, 0x1B2E, 0x1C6E, 0x1ED6, 0x1F22):
        work = [entry]
        while work:
            pc = work.pop()
            while pc not in out:
                i = next(md.disasm(ROM[pc:pc + 12], pc), None)
                if i is None: break
                out.add(pc)
                m = i.mnemonic; t = _re.search(r"\$([0-9a-f]+)$", i.op_str)
                if m in ("rts", "rte") or m.startswith("jmp"): break
                if m.startswith("bra"):
                    if t: work.append(int(t.group(1), 16))
                    break
                if (m.startswith("b") and not m.startswith(("bchg", "bclr", "bset", "btst", "bsr"))) or m.startswith("db"):
                    if t: work.append(int(t.group(1), 16))
                pc += i.size
    return out

ok = True
CHEAP = {0x000A58, 0x000ADE, 0x000B1E, 0x000C26, 0x000C92, 0x000D3A, 0x000DD8, 0x000E1C, 0x000EDC,
         0x000F06, 0x00118C, 0x0019EC, 0x001A0E, 0x001B2E, 0x001C6E, 0x001ED6, 0x001F22}
for addr, name, kind, maxc in CASES:
    calls = ios = returned = 0; bad = []
    n = RUNS * 10 if addr in CHEAP else RUNS      # small routines: more runs to reach rare flag combinations
    for s in range(n):
        probs, nc, ni, ret = one(addr, (addr << 16) ^ s, kind, maxc)
        calls += nc; ios += ni; returned += ret
        if probs: bad.append((s, probs))
    ok &= not bad
    print(f"{'PASS' if not bad else 'FAIL'} {addr:06x} {name:22s} {n} runs, {calls} calls, {ios} I/O accesses, {returned} returned")
    for s, p in bad[:3]:
        print(f"   seed {s}:"); [print("     " + x) for x in p]
try:
    ALL = insn_addrs()
    miss = sorted(ALL - COVERED)
    print(f"\ninstruction coverage (ROM side): {len(ALL) - len(miss)}/{len(ALL)}"
          + (f"; never executed: {[hex(a) for a in miss]}" if miss else ""))
except ImportError:
    print("\n(capstone not installed: no coverage report)")
print("\nALL PASS" if ok else "\nSOME TESTS FAILED")
