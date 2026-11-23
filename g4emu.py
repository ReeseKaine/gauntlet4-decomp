#!/usr/bin/env python3
"""
g4emu.py -- headless runner for Gauntlet 4: the real ROM in Unicorn plus a
minimal VDP model (ports, registers, VRAM/CRAM/VSRAM, auto-increment, DMA),
enough to run the game's own level setup and read back what it drew.

    emu = Emu(rom_bytes)
    emu.boot_to_level(mode=0, level=1, alt=False)   # reset -> title skipped -> level set up
    emu.vdp.vram / emu.vdp.cram / emu.vdp.regs ; emu.ram()

Title and attract (0x22504) are skipped and 0x236A4 answers "Start pressed";
player placement (0x2116) is skipped, since no player has joined, and so are the
two random post-processing steps of level_build (0x1939E keeps one of several
tile-4 cells, 0x193F0 scatters bonus food per player), so the result is the
level as designed. The run
stops at 0x94C, where play_level has finished setting the level up and
enters its per-frame loop.
"""
import ctypes, struct
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL, UC_HOOK_CODE
from unicorn.m68k_const import *

class VDP:
    def __init__(self, emu):
        self.emu = emu
        self.vram = bytearray(0x10000); self.cram = [0] * 64; self.vsram = [0] * 40
        self.regs = [0] * 24
        self.code = 0; self.addr = 0; self.pending = False; self.fill = False
        self.vb = 0; self.hv = 0
        self.writes = {"vram": 0, "cram": 0, "vsram": 0, "dma": 0}

    # --- ports
    def ctrl_write(self, w):
        if self.pending:
            self.pending = False
            self.addr = (self.addr & 0x3FFF) | ((w & 3) << 14)
            self.code = (self.code & 0x03) | ((w >> 2) & 0x3C)
            if self.code & 0x20 and self.regs[1] & 0x10:
                self.dma()
            return
        if (w & 0xC000) == 0x8000:
            r = (w >> 8) & 0x1F
            if r < 24: self.regs[r] = w & 0xFF
            return
        self.code = (self.code & 0x3C) | (w >> 14)
        self.addr = (self.addr & 0xC000) | (w & 0x3FFF)
        self.pending = True

    def data_write(self, w):
        self.pending = False
        if self.fill:                                  # DMA fill: the data write starts it
            self.fill = False
            n = self.dma_len()
            self._store(w)
            for _ in range(n):
                self.vram[self.addr & 0xFFFF] = (w >> 8) & 0xFF
                self.addr = (self.addr + self.regs[15]) & 0xFFFF
            return
        self._store(w)

    def _store(self, w):
        c = self.code & 0x0F
        if c == 1:
            a = self.addr & 0xFFFE
            self.vram[a] = (w >> 8) & 0xFF; self.vram[a + 1] = w & 0xFF; self.writes["vram"] += 1
        elif c == 3:
            self.cram[(self.addr >> 1) & 0x3F] = w & 0x0EEE; self.writes["cram"] += 1
        elif c == 5:
            i = (self.addr >> 1) & 0x3F
            if i < 40: self.vsram[i] = w & 0x07FF
            self.writes["vsram"] += 1
        self.addr = (self.addr + self.regs[15]) & 0xFFFF

    def status_read(self):
        self.vb ^= 1                                    # alternate in/out of VBlank on every read
        return 0x3400 | (self.vb << 3)

    # --- DMA
    def dma_len(self):
        n = self.regs[19] | (self.regs[20] << 8)
        return n or 0x10000

    def dma(self):
        self.writes["dma"] += 1
        kind = self.regs[23] >> 6
        if kind in (0, 1):                             # 68000 memory -> VDP
            src = ((self.regs[23] & 0x7F) << 17) | (self.regs[22] << 9) | (self.regs[21] << 1)
            for _ in range(self.dma_len()):
                w = struct.unpack(">H", bytes(self.emu.uc.mem_read(src, 2)))[0]
                self._store(w); src = (src + 2) & 0xFFFFFF
            self.code &= ~0x20
        elif kind == 2:                                # fill: waits for the next data write
            self.fill = True
        else:                                          # VRAM copy
            src = self.regs[21] | (self.regs[22] << 8)
            for _ in range(self.dma_len()):
                self.vram[self.addr & 0xFFFF] = self.vram[src & 0xFFFF]
                src += 1; self.addr = (self.addr + self.regs[15]) & 0xFFFF


class Stop(Exception):
    pass


class Emu:
    WAIT_VBLANK = 0x020496

    def __init__(self, rom):
        self.rom = rom
        self.ramb = (ctypes.c_uint8 * 0x10000)()
        uc = self.uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
        try: uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
        except Exception: pass
        uc.mem_map(0, 0x100000, UC_PROT_ALL); uc.mem_write(0, rom)
        uc.mem_map_ptr(0xFF0000, 0x10000, UC_PROT_ALL, ctypes.addressof(self.ramb))
        uc.mem_map_ptr(0xFFFF0000, 0x10000, UC_PROT_ALL, ctypes.addressof(self.ramb))
        uc.mmio_map(0xA00000, 0x20000, self._io_read, None, self._io_write, None)
        uc.mmio_map(0xC00000, 0x1000, self._vdp_read, None, self._vdp_write, None)
        self.vdp = VDP(self)
        self.z80ram = bytearray(0x2000)
        self.stubs = {}          # address -> callable(emu) returning the Z flag; then rts
        self.pre = {}            # address -> callable(emu), runs before the instruction there
        self.stop_at = set()
        self._hooks = []

    # --- memory helpers
    def ram(self): return bytes(self.ramb)
    def r8(self, a): return self.uc.mem_read(a, 1)[0]
    def r16(self, a): return struct.unpack(">H", bytes(self.uc.mem_read(a, 2)))[0]
    def r32(self, a): return struct.unpack(">I", bytes(self.uc.mem_read(a, 4)))[0]
    def w8(self, a, v): self.uc.mem_write(a, bytes([v & 0xFF]))
    def w16(self, a, v): self.uc.mem_write(a, struct.pack(">H", v & 0xFFFF))

    # --- hardware
    def _io_read(self, uc, off, size, _):
        a = 0xA00000 + off
        if a < 0xA02000:                               # Z80 RAM (8 KB), as the 68000 sees it
            o = a - 0xA00000
            return self.z80ram[o] if size == 1 else (self.z80ram[o] << 8) | self.z80ram[o + 1] if size == 2 \
                else int.from_bytes(self.z80ram[o:o + 4], "big")
        if a == 0xA11100: return 0                     # Z80 bus granted
        if a in (0xA10003, 0xA10005, 0xA10002, 0xA10004): return 0x7F if size == 1 else 0x007F   # pads: nothing pressed
        if a == 0xA10001 or a == 0xA10000: return 0xA0
        return 0

    def _io_write(self, uc, off, size, value, _):
        a = 0xA00000 + off
        if a < 0xA02000:                               # keep Z80 RAM: the program upload, the sample
            o = a - 0xA00000                           # directory and the 68000's commands
            self.z80ram[o:o + size] = (value & ((1 << (8 * size)) - 1)).to_bytes(size, "big")

    def _vdp_read(self, uc, off, size, _):
        if off in (4, 6):
            v = self.vdp.status_read()
            return (v << 16) | self.vdp.status_read() if size == 4 else v
        if off in (8, 0xA):
            self.vdp.hv = (self.vdp.hv + 0x0101) & 0xFFFF; return self.vdp.hv
        return 0

    def _vdp_write(self, uc, off, size, value, _):
        if off >= 0x10: return                         # PSG
        words = [(value >> 16) & 0xFFFF, value & 0xFFFF] if size == 4 else \
                [value & 0xFFFF] if size == 2 else [((value & 0xFF) << 8) | (value & 0xFF)]
        for w in words:
            if off in (4, 6): self.vdp.ctrl_write(w)
            elif off in (0, 2): self.vdp.data_write(w)

    # --- control
    def _on_code(self, uc, addr, size, _):
        if addr in self.pre: self.pre[addr](self)
        if addr in self.stop_at:
            uc.emu_stop(); self.stopped_at = addr; return
        fn = self.stubs.get(addr)
        if fn is None: return
        z = fn(self)
        sp = uc.reg_read(UC_M68K_REG_A7)
        ret = self.r32(sp)
        uc.reg_write(UC_M68K_REG_A7, sp + 4)
        sr = uc.reg_read(UC_M68K_REG_SR)
        uc.reg_write(UC_M68K_REG_SR, (sr & ~0x04) | (0x04 if z else 0))
        uc.reg_write(UC_M68K_REG_PC, ret)

    def run(self, start, count=200_000_000):
        for h in self._hooks: self.uc.hook_del(h)
        self._hooks = [self.uc.hook_add(UC_HOOK_CODE, self._on_code, None, a, a)
                       for a in set(self.stubs) | set(self.pre) | set(self.stop_at)]
        self.stopped_at = None
        sp = struct.unpack(">I", self.rom[0:4])[0]
        self.uc.reg_write(UC_M68K_REG_A7, sp)
        self.uc.reg_write(UC_M68K_REG_SR, 0x2700)
        self.uc.emu_start(start, 0xFFFFFFFF, count=count)
        return self.stopped_at

    @staticmethod
    def quest_setting(level):
        """(mode, $ffe842, area $ffe844, stage $ffe846) the game uses for a level number >= 0x81:
        0x81-0x8D Battle arenas; 0x8E Quest hub; 0x8F + area*10 + stage-1 Quest stages (0x1310A)."""
        if level < 0x8E: return 2, 0, 0, 1
        if level == 0x8E: return 1, 1, 0, 1
        n = level - 0x8F
        return 1, 0, n // 10, n % 10 + 1

    def boot_to_level(self, mode=0, level=1, alt=False):
        """Reset, skip the title, start a game in `mode`, set up `level`; stop before its first frame.
        Levels >= 0x81 pick their own mode / hub / area / stage (quest_setting)."""
        self.stubs = {
            0x022504: lambda e: 0,                     # title / attract: skip
            0x0236A4: lambda e: 1,                     # "Start pressed" (Z = 1)
            0x002116: lambda e: 0,                     # place players: nobody has joined
            0x01939E: lambda e: 0,                     # random pick among several tile-4 cells: keep them all
            0x0193F0: lambda e: 0,                     # random bonus food per player: skip (designed map only)
        }
        def before_setup(e):                           # hook at the bsr to game_setup
            e.w16(0xFFE838, mode)
            e.w8(0xFFE790, 0x00 if alt else 0xFF)
        if level >= 0x81: mode, hub, area, stage = self.quest_setting(level)
        def before_level(e):
            e.w16(0xFFE758, level)
            if mode == 1:
                e.w16(0xFFE842, hub); e.w16(0xFFE844, area); e.w16(0xFFE846, stage)
        self.pre = {0x000528: before_setup, 0x0008C2: before_level}
        self.stop_at = {0x00094C}
        return self.run(0x000400)


# ---------------------------------------------------------------- running the game past setup
SENT = 0xE00000                     # return address that ends call() / frame()

def _ensure_sentinel(emu):
    if getattr(emu, "_sent", False): return
    emu.uc.mem_map(SENT, 0x1000, UC_PROT_ALL); emu._sent = True
    def on_rte(uc, addr, size, _):                     # Unicorn faults on rte: do the 68000 pop by hand
        sp = uc.reg_read(UC_M68K_REG_A7)
        sr = struct.unpack(">H", bytes(uc.mem_read(sp, 2)))[0]
        pc = struct.unpack(">I", bytes(uc.mem_read(sp + 2, 4)))[0]
        uc.reg_write(UC_M68K_REG_A7, sp + 6); uc.reg_write(UC_M68K_REG_SR, sr); uc.reg_write(UC_M68K_REG_PC, pc)
    emu.uc.hook_add(UC_HOOK_CODE, on_rte, None, 0x01B7F2, 0x01B7F2)   # the VBlank handler's rte

def call(emu, addr, d0=None, d1=None):
    """Run the game routine at addr (as jsr would) until it returns."""
    _ensure_sentinel(emu)
    for h in emu._hooks: emu.uc.hook_del(h)
    emu._hooks = []
    sp = (emu.uc.reg_read(UC_M68K_REG_A7) - 4) & 0xFFFFFFFF
    emu.uc.mem_write(sp, struct.pack(">I", SENT)); emu.uc.reg_write(UC_M68K_REG_A7, sp)
    if d0 is not None: emu.uc.reg_write(UC_M68K_REG_D0, d0)
    if d1 is not None: emu.uc.reg_write(UC_M68K_REG_D1, d1)
    emu.uc.emu_start(addr, SENT, count=50_000_000)

def frame(emu):
    """One vertical blank: the level-6 interrupt (exception frame, then the handler at 0x1A270)."""
    _ensure_sentinel(emu)
    for h in emu._hooks: emu.uc.hook_del(h)
    emu._hooks = []
    sr = emu.uc.reg_read(UC_M68K_REG_SR) & 0xFFFF
    sp = (emu.uc.reg_read(UC_M68K_REG_A7) - 6) & 0xFFFFFFFF
    emu.uc.mem_write(sp, struct.pack(">HI", sr, SENT)); emu.uc.reg_write(UC_M68K_REG_A7, sp)
    emu.uc.reg_write(UC_M68K_REG_SR, (sr & 0xF8FF) | 0x2600)
    emu.uc.emu_start(0x01A270, SENT, count=50_000_000)

def music_snapshots_wide(rom, cache=None):
    """Many more driver states, for the song-command handlers: every song 0x00-0x12 played
    for 240 frames with a snapshot every 24 frames, and every effect 0x01-0x5F after 1 and 4
    frames. Cached (pickle) when cache is a path."""
    import os, pickle
    if cache and os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    out = []
    for song in range(0x00, 0x13):
        e = Emu(rom); e.boot_to_level(0, 1); call(e, 0x01F664, d1=song)
        for f in range(1, 241):
            frame(e)
            if f % 24 == 0: out.append((f"song {song:#x} frame {f}", e.ram()))
    for sfx in range(0x01, 0x60):
        e = Emu(rom); e.boot_to_level(0, 1); call(e, 0x01F616, d1=sfx)
        for f in range(1, 5):
            frame(e)
            if f in (1, 4): out.append((f"sfx {sfx:#x} frame {f}", e.ram()))
    if cache: pickle.dump(out, open(cache, "wb"))
    return out

def music_snapshots(rom):
    """RAM with the sound driver busy: songs started through the game's own request routine
    (0x1F664, as the game does), sound effects through play_sfx (0x1F616), then a varying
    number of frames. Returns [(label, ram bytes)]."""
    out = []
    plan = [("song", s, n) for s, n in ((0x0A, 3), (0x0A, 30), (0x06, 12), (0x07, 60), (0x08, 25), (0x09, 90),
                                         (0x0B, 8), (0x0C, 40), (0x0D, 15), (0x0E, 50), (0x0F, 20), (0x10, 70),
                                         (0x11, 10), (0x12, 35), (0x05, 18), (0x00, 6))]
    plan += [("sfx", s, n) for s, n in ((0x05, 2), (0x10, 4), (0x1E, 3), (0x2B, 6), (0x40, 2), (0x50, 5))]
    for kind, sid, frames in plan:
        e = Emu(rom); e.boot_to_level(0, 1)
        if kind == "song": call(e, 0x01F664, d1=sid)
        else: call(e, 0x01F616, d1=sid)
        for _ in range(frames): frame(e)
        out.append((f"{kind} {sid:#x} after {frames} frames", e.ram()))
    return out
