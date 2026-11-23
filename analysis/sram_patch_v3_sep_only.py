#!/usr/bin/env python3
"""
Gauntlet 4 (Mega Drive, 1993-09 build) -- battery-backed SRAM save for passwords.

Usage:  python3 sram_patch.py <input.bin> <output.bin>

The game has two password systems; both are hooked:
  System A  Quest / Battle mode   30 chars, name +0x16f, password +0x17f
  System B  Record mode           32 chars, name +0x1c3, password +0x1d3

What it does
  * Declares 8-bit SRAM on odd bytes at 0x200001-0x2003FF in the ROM header.
  * SAVE: each time the game generates a password for an active player
    (+0xd0 != 0) with a non-empty name, the name and password are written to
    the SRAM slot for that system + character class (+0xb4: 0 THOR, 1 THYRA,
    2 MERLIN, 3 QUESTOR).
  * LOAD: when a player answers YES to "Enter password?", their password buffer
    is empty, and the slot for their system + class holds the same name, the
    saved password is pre-filled. Pressing Start loads it through the game's
    own decoder, so a corrupt save is rejected by the game's checksum like a
    mistyped password.
  * Recomputes the header checksum.

SRAM slot layout (logical bytes; physical address = 0x200001 + slot*0x80 + 2*n)
  slots 0-3: System A by class      slots 4-7: System B by class
  0     'G'          magic (written last)
  1     '4'
  2     version      0x01 = System A, 0x02 = System B
  4-19  name (16 bytes; NUL-terminated, max 10 chars)
  20-   password (30 chars for A, 32 for B)
"""
import struct, sys

CODE_BASE = 0xFC000           # inside the zero-filled gap 0xF98C0-0xFDFFF

# ---------------------------------------------------------------- tiny assembler
class Asm:
    def __init__(self, base):
        self.base, self.out, self.labels, self.fix = base, bytearray(), {}, []
    def pc(self): return self.base + len(self.out)
    def label(self, n): self.labels[n] = self.pc()
    def w(self, *words):
        for x in words: self.out += struct.pack(">H", x & 0xFFFF)
    def l(self, x): self.out += struct.pack(">I", x)
    def bcc8(self, op, lbl):            # Bcc.s  (op = 0x60 bra, 0x62 bhi, 0x66 bne, 0x67 beq)
        self.fix.append(("b8", len(self.out), lbl)); self.w(op << 8)
    def dbf(self, dreg, lbl):           # DBF Dn,label
        self.w(0x51C8 | dreg); self.fix.append(("d16", len(self.out), lbl)); self.w(0)
    def resolve(self):
        for kind, off, lbl in self.fix:
            tgt = self.labels[lbl]
            if kind == "b8":
                disp = tgt - (self.base + off + 2)
                assert -128 <= disp <= 127 and disp != 0, (lbl, disp)
                self.out[off + 1] = disp & 0xFF
            else:
                disp = tgt - (self.base + off)
                self.out[off:off + 2] = struct.pack(">h", disp)
        return bytes(self.out)

BHI, BNE, BEQ, BRA = 0x62, 0x66, 0x67, 0x60
JSR_ABS, RTS, NOP = 0x4EB9, 0x4E75, 0x4E71
MOVEM_SAVE = (0x48E7, 0xC070)       # movem.l d0-d1/a1-a3,-(sp)
MOVEM_REST = (0x4CDF, 0x0E03)       # movem.l (sp)+,d0-d1/a1-a3

def slot_addr(a, slot_base, exit_lbl):
    """d0 = class (+0xb4); exit if >3; a1 = 0x200001 + (slot_base + d0)*0x80"""
    a.w(0x3028, 0x00B4)             # move.w  $b4(a0),d0
    a.w(0x0C40, 0x0003)             # cmpi.w  #3,d0
    a.bcc8(BHI, exit_lbl)           # bhi.s   exit
    if slot_base:
        assert 1 <= slot_base <= 8
        a.w(0x5040 | ((slot_base & 7) << 9))   # addq.w #slot_base,d0
    a.w(0xEF48)                     # lsl.w   #7,d0
    a.w(0x43F9); a.l(0x00200001)    # lea     $200001,a1
    a.w(0xD2C0)                     # adda.w  d0,a1

def save_hook(a, p, tag):
    """Replaces 'bsr.b encode ; move.w #1,(flag).l' -- runs both, then saves."""
    done = tag + "_done"; loop = tag + "_loop"
    p["SAVE_HOOK"] = a.pc()
    a.w(JSR_ABS); a.l(p["encode"])                    # jsr encode           (displaced)
    a.w(0x33FC, 0x0001); a.l(p["gen_flag"])           # move.w #1,(flag).l   (displaced)
    a.w(*MOVEM_SAVE)
    # Only real, active players: 0x8164 calls pw_generate for all 4 structs,
    # including players who never joined (zeroed RAM: class 0 = THOR, empty name).
    a.w(0x4A68, 0x00D0)                               # tst.w  $d0(a0)
    a.bcc8(BEQ, done)
    a.w(0x4A28, p["name"])                            # tst.b  name(a0)
    a.bcc8(BEQ, done)
    slot_addr(a, p["slot_base"], done)
    a.w(0x4211)                                       # clr.b  (a1)          invalidate magic first
    a.w(0x45E8, p["name"])                            # lea    name(a0),a2   name then password (contiguous)
    a.w(0x47E9, 0x0008)                               # lea    8(a1),a3      logical byte 4
    a.w(0x7200 | (16 + p["pw_len"] - 1))              # moveq  #n-1,d1
    a.label(loop)
    a.w(0x169A)                                       # move.b (a2)+,(a3)
    a.w(0x548B)                                       # addq.l #2,a3
    a.dbf(1, loop)
    a.w(0x137C, p["version"], 0x0004)                 # move.b #ver,4(a1)
    a.w(0x137C, 0x0034, 0x0002)                       # move.b #'4',2(a1)
    a.w(0x12BC, 0x0047)                               # move.b #'G',(a1)    magic last
    a.label(done)
    a.w(*MOVEM_REST)
    a.w(RTS)

def load_hook(a, p, tag):
    """Replaces the YES path of the "Enter password?" prompt; runs the displaced
    instructions, pre-fills the password if possible, then calls the redraw."""
    done, cmp_, match, copy = (tag + s for s in ("_done", "_cmp", "_match", "_copy"))
    p["LOAD_HOOK"] = a.pc()
    for words in p["load_pre"]: a.w(*words)           # displaced clears
    a.w(*MOVEM_SAVE)
    a.w(0x4A28, p["pw"])                              # tst.b  pw(a0)        RAM already has a password?
    a.bcc8(BNE, done)                                 #                      -> keep it
    slot_addr(a, p["slot_base"], done)
    a.w(0x0C11, 0x0047)                               # cmpi.b #'G',(a1)
    a.bcc8(BNE, done)
    a.w(0x0C29, 0x0034, 0x0002)                       # cmpi.b #'4',2(a1)
    a.bcc8(BNE, done)
    a.w(0x0C29, p["version"], 0x0004)                 # cmpi.b #ver,4(a1)
    a.bcc8(BNE, done)
    a.w(0x45E9, 0x0008)                               # lea    8(a1),a2      saved name
    a.w(0x47E8, p["name"])                            # lea    name(a0),a3   typed name
    a.w(0x720A)                                       # moveq  #10,d1        up to 11 bytes incl. NUL
    a.label(cmp_)
    a.w(0x1012)                                       # move.b (a2),d0
    a.w(0xB01B)                                       # cmp.b  (a3)+,d0
    a.bcc8(BNE, done)                                 #                      names differ
    a.w(0x548A)                                       # addq.l #2,a2
    a.w(0x4A00)                                       # tst.b  d0
    a.bcc8(BEQ, match)                                #                      both hit NUL
    a.dbf(1, cmp_)
    a.bcc8(BRA, done)                                 # no NUL within 11 bytes: invalid
    a.label(match)
    a.w(0x45E9, 0x0028)                               # lea    40(a1),a2     logical byte 20
    a.w(0x47E8, p["pw"])                              # lea    pw(a0),a3
    a.w(0x7200 | (p["pw_len"] - 1))                   # moveq  #len-1,d1
    a.label(copy)
    a.w(0x16D2)                                       # move.b (a2),(a3)+
    a.w(0x548A)                                       # addq.l #2,a2
    a.dbf(1, copy)
    a.w(0x4213)                                       # clr.b  (a3)          terminator
    a.w(0x317C, p["pw_len"] - 1, p["pw_lenfield"])    # move.w #len-1,lenfield(a0)  (as the generator does)
    a.w(JSR_ABS); a.l(p["sync"])                      # jsr    sync_cursor
    a.label(done)
    a.w(*MOVEM_REST)
    a.w(JSR_ABS); a.l(p["redraw"])                    # jsr    redraw        (displaced bsr.w)
    a.w(RTS)

SYSTEMS = {
    "A": dict(  # Quest / Battle
        version=1, slot_base=0, name=0x16F, pw=0x17F, pw_len=30, pw_lenfield=0x15E,
        encode=0x12986, gen_flag=0xFFE840, sync=0x123E6, redraw=0x12612,
        save_site=0x12942, orig_save="614233fc000100ffe840",
        load_site=0x11E88, orig_load="4228006061000784",
        load_pre=[(0x4228, 0x0060)],                                   # clr.b $60(a0)
    ),
    "B": dict(  # Record
        version=2, slot_base=4, name=0x1C3, pw=0x1D3, pw_len=32, pw_lenfield=0x1BA,
        encode=0x183E6, gen_flag=0xFFE968, sync=0x180E4, redraw=0x18158,
        save_site=0x183CE, orig_save="611633fc000100ffe968",
        load_site=0x17D38, orig_load="42280060423900ffe97061000414",
        load_pre=[(0x4228, 0x0060), (0x4239, 0x00FF, 0xE970)],        # clr.b $60(a0) ; clr.b ($ffe970).l
    ),
}

a = Asm(CODE_BASE)
for tag, p in SYSTEMS.items():
    save_hook(a, p, "s" + tag)
    load_hook(a, p, "l" + tag)
CODE = a.resolve()

def site_patch(target, length):
    b = struct.pack(">HI", JSR_ABS, target)
    assert (length - len(b)) % 2 == 0
    return b + struct.pack(">H", NOP) * ((length - len(b)) // 2)

def patch(rom: bytearray):
    assert len(rom) == 0x100000, "expected the 1 MB 1993-09 ROM"
    for tag, p in SYSTEMS.items():
        for kind in ("save", "load"):
            site, orig = p[kind + "_site"], bytes.fromhex(p["orig_" + kind])
            assert rom[site:site + len(orig)] == orig, f"System {tag} {kind} site bytes differ (wrong ROM or already patched?)"
    assert not any(rom[CODE_BASE - 0x100:CODE_BASE + len(CODE) + 0x100]), "code area not empty"
    assert rom[0x1B0:0x1BC] == b" " * 12, "header SRAM field not blank"

    rom[CODE_BASE:CODE_BASE + len(CODE)] = CODE
    for p in SYSTEMS.values():
        for kind in ("save", "load"):
            site, n = p[kind + "_site"], len(bytes.fromhex(p["orig_" + kind]))
            rom[site:site + n] = site_patch(p[kind.upper() + "_HOOK"], n)
    rom[0x1B0:0x1BC] = b"RA" + bytes([0xF8, 0x20]) + struct.pack(">II", 0x00200001, 0x002003FF)
    ck = sum(struct.unpack(">%dH" % ((len(rom) - 0x200) // 2), rom[0x200:])) & 0xFFFF
    rom[0x18E:0x190] = struct.pack(">H", ck)
    return rom

if __name__ == "__main__":
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    rom = bytearray(open(sys.argv[1], "rb").read())
    patch(rom)
    open(sys.argv[2], "wb").write(rom)
    for tag, p in SYSTEMS.items():
        print(f"System {tag}: SAVE_HOOK @ {p['SAVE_HOOK']:#x}, LOAD_HOOK @ {p['LOAD_HOOK']:#x}")
    print(f"{len(CODE)} bytes of code; header checksum now {struct.unpack('>H', rom[0x18E:0x190])[0]:#06x}")
