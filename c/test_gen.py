"""
Differential test for lifter-generated routines (gen/*.c) vs. the real ROM
code in Unicorn.

Each routine runs in isolation. On the ROM side, any instruction outside the
routine's own body (as lift.py found it) counts as a call into untranslated
code; on the C side every JSR / g4_jmp goes to g4_external. Both sides then
get the same scripted answer from an RNG seeded identically: random rewrites
of the RAM variables the routine uses, and random condition codes (all five)
on return. Hardware reads get the same scripted values on both sides.

Compared: every call (target, return address, D0-D7, A0-A7, all of RAM),
every hardware access, and the end state (registers, all five flags, RAM).
Routines ending in rte get an exception frame (PC, SR) on both sides.

Build: gcc -std=c99 -O1 -Wall -Wextra -pedantic -shared -fPIC -o libg4.so \\
           g4_*.c gen/*.c test_shim.c
Usage: python3 test_gen.py <rom.bin> <libg4.so> <runs> <gen-file> [addr ...]

With G4SNAP=music the snapshots also include RAM taken while songs and sound
effects play (g4emu.music_snapshots), for the sound driver's deep paths.
With G4SNAP=1 most runs start from real game RAM: snapshots taken by
g4emu.py (the ROM booted into set-up levels of every mode), with the
routine's variables perturbed on top. Routines that walk the game's own
pointers and tables (the VBlank handler) need this to reach their deep paths.
"""
import ctypes, hashlib, os, random, re, struct, sys
from unicorn import (Uc, UcError, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL, UC_HOOK_CODE,
                     UC_HOOK_MEM_FETCH_UNMAPPED, UC_HOOK_MEM_READ_UNMAPPED, UC_HOOK_MEM_WRITE_UNMAPPED)
from unicorn.m68k_const import *

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
import lift                                                   # same body discovery as the generator

ROM = open(sys.argv[1], "rb").read()
lib = ctypes.CDLL(os.path.abspath(sys.argv[2]))
RUNS = int(sys.argv[3])
GENFILE = sys.argv[4]
GSRC = open(GENFILE).read()
ONLY = {int(a, 16) for a in sys.argv[5:]}

class M68K(ctypes.Structure):
    _fields_ = [("d", ctypes.c_uint32 * 8), ("a", ctypes.c_uint32 * 8), ("x", ctypes.c_uint32), ("z", ctypes.c_uint32),
                ("sr", ctypes.c_uint32), ("usp", ctypes.c_uint32),
                ("n", ctypes.c_uint32), ("v", ctypes.c_uint32), ("cf", ctypes.c_uint32)]
EXT_CB = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.POINTER(M68K), ctypes.c_uint32)
RD_CB = ctypes.CFUNCTYPE(ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
WR_CB = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_int)
for f in ("g4_test_run",): getattr(lib, f).argtypes = [ctypes.POINTER(M68K), ctypes.c_uint32]
lib.g4_test_run_irq.argtypes = [ctypes.POINTER(M68K), ctypes.c_uint32, ctypes.c_uint32]
lib.g4_test_set.argtypes = [EXT_CB, RD_CB, WR_CB]
TRACE = tuple(int(x, 16 if i == 0 else 10) for i, x in enumerate(os.environ["G4TRACE"].split(","))) if os.environ.get("G4TRACE") else None
TR_CB = ctypes.CFUNCTYPE(None, ctypes.POINTER(M68K), ctypes.c_uint32)
TRACE_LOG = {"rom": [], "c": []}
if TRACE:
    def _ctrace(cp, pc):
        c = cp.contents; TRACE_LOG["c"].append((pc, tuple(c.d), tuple(c.a)))
    _CTRACE = TR_CB(_ctrace)
    lib.g4_test_set_trace.argtypes = [TR_CB]; lib.g4_test_set_trace(_CTRACE)
MEM = (ctypes.c_uint8 * 0x1000000)()
ctypes.memmove(MEM, ROM, len(ROM))
ctypes.c_void_p.in_dll(lib, "g4_mem").value = ctypes.addressof(MEM)
ctypes.c_int.in_dll(lib, "g4_use_translated").value = 0

CAP = int(os.environ.get("G4CAP", "5000000"))   # ROM-side instruction budget per run
# G4REGION="fff000-fff500": also perturb random bytes in this RAM range (state reached through pointers)
REGION = tuple(int(x, 16) for x in os.environ["G4REGION"].split("-")) if os.environ.get("G4REGION") else None
# G4REGS="a4=a04000,a5=a11100": registers that callers always set up, preset in most runs
# a value list "d6=0|1" picks one of them per run
PRESET = {k: [int(x, 16) for x in v.split("|")] for k, v in (x.split("=") for x in os.environ["G4REGS"].split(","))} if os.environ.get("G4REGS") else {}

class Budget(Exception):
    """The ROM side hit the instruction cap (a loop that random input never lets end)."""

RAM, SENTINEL, PROBE, PROBE_END, SP0 = 0xFF0000, 0xE00000, 0xE00100, 0xE00006, 0xFFFD00
DREG = [UC_M68K_REG_D0 + i for i in range(8)]; AREG = [UC_M68K_REG_A0 + i for i in range(8)]

ROUTINES = [(int(a, 16), n) for a, n in re.findall(r"^/\* ([0-9a-f]{6}) \*/\nvoid (g4_\w+)\(M68K \*c\)", GSRC, re.M)]
if ONLY: ROUTINES = [r for r in ROUTINES if r[0] in ONLY]
if TRACE: ROUTINES = [r for r in ROUTINES if r[0] == TRACE[0]]

def body_of(entry):
    return lift.discover(entry)

def vars_of(entry):
    """RAM addresses named in this routine's generated code."""
    m = re.search(rf"/\* {entry:06x} \*/\nvoid \w+\(M68K \*c\)\n\{{(.*?)\n\}}", GSRC, re.S)
    return sorted({int(v, 16) for v in re.findall(r"0x(FF[0-9A-F]{4})u", m.group(1))})

def fields_of(entry):
    """Struct arrays the routine walks: RAM bases it loads with lea, the strides it steps by
    (lea $n(An),An), and the field offsets it uses ($n(An)); perturbing those reaches paths
    guarded by per-player / per-slot flags."""
    m = re.search(rf"/\* {entry:06x} \*/\nvoid \w+\(M68K \*c\)\n\{{(.*?)\n\}}", GSRC, re.S).group(1)
    asm = re.findall(r"/\* [0-9a-f]{6}  ([^*]*?) \*/", m)
    bases = sorted({int(b, 16) for a in asm for b in re.findall(r"lea\.l \$(ff[0-9a-f]{4})\.l", a)})
    strides = sorted({int(x, 16) for a in asm for x, r1, r2 in re.findall(r"lea\.l \$([0-9a-f]+)\((a\d)\), (a\d)", a) if r1 == r2})
    offs = sorted({int(x, 16) for a in asm for x in re.findall(r"\$([0-9a-f]{1,3})\(a\d\)", a)})
    # the four player structs (0x2C4 bytes at $FF0040) are often reached through ROM pointer tables
    return sorted(set(bases) | {0xFF0040}), sorted(set(strides) | {0x2C4}), offs

def rand_val(r):
    return r.choice([0, 0, 0, 0, 0, 1, 2, 3, r.randrange(4, 8), 0x0B, 0x50, 0xFF, 0xFFFF, r.randrange(0x10000)])

class Script:
    def __init__(self, seed, vars_, max_calls=2000):
        self.r, self.vars, self.n, self.max = random.Random(seed), vars_, 0, max_calls
    def io_read(self, addr, size):
        if addr == 0xA11100: return 0                         # Z80 bus granted
        if addr in (0xC00004, 0xC00006): return self.r.choice([0x3400, 0x3408, 0x3402])
        if addr == 0xA00113: return 0 if self.r.random() < 0.9 else 0xFF       # Z80 "YM busy": usually free
        if 0xA04000 <= addr <= 0xA04003:                                      # YM2612 status: usually ready
            return self.r.choice([0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x80])
        return self.r.randrange(1 << (8 * size))
    def ext(self, mem, target):
        r = self.r; self.n += 1
        for _ in range(r.randint(0, 3)):
            if not self.vars: break
            v = r.choice(self.vars); val = rand_val(r)
            if r.random() < 0.5: mem(v, bytes([val & 0xFF]))
            else: mem(v & ~1, struct.pack(">H", val))
        return r.randrange(32), self.n >= self.max           # CCR (X N Z V C), stop?

# ---------------------------------------------------------------- ROM side
RAMBUF = (ctypes.c_uint8 * 0x10000)()
ROMBUF = (ctypes.c_uint8 * 0x100000)(); ctypes.memmove(ROMBUF, ROM, len(ROM))
PAGES = {}                                   # empty 24-bit pages, shared by all their mirrors
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
try: uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
except Exception: pass
uc.mem_map_ptr(0, 0x100000, UC_PROT_ALL, ctypes.addressof(ROMBUF))
uc.mem_map_ptr(RAM, 0x10000, UC_PROT_ALL, ctypes.addressof(RAMBUF))

def mirror(u, addr):
    """The 68000 drives 24 address lines: map the page at addr onto its 24-bit mirror."""
    a24 = addr & 0xFFF000 & 0xFFFFFF; page = addr & 0xFFFFF000
    if a24 < 0x100000: host = ctypes.addressof(ROMBUF) + a24
    elif a24 >= RAM: host = ctypes.addressof(RAMBUF) + (a24 - RAM)
    elif 0xA00000 <= a24 < 0xE01000: return False            # hardware mirrors: not modelled
    else:
        if a24 not in PAGES: PAGES[a24] = (ctypes.c_uint8 * 0x1000)()
        host = ctypes.addressof(PAGES[a24])
    try: u.mem_map_ptr(page, 0x1000, UC_PROT_ALL, host)
    except UcError: return False
    return True

def on_data_unmapped(u, access, addr, size, value, _):
    ok = mirror(u, addr)
    if ok and (addr & 0xFFF) + size > 0x1000: ok = mirror(u, addr + size - 1)
    return ok
uc.mem_map(0xE00000, 0x1000, UC_PROT_ALL); uc.mem_write(SENTINEL, bytes.fromhex("40f900e00100"))
CUR = {}
def io_rd(u, off, size, base):
    a = (base + off) & (~1 if size > 1 else ~0); s = CUR["script"]; log = CUR["io"]
    if size == 4:
        hi, lo = s.io_read(a, 2), s.io_read(a + 2, 2); log += [("r", a, 2, hi), ("r", a + 2, 2, lo)]; return (hi << 16) | lo
    v = s.io_read(a, size); log.append(("r", a, size, v)); return v
def io_wr(u, off, size, value, base):
    a = base + off; log = CUR["io"]                         # writes are compared byte by byte (bytewise)
    if size == 4: log += [("w", a, 2, (value >> 16) & 0xFFFF), ("w", a + 2, 2, value & 0xFFFF)]
    else: log.append(("w", a, size, value & ((1 << (8 * size)) - 1)))
uc.mmio_map(0xA00000, 0x20000, io_rd, 0xA00000, io_wr, 0xA00000)
uc.mmio_map(0xC00000, 0x1000, io_rd, 0xC00000, io_wr, 0xC00000)
COVERED = set()

REGS_UC = {**{f"d{i}": UC_M68K_REG_D0 + i for i in range(8)}, **{f"a{i}": UC_M68K_REG_A0 + i for i in range(8)}}

def indirect_target(u, i):
    """Target of jsr/jmp through a register: (An), d(An), d(An,Xn.s), d(PC,Xn.s)."""
    m = re.match(r"^(-?\$?[0-9a-f]+)?\(([^)]*)\)$", i.op_str.strip())
    if not m: return None
    disp = lift.num(m.group(1)) if m.group(1) else 0
    parts = [p.strip() for p in m.group(2).split(",")]
    base = disp if parts[0] == "pc" else (u.reg_read(REGS_UC[parts[0]]) + disp)
    if len(parts) > 1:
        ix, isz = (parts[1].split(".") + ["w"])[:2]
        v = u.reg_read(REGS_UC[ix]) & 0xFFFFFFFF
        base += (v & 0xFFFF) - (0x10000 if v & 0x8000 else 0) if isz == "w" else v
    return base & 0xFFFFFFFF

def on_code(u, pc, size, _):
    st = CUR
    if st.pop("callee_next", False) and pc not in (SENTINEL, PROBE_END):
        # the instruction after a bsr/jsr is the callee's entry, even when that address is also
        # part of this routine's body (shared code): a call, as the C side's JSR() makes it
        external(u, pc); return
    if pc in st["body"]:
        st["cov"].add(pc)
        if TRACE: TRACE_LOG["rom"].append((pc, tuple(u.reg_read(r) & 0xFFFFFFFF for r in DREG), tuple(u.reg_read(r) & 0xFFFFFFFF for r in AREG)))
        i = st["body"][pc]; b = i.mnemonic.split(".")[0]
        if b == "rte":                                        # Unicorn faults on rte: do the 68000 pop by hand
            sp = u.reg_read(UC_M68K_REG_A7)
            sr = struct.unpack(">H", bytes(u.mem_read(sp, 2)))[0]
            ret = struct.unpack(">I", bytes(u.mem_read(sp + 2, 4)))[0]
            u.reg_write(UC_M68K_REG_A7, sp + 6); u.reg_write(UC_M68K_REG_SR, sr); u.reg_write(UC_M68K_REG_PC, ret)
            return
        if b in ("bsr", "jsr"): st["callee_next"] = True
        if b in ("jsr", "jmp") and "(" in i.op_str:
            t = indirect_target(u, i)
            if t is not None and (t >= len(ROM) or t & 1):     # not code: the dispatcher's g4_external case
                if b == "jsr":
                    sp = u.reg_read(UC_M68K_REG_A7) - 4
                    u.mem_write(sp, struct.pack(">I", pc + i.size)); u.reg_write(UC_M68K_REG_A7, sp)
                st["callee_next"] = False
                external(u, t)
        return
    if pc in (SENTINEL, PROBE_END) or 0xE00000 <= pc < 0xE01000: return
    external(u, pc)

def on_fetch_unmapped(u, access, addr, size, value, _):
    """A computed call to an address with nothing there: still a call into untranslated code.
    Unicorn retries the fetch, so map the page onto its 24-bit mirror first."""
    mirror(u, addr)
    external(u, addr)
    return True

def external(u, pc):
    st = CUR
    sp = u.reg_read(UC_M68K_REG_A7)
    ret = struct.unpack(">I", bytes(u.mem_read(sp, 4)))[0]
    st["calls"].append((pc & 0xFFFFFF, ret, tuple(u.reg_read(r) & 0xFFFFFFFF for r in DREG + AREG), hashlib.md5(bytes(RAMBUF)).hexdigest()))
    ccr, stop = st["script"].ext(lambda a, b: u.mem_write(a, b), pc)
    u.reg_write(UC_M68K_REG_A7, sp + 4)
    sr = u.reg_read(UC_M68K_REG_SR)
    u.reg_write(UC_M68K_REG_SR, (sr & ~0x1F) | ccr)
    if stop: st["stopped"] = True; u.emu_stop()
    else: u.reg_write(UC_M68K_REG_PC, ret)
uc.hook_add(UC_HOOK_CODE, on_code, None, 0, 0xFFFFFFFF)
uc.hook_add(UC_HOOK_MEM_FETCH_UNMAPPED, on_fetch_unmapped)
uc.hook_add(UC_HOOK_MEM_READ_UNMAPPED | UC_HOOK_MEM_WRITE_UNMAPPED, on_data_unmapped)

def rom_run(entry, irq, regs, ram, script):
    global CUR
    CUR = {"script": script, "io": [], "calls": [], "body": BODY[entry], "stopped": False, "cov": set()}
    ctypes.memmove(RAMBUF, bytes(ram), 0x10000)
    sp = SP0 - 4; uc.mem_write(sp, struct.pack(">I", SENTINEL))
    if irq: sp -= 2; uc.mem_write(sp, struct.pack(">H", regs["sr"]))
    uc.reg_write(UC_M68K_REG_SR, 0x2700 | regs["ccr"])
    for i in range(8): uc.reg_write(DREG[i], regs["d"][i])
    for i in range(7): uc.reg_write(AREG[i], regs["a"][i])
    uc.reg_write(UC_M68K_REG_A7, sp)
    uc.emu_start(entry, PROBE_END, count=CAP)
    if not CUR["stopped"] and uc.reg_read(UC_M68K_REG_PC) != PROBE_END:
        raise Budget()                                        # ran out of instructions: not comparable
    end = None
    if not CUR["stopped"]:
        srw = struct.unpack(">H", bytes(uc.mem_read(PROBE, 2)))[0]
        end = (tuple(uc.reg_read(r) & 0xFFFFFFFF for r in DREG + AREG), srw & 0x1F)
    return CUR["calls"], CUR["io"], end, bytes(RAMBUF)

# ---------------------------------------------------------------- C side
def c_run(entry, irq, regs, ram, script):
    calls, io = [], []
    ctypes.memmove(ctypes.addressof(MEM) + RAM, bytes(ram), 0x10000)
    cpu = M68K()
    for i in range(8): cpu.d[i] = regs["d"][i]
    for i in range(7): cpu.a[i] = regs["a"][i]
    cpu.a[7] = SP0; cpu.sr = 0x2700
    cc = regs["ccr"]; cpu.x, cpu.n, cpu.z, cpu.v, cpu.cf = (cc >> 4) & 1, (cc >> 3) & 1, (cc >> 2) & 1, (cc >> 1) & 1, cc & 1
    def ram_now(): return ctypes.string_at(ctypes.addressof(MEM) + RAM, 0x10000)
    def ext(cp, target):
        c = cp.contents; sp = c.a[7] & 0xFFFFFF
        ret = struct.unpack(">I", ctypes.string_at(ctypes.addressof(MEM) + sp, 4))[0]
        calls.append((target, ret, tuple(list(c.d) + list(c.a)), hashlib.md5(ram_now()).hexdigest()))
        ccr, stop = script.ext(lambda a, b: ctypes.memmove(ctypes.addressof(MEM) + (a & 0xFFFFFF), b, len(b)), target)
        return 8 | (ccr << 8) | (2 if stop else 0)
    def rd(a, size):
        a &= ~1 if size > 1 else ~0                          # odd word access: an address error on the CPU
        v = script.io_read(a, size); io.append(("r", a, size, v)); return v
    def wr(a, v, size):
        io.append(("w", a, size, v & ((1 << (8 * size)) - 1)))
    cbs = (EXT_CB(ext), RD_CB(rd), WR_CB(wr)); lib.g4_test_set(*cbs)
    res = lib.g4_test_run_irq(ctypes.byref(cpu), entry, regs["sr"]) if irq else lib.g4_test_run(ctypes.byref(cpu), entry)
    end = None
    if res == 0:
        end = (tuple(list(cpu.d) + list(cpu.a)), (cpu.x << 4) | (cpu.n << 3) | (cpu.z << 2) | (cpu.v << 1) | cpu.cf)
    return calls, io, end, ram_now()

def restore_rom():
    """Stray writes through random pointers may hit ROM, or the return-probe page at $E00000,
    on either side: put them back (a clobbered probe reports stale flags for every later run)."""
    uc.mem_write(0xE00000, bytes(0x1000)); uc.mem_write(SENTINEL, bytes.fromhex("40f900e00100"))
    if bytes(ROMBUF[:len(ROM)]) != ROM: ctypes.memmove(ROMBUF, ROM, len(ROM))
    if ctypes.string_at(ctypes.addressof(MEM), len(ROM)) != ROM: ctypes.memmove(MEM, ROM, len(ROM))
    for p in PAGES.values(): ctypes.memset(p, 0, 0x1000)
    for a in range(0x100000, RAM, 0x1000):
        pass

def bytewise(io):
    """Hardware writes as single bytes: Unicorn splits a long write at an address 2 mod 4 into
    four byte writes where the 68000 bus does two word writes; the bytes written are the same."""
    out = []
    for k, a, sz, v in io:
        if k == "w": out += [("w", a + j, 1, (v >> (8 * (sz - 1 - j))) & 0xFF) for j in range(sz)]
        else: out.append((k, a, sz, v))
    return out

def first_diff(a, b):
    return next((i for i, (x, y) in enumerate(zip(a, b)) if x != y), min(len(a), len(b)))

SNAPS = []
if os.environ.get("G4SNAP"):
    from g4emu import Emu
    for mode, level, alt in ((0, 1, False), (0, 5, False), (0, 40, True), (3, 1, False), (0, 0x81, False), (0, 0x8E, False), (0, 0x92, False)):
        e = Emu(ROM); e.boot_to_level(mode, level, alt); SNAPS.append(bytearray(e.ram()))
    if os.environ["G4SNAP"] in ("music", "musicwide"):         # plus RAM with songs / effects playing
        import g4emu
        SNAPS += [bytearray(ram) for _, ram in g4emu.music_snapshots(ROM)]
    if os.environ["G4SNAP"] == "musicwide":                     # every song over time, every effect
        SNAPS += [bytearray(ram) for _, ram in g4emu.music_snapshots_wide(ROM, os.environ.get("G4SNAPCACHE"))]
    print(f"{len(SNAPS)} RAM snapshots from real level setups" + (" and playing music" if os.environ["G4SNAP"] != "1" else ""))

BODY = {}
ok = True
for entry, name in ROUTINES:
    BODY[entry] = body_of(entry)
    irq = any(BODY[entry][pc].mnemonic == "rte" for pc in BODY[entry])
    vs = vars_of(entry)
    FIELDS = fields_of(entry)
    bad = []; ncalls = nio = nret = skipped = capped = 0
    for s in range(RUNS):
        if TRACE and s != TRACE[1]: continue
        r = random.Random((entry << 12) ^ s)
        ram = bytearray(r.choice(SNAPS)) if SNAPS and r.random() < 0.85 else bytearray(r.randbytes(0x10000))
        if SNAPS and r.random() < 0.5:                          # one player joined, as in play
            ram[0x0B60:0x0B62] = b"\0\0"; ram[0x0B50:0x0B54] = struct.pack(">I", 0xFF0040)
            ram[0x0040 + 0x60] = ram[0x0040 + 0x61] = 0
        zero_bias = r.choice([0.5, 0.8, 0.95])                 # variables: often 0, as game flags mostly are
        keep = r.choice([0.5, 0.85, 0.97])
        for v in vs:
            if SNAPS and r.random() < keep: continue           # mostly keep the real value
            if r.random() < zero_bias:
                if r.random() < 0.5: ram[v - RAM] = rand_val(r) & 0xFF
                else: ram[(v & ~1) - RAM:(v & ~1) - RAM + 2] = struct.pack(">H", rand_val(r))
        fb, fs, fo = FIELDS
        if fb and fo:
            for _ in range(r.randint(0, 16)):                 # flags in struct arrays (players, slots)
                a = r.choice(fb) + r.choice(fs) * r.randrange(4) + r.choice(fo)
                if RAM <= a < RAM + 0xFFFE:
                    if a % 2 == 0 and r.random() < 0.5:           # word fields: counters, timers
                        ram[a - RAM:a - RAM + 2] = struct.pack(">H", r.choice([0, 1, 1, 2, 3, 0x10, 0x7FFF, 0x8000, 0xFFFF, r.randrange(0x10000)]))
                    else:
                        ram[a - RAM] = r.choice([0, 0, 0xFF, 0xFF, 1, 0x80, r.randrange(256)])
        if REGION:                                            # G4REGION: random bytes in a RAM range
            lo, hi = REGION
            for _ in range(r.randint(0, 24)):
                a = r.randrange(lo, hi); ram[a - RAM] = r.choice([0, 0, 1, 2, 3, 4, 0x80, 0xFF, r.randrange(256)])
        regs = {"d": [r.randrange(1 << 32) for _ in range(8)], "a": [r.randrange(1 << 32) for _ in range(7)],
                "ccr": r.randrange(32), "sr": 0x2000 | r.randrange(32)}
        # address registers usually point at RAM, as callers set them up
        regs["a"] = [a if r.random() < 0.2 else 0xFF0000 | (a & 0xFFFE) for a in regs["a"]]
        if PRESET and r.random() < 0.7:                       # registers the routine's callers always set
            for k, v in PRESET.items(): regs[k[0]][int(k[1])] = r.choice(v)
        try:
            A = rom_run(entry, irq, regs, ram, Script(s, vs))
        except Budget:
            capped += 1; restore_rom(); continue
        except UcError as e:
            if os.environ.get("G4DEBUG"): print("   skip:", e, hex(uc.reg_read(UC_M68K_REG_PC)))
            # a random pointer reached the hardware area's mirrors: no game state does that
            skipped += 1; restore_rom(); continue
        COVERED.update(CUR["cov"])                            # coverage only from compared runs
        A = (A[0], bytewise(A[1]), A[2], A[3])
        restore_rom()
        B = c_run(entry, irq, regs, ram, Script(s, vs))
        B = (B[0], bytewise(B[1]), B[2], B[3])
        ctypes.memset(ctypes.addressof(MEM) + 0x100000, 0, 0xA00000 - 0x100000); ctypes.memset(ctypes.addressof(MEM) + 0xE01000, 0, RAM - 0xE01000)
        restore_rom()
        ncalls += len(A[0]); nio += len(A[1]); nret += A[2] is not None
        if A != B:
            msg = []
            if A[0] != B[0]:
                k = first_diff(A[0], B[0])
                msg.append(f"call #{k}: ROM {A[0][k] if k < len(A[0]) else None}\n         C   {B[0][k] if k < len(B[0]) else None}")
            if A[1] != B[1]:
                k = first_diff(A[1], B[1]); msg.append(f"io #{k}: ROM {A[1][k] if k < len(A[1]) else None} C {B[1][k] if k < len(B[1]) else None}")
            if A[2] != B[2]: msg.append(f"end ROM {A[2]}\n    C   {B[2]}")
            if A[3] != B[3]:
                k = next(i for i in range(0x10000) if A[3][i] != B[3][i]); msg.append(f"RAM {RAM + k:#x}: ROM {A[3][k]:#x} C {B[3][k]:#x}")
            bad.append((s, msg))
    if TRACE and os.environ.get("G4IODUMP"):
        print("ROM io:", [(k, hex(a), sz, hex(v)) for k, a, sz, v in A[1]]); print("C   io:", [(k, hex(a), sz, hex(v)) for k, a, sz, v in B[1]])
    if TRACE:
        R_, C_ = TRACE_LOG["rom"], TRACE_LOG["c"]
        k = next((j for j, (x, y) in enumerate(zip(R_, C_)) if x != y), min(len(R_), len(C_)))
        print(f"trace: {len(R_)} ROM steps, {len(C_)} C steps; first difference at step {k}")
        for j in range(max(0, k - 6), min(k + 2, max(len(R_), len(C_)))):
            for side, L in (("ROM", R_), ("C  ", C_)):
                if j < len(L):
                    pc, d, a = L[j]; ins = BODY[entry].get(pc)
                    print(f"  {j:4d} {side} {pc:06x} {ins.mnemonic + ' ' + ins.op_str if ins else '?':32s} D={[hex(x) for x in d]} A={[hex(x) for x in a]}")
    cov = len(set(BODY[entry]) & COVERED)
    ok &= not bad
    print(f"{'PASS' if not bad else 'FAIL'} {entry:06x} {name:18s} {RUNS} runs, {ncalls} calls, {nio} I/O, {nret} returned, "
          f"coverage {cov}/{len(BODY[entry])}" + (f", {skipped} skipped (pointer into hardware mirrors)" if skipped else "")
          + (f", {capped} skipped (endless loop on random input)" if capped else ""))
    if os.environ.get("G4MISS"):                              # uncovered ranges, with the instruction before each
        miss = sorted(set(BODY[entry]) - COVERED); runs = []
        for pc in miss:
            if runs and pc - runs[-1][1] <= 8: runs[-1][1] = pc
            else: runs.append([pc, pc])
        for lo, hi in runs:
            n = sum(1 for p in miss if lo <= p <= hi)
            prev = max((p for p in BODY[entry] if p < lo), default=None)
            pi = BODY[entry][prev] if prev is not None else None
            print(f"   miss {lo:06x}-{hi:06x} ({n}) after {prev:06x} {pi.mnemonic} {pi.op_str}" if pi else f"   miss {lo:06x}-{hi:06x} ({n})")
    for s, m in bad[:2]:
        print(f"   seed {s}:"); [print("     " + x) for x in m]
allbody = set().union(*(set(b) for b in BODY.values())) if BODY else set()
print(f"\nunion coverage (each instruction run in some routine's test): {len(allbody & COVERED)}/{len(allbody)}")
print("\nALL PASS" if ok else "\nSOME TESTS FAILED")
