import struct, sys
from unicorn import Uc, UC_ARCH_M68K, UC_MODE_BIG_ENDIAN, UC_PROT_ALL
from unicorn.m68k_const import *

ROM = open(sys.argv[1], "rb").read()
SHIFT = {b"1993.SEP": 0x00, b"1993.AUG": 0x22}[ROM[0x118:0x120]]   # Aug routines sit 0x22 higher
def R(x): return x + SHIFT                                              # ROM address in this build
print("build", ROM[0x118:0x120].decode())
P0 = 0xFF0040                      # player struct 0
SENTINEL = 0xFFFE00                # return address that stops emulation
WORK = 0xFFF000                    # 64-byte work buffer for A4

def new_cpu():
    uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
    try: uc.ctl_set_cpu_model(UC_CPU_M68K_M68000)
    except Exception: pass
    uc.mem_map(0x000000, 0x100000, UC_PROT_ALL); uc.mem_write(0, ROM)
    uc.mem_map(0x200000, 0x1000, UC_PROT_ALL)            # SRAM
    uc.mem_map(0xFF0000, 0x10000, UC_PROT_ALL)           # work RAM
    uc.mem_write(R(0x12612), b"\x4e\x75")                   # stub: redraw routine (touches VDP)
    return uc

def call(uc, addr, a0=P0, until=SENTINEL):
    sp = 0xFFFD00
    uc.mem_write(sp, struct.pack(">I", SENTINEL))
    uc.reg_write(UC_M68K_REG_A7, sp)
    uc.reg_write(UC_M68K_REG_A0, a0)
    uc.reg_write(UC_M68K_REG_A4, WORK)
    uc.emu_start(addr, until, count=2_000_000)
    return (uc.reg_read(UC_M68K_REG_SR) >> 2) & 1         # Z flag

def wb(uc, a, v): uc.mem_write(a, bytes([v & 0xFF]))
def ww(uc, a, v): uc.mem_write(a, struct.pack(">H", v & 0xFFFF))
def wl(uc, a, v): uc.mem_write(a, struct.pack(">I", v))
def rb(uc, a, n=1): return bytes(uc.mem_read(a, n))
def rw(uc, a): return struct.unpack(">H", rb(uc, a, 2))[0]
def rl(uc, a): return struct.unpack(">I", rb(uc, a, 4))[0]
def sram_logical(uc, slot, n=50):
    return bytes(rb(uc, 0x200001 + slot * 0x80 + 2 * i)[0] for i in range(n))

# Fields covered by the password, with test values inside each field's range
FIELDS = [("l",0x134,12300),("w",0x96,1),("w",0x98,0),("w",0x9a,1),("w",0x9c,1),("w",0x9e,0),("w",0xa0,1),
          ("b",0x16d,9),("l",0x124,0xBEEF),("l",0x128,0x1234),
          ("w",0x2a0,7),("w",0x2a2,3),("w",0x2a4,5),("w",0x2a6,0),("w",0x2a8,2),("w",0x2aa,6),
          ("w",0x298,4),("w",0x29a,2),("w",0x29c,1),("w",0x29e,1),("b",0x166,42),
          ("w",0xcc,3),("w",0xce,4),("b",0x1a2,5),("l",0x20c,754*60),("l",0x1fc,98765),
          ("l",0x200,77),("w",0xf0,2),("w",0x14a,13)]

def setup_player(uc, name, cls):
    for t,off,v in FIELDS: {"b":wb,"w":ww,"l":wl}[t](uc, P0+off, v)
    ww(uc, P0+0xb4, cls)
    ww(uc, P0+0xd0, 1)                                   # active player
    uc.mem_write(P0+0x16f, name.encode() + b"\0" * (16 - len(name)))

def read_fields(uc): return [( {"b":lambda a:rb(uc,a)[0],"w":lambda a:rw(uc,a),"l":lambda a:rl(uc,a)}[t](P0+off) ) for t,off,_ in FIELDS]

ok = True
def check(cond, msg):
    global ok; ok &= bool(cond); print(("PASS " if cond else "FAIL ") + msg)

# 1. SAVE: real pw_generate (0x1292e) -> pw_encode -> SAVE_HOOK
uc = new_cpu(); setup_player(uc, "MERLIN", 2)
call(uc, R(0x1292e))
pw = rb(uc, P0+0x17f, 30).decode()
print("generated password:", pw)
s = sram_logical(uc, 2)
check(s[:3] == b"G4\x01", "slot 2 magic/version written")
check(s[4:11] == b"MERLIN\0", "slot 2 name saved")
check(s[20:50].decode() == pw, "slot 2 password matches generated password")
check(rw(uc, P0+0x15e) == 29, "pw_generate still sets len=29 after hook")
check(all(b == 0 for b in sram_logical(uc, 0) + sram_logical(uc, 1) + sram_logical(uc, 3)), "other slots untouched")

# 2. LOAD (power-on: RAM password empty, same name) -> prefilled
uc2 = new_cpu(); uc2.mem_write(0x200000, rb(uc, 0x200000, 0x400))     # same SRAM, fresh RAM
uc2.mem_write(P0+0x16f, b"MERLIN\0"); ww(uc2, P0+0xb4, 2); wb(uc2, P0+0x60, 0xFF)
call(uc2, R(0x11e88), until=R(0x11e90))                                                    # patched site: jsr LOAD_HOOK ; nop
check(rb(uc2, P0+0x17f, 31) == pw.encode() + b"\0", "password prefilled from SRAM")
check(rw(uc2, P0+0x15e) == 29, "length set to 29")
check(rb(uc2, P0+0x60)[0] == 0, "displaced clr.b $60(a0) still executed")
idx = "0123456789ACDEFGHJKLMPRTUWXY+-=:".index(pw[29])
check((rw(uc2, P0+0x162), rw(uc2, P0+0x160)) == (idx // 8, idx % 8), "cursor synced onto last character")

# 3. DECODE the prefilled password with the game's own pw_decode -> all fields restored
z = call(uc2, R(0x12c08))
check(z == 1, "pw_decode accepts the restored password (Z=1)")
got, want = read_fields(uc2), [v for _,_,v in FIELDS]
want[FIELDS.index(("l",0x20c,754*60))] = 754*60
check(got == want, "all decoded fields equal the original values")
if got != want:
    for (t,off,v),g in zip(FIELDS,got):
        if g != v: print(f"   +{off:#x}: want {v} got {g}")

# 4. Negative cases
uc3 = new_cpu(); uc3.mem_write(0x200000, rb(uc, 0x200000, 0x400))
uc3.mem_write(P0+0x16f, b"MERLINA\0"); ww(uc3, P0+0xb4, 2); call(uc3, R(0x11e88), until=R(0x11e90))
check(rb(uc3, P0+0x17f)[0] == 0, "different name -> no prefill")
uc3 = new_cpu(); uc3.mem_write(0x200000, rb(uc, 0x200000, 0x400))
uc3.mem_write(P0+0x16f, b"MERLIN\0"); ww(uc3, P0+0xb4, 1); call(uc3, R(0x11e88), until=R(0x11e90))
check(rb(uc3, P0+0x17f)[0] == 0, "different class (slot 1 empty) -> no prefill")
uc3 = new_cpu(); uc3.mem_write(0x200000, rb(uc, 0x200000, 0x400))
uc3.mem_write(P0+0x16f, b"MERLIN\0"); ww(uc3, P0+0xb4, 2); uc3.mem_write(P0+0x17f, b"KEEPME\0"); call(uc3, R(0x11e88), until=R(0x11e90))
check(rb(uc3, P0+0x17f, 7) == b"KEEPME\0", "existing RAM password is kept")
uc3 = new_cpu(); uc3.mem_write(0x200000, rb(uc, 0x200000, 0x400))
wb(uc3, 0x200001 + 2*0x80 + 2*25, ord("0") if pw[5] != "0" else ord("1"))   # corrupt password char 5
uc3.mem_write(P0+0x16f, b"MERLIN\0"); ww(uc3, P0+0xb4, 2); call(uc3, R(0x11e88), until=R(0x11e90))
check(call(uc3, R(0x12c08)) == 0, "corrupted saved password is rejected by pw_decode (Z=0)")
uc3 = new_cpu(); setup_player(uc3, "THOR", 0); wb(uc3, 0xFFE866, 0xFF); call(uc3, R(0x1292e))
check(all(b == 0 for b in sram_logical(uc3, 0)), "no save when password generation is disabled (ffe866)")
uc3 = new_cpu(); setup_player(uc3, "THOR", 7); call(uc3, R(0x1292e))
check(all(b == 0 for b in rb(uc3, 0x200000, 0x1000)), "out-of-range class -> nothing written")

# 5. Regression: 0x8164 also runs pw_generate for structs of players who never joined
#    (zeroed RAM -> class 0 = THOR, empty name). They must not overwrite THOR's save.
P1 = P0 + 0x2c4
uc3 = new_cpu(); setup_player(uc3, "THOR", 0); call(uc3, R(0x1292e))
saved = sram_logical(uc3, 0)
call(uc3, R(0x1292e), a0=P1)                                              # inactive, zeroed struct
check(sram_logical(uc3, 0) == saved and saved[4:9] == b"THOR\0", "inactive empty struct does not overwrite THOR slot")
uc3 = new_cpu(); setup_player(uc3, "THOR", 0); ww(uc3, P0+0xd0, 0); call(uc3, R(0x1292e))
check(all(b == 0 for b in sram_logical(uc3, 0)), "inactive player with a name -> no save")
uc3 = new_cpu(); setup_player(uc3, "", 0); call(uc3, R(0x1292e))
check(all(b == 0 for b in sram_logical(uc3, 0)), "active player with empty name -> no save")


# ================= System B (Record mode, 32 chars, name +0x1c3, password +0x1d3) =================
FIELDS_B = [("w",0x1b0,100),("w",0x1b2,9),("w",0xd0,1),("l",0xb6,99999),("w",0xcc,5),("w",0xce,7),
            ("w",0x96,1),("w",0x98,0),("w",0x9a,1),("w",0x9c,1),("w",0x9e,0),("w",0xa0,1),
            ("w",0x1aa,12345),("l",0x204,0xBEEF),("l",0xc6,0x0ABCDEF),("l",0x20c,1234567),("w",0x1c0,6)]
GLOBALS_B = [(0xFFE758,99),(0xFFE75A,5),(0xFFE770,2),(0xFFE768,3),(0xFFE764,4)]
def setup_b(uc, name, cls):
    for t,off,v in FIELDS_B: {"b":wb,"w":ww,"l":wl}[t](uc, P0+off, v)
    for a,v in GLOBALS_B: ww(uc, a, v)
    ww(uc, P0+0xb4, cls); wl(uc, 0xFFE96C, P0)
    uc.mem_write(P0+0x1c3, name.encode() + b"\0" * (16 - len(name)))
def stubs_b(uc): uc.mem_write(R(0x18158), b"\x4e\x75")           # Record redraw touches the VDP

ub = new_cpu(); setup_b(ub, "THOR", 0); call(ub, R(0x183bc))       # Record password wrapper (A0 from 0xffe96c)
pwb = rb(ub, P0+0x1d3, 32).decode()
print("Record password:", pwb, len(pwb))
sb = sram_logical(ub, 4, 52)
check(sb[:3] == b"G4\x02", "B: slot 4 (THOR) magic/version 2 written")
check(sb[4:9] == b"THOR\0" and sb[20:52].decode() == pwb, "B: name and 32-char password saved")
check(all(b == 0 for i in (0,1,2,3,5,6,7) for b in sram_logical(ub, i, 52)), "B: no other slot touched (incl. Quest slot 0)")
check(rw(ub, P0+0x1ba) == 31, "B: wrapper still sets len=31 after hook")

ub2 = new_cpu(); stubs_b(ub2); ub2.mem_write(0x200000, rb(ub, 0x200000, 0x400))
ub2.mem_write(P0+0x1c3, b"THOR\0"); ww(ub2, P0+0xb4, 0); wb(ub2, P0+0x60, 0xFF); wb(ub2, 0xFFE970, 0xFF)
call(ub2, R(0x17d38), until=R(0x17d46))                                 # patched Record YES path
check(rb(ub2, P0+0x1d3, 33) == pwb.encode() + b"\0", "B: password prefilled from SRAM")
check(rw(ub2, P0+0x1ba) == 31, "B: length set to 31")
check(rb(ub2, P0+0x60)[0] == 0 and rb(ub2, 0xFFE970)[0] == 0, "B: displaced clears of $60(a0) and $ffe970 still executed")
z = call(ub2, R(0x18542))                                            # Record decoder
check(z == 1, "B: Record decoder accepts restored password (Z=1)")
gotb = [{"b":lambda a:rb(ub2,a)[0],"w":lambda a:rw(ub2,a),"l":lambda a:rl(ub2,a)}[t](P0+off) for t,off,_ in FIELDS_B]
check(gotb == [v for _,_,v in FIELDS_B] and [rw(ub2,a) for a,_ in GLOBALS_B] == [v for _,v in GLOBALS_B],
      "B: all decoded fields and global progress equal the originals")

ub3 = new_cpu(); stubs_b(ub3); ub3.mem_write(0x200000, rb(ub, 0x200000, 0x400))
ub3.mem_write(P0+0x1c3, b"THYRA\0"); ww(ub3, P0+0xb4, 0); call(ub3, R(0x17d38), until=R(0x17d46))
check(rb(ub3, P0+0x1d3)[0] == 0, "B: different name -> no prefill")
# Quest save for THOR must not satisfy a Record load for THOR (separate slots/versions), and vice versa
uq = new_cpu(); setup_player(uq, "THOR", 0); call(uq, R(0x1292e))
ub4 = new_cpu(); stubs_b(ub4); ub4.mem_write(0x200000, rb(uq, 0x200000, 0x400))
ub4.mem_write(P0+0x1c3, b"THOR\0"); ww(ub4, P0+0xb4, 0); call(ub4, R(0x17d38), until=R(0x17d46))
check(rb(ub4, P0+0x1d3)[0] == 0, "B: a Quest save does not prefill Record mode")
uq2 = new_cpu(); uq2.mem_write(0x200000, rb(ub, 0x200000, 0x400))
uq2.mem_write(P0+0x16f, b"THOR\0"); ww(uq2, P0+0xb4, 0); call(uq2, R(0x11e88), until=R(0x11e90))
check(rb(uq2, P0+0x17f)[0] == 0, "A: a Record save does not prefill Quest mode")

print("\nALL PASS" if ok else "\nSOME TESTS FAILED")
