#!/usr/bin/env python3
"""
extract_levels.py -- dump every Gauntlet 4 level map, decoded by the C
translation of the game's own decompressor (c/g4_level.c, verified
bit-exact against the ROM by c/test_level.py).

Usage: python3 extract_levels.py <rom.bin> <libg4.so> <out_dir>

Writes
  <out_dir>/levels.json   one entry per level-table slot:
      table      "arcade" (0x25BC4, used when $ffe790 is set),
                 "arcade_alt" (0x25DC4, $ffe790 clear) or
                 "quest_battle" (0x25FC4, level numbers 0x81-0xC1)
      index      slot in that table; level number = index+1 (arcade) or 0x81+index
      record     ROM address of the record
      header     bytes 0-7 (byte 7 = option flags)
      nibbles    record bytes 8-9 as [hi, lo, hi, lo] -> $ffe7dc, $ffe7e0, $ffe7de, $ffe7e2
      palette    record bytes 10-13 (P0-P3)
      raw        32x32 cells as decoded, before the remap (values 0-63), row-major
      remap      the remap table the game uses for this level: arcade levels and Battle arenas
                 (0x81-0x8D) "arcade"; Quest hub 0x8E "quest_e842"; Quest stages 0x8F+ "quest"
  plus "remap_tables": the four 64-byte tables (0x31D2E arcade/battle/record,
  0x31D6E quest, 0x31DAE quest with $ffe842 = 1, 0x31DEE quest with $ffe875
  set or $ffe846 > 10). Final cell code = remap_tables[name][raw].
  <out_dir>/levels.txt    the same maps as hex grids (after the default remap)
"""
import ctypes, json, os, struct, sys

rom_path, lib_path, out_dir = sys.argv[1:4]
ROM = open(rom_path, "rb").read()
lib = ctypes.CDLL(os.path.abspath(lib_path))

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
ctypes.c_int.in_dll(lib, "g4_use_translated").value = 1
def no_ext(cp, t): raise SystemExit(f"unexpected call to untranslated code {t:#x}")
CBS = (EXT_CB(no_ext), RD_CB(lambda a, s: 0), WR_CB(lambda a, v, s: None))
lib.g4_test_set(*CBS)

MAP, SP = 0xFFE246, 0xFFFD00
TABLES = {"arcade": 0x31D2E, "quest": 0x31D6E, "quest_e842": 0x31DAE, "quest_late": 0x31DEE}
L = lambda a: struct.unpack(">I", ROM[a:a + 4])[0]

def wr(addr, data): ctypes.memmove(ctypes.addressof(MEM) + addr, bytes(data), len(data))
def rd(addr, n): return ctypes.string_at(ctypes.addressof(MEM) + addr, n)

def decode_raw(record):
    """Run map_decode with the arcade remap table temporarily set to identity
    (mode 0), so the map comes out as decoded, before the remap."""
    saved = rd(TABLES["arcade"], 64)
    wr(TABLES["arcade"], range(64))
    wr(0xFFE838, b"\0\0")                                 # mode 0 -> table 0x31D2E
    wr(SP - 4, struct.pack(">I", record))                 # the caller's move.l ($ffe75c).l,-(SP)
    cpu = M68K(); cpu.a[4] = MAP; cpu.a[7] = SP - 4
    assert lib.g4_test_run(ctypes.byref(cpu), 0x019122) == 0
    grid = rd(MAP, 0x400)
    wr(TABLES["arcade"], saved)
    return list(grid)

def level_ptr(level, alt):
    """The game's own lookup (C translation of 0x40D8)."""
    wr(0xFFE758, struct.pack(">H", level)); wr(0xFFE790, b"\0" if alt else b"\xff"); wr(0xFFE799, b"\0")
    cpu = M68K(); cpu.a[7] = SP
    assert lib.g4_test_run(ctypes.byref(cpu), 0x0040D8) == 0
    return struct.unpack(">I", rd(0xFFE75C, 4))[0]

slots = []
for table, alt, levels, remap in (("arcade", False, range(1, 129), "arcade"),
                                  ("arcade_alt", True, range(1, 129), "arcade"),
                                  ("quest_battle", False, range(0x81, 0x81 + 65), "quest")):
    for lv in levels:
        rec = level_ptr(lv, alt)
        slots.append(dict(table=table, index=lv - (1 if lv < 0x81 else 0x81), level=lv, record=rec,
                          header=list(ROM[rec:rec + 8]),
                          nibbles=[ROM[rec + 8] >> 4, ROM[rec + 8] & 15, ROM[rec + 9] >> 4, ROM[rec + 9] & 15],
                          palette=list(ROM[rec + 10:rec + 14]), raw=decode_raw(rec),
                          remap=remap if lv < 0x81 else "arcade" if lv < 0x8E else "quest_e842" if lv == 0x8E else "quest"))

os.makedirs(out_dir, exist_ok=True)
json.dump({"rom": os.path.basename(rom_path), "levels": slots,
           "remap_tables": {k: list(ROM[v:v + 64]) for k, v in TABLES.items()}},
          open(os.path.join(out_dir, "levels.json"), "w"))
with open(os.path.join(out_dir, "levels.txt"), "w") as f:
    for s in slots:
        t = ROM[TABLES[s["remap"]]:TABLES[s["remap"]] + 64]
        f.write(f"== {s['table']} [{s['index']}]  level {s['level']:#x}  record {s['record']:#07x}  "
                f"header {bytes(s['header']).hex(' ')}  palette {bytes(s['palette']).hex(' ')}  remap {s['remap']}\n")
        for y in range(32):
            f.write(" ".join(f"{t[c]:02x}" for c in s["raw"][32 * y:32 * y + 32]) + "\n")
        f.write("\n")
uniq = len({s["record"] for s in slots})
print(f"{len(slots)} table slots, {uniq} distinct level records -> {out_dir}/levels.json, levels.txt")
