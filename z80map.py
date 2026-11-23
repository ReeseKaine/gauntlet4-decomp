#!/usr/bin/env python3
"""z80map.py -- recursive-descent disassembly of the Gauntlet 4 Z80 program (ROM 0x3D470, 8 KB)."""
import os, re, sys
from z80dis import z80
ROM = open(sys.argv[1] if len(sys.argv) > 1 else os.environ.get("G4ROM", os.path.expanduser("~/GAMEPROJECT/gauntlet4-199309.bin")), "rb").read()
Z = ROM[0x3D470:0x3D470 + 0x2000]

def dec(pc):
    d = z80.decode(Z[pc:pc + 4], pc); return d, z80.disasm(d)

def walk(entries):
    seen = {}; work = list(entries)
    while work:
        pc = work.pop()
        while 0 <= pc < 0x2000 and pc not in seen:
            d, t = dec(pc); seen[pc] = (d.len, t)
            m = t.split()[0] if t else "?"
            tg = re.search(r"0x([0-9a-f]+)$", t)
            tgt = int(tg.group(1), 16) if tg else None
            if m in ("RET", "RETI", "RETN") and "," not in t: break
            if m == "JP" and "(" in t: break                    # JP (HL): computed
            if m in ("JP", "JR"):
                if tgt is not None: work.append(tgt)
                if "," not in t: break
            if m in ("CALL", "DJNZ", "RST"):
                if tgt is not None: work.append(tgt)
                if m == "RST": work.append(int(t.split()[1], 16))
            if m == "RET": pass                                 # conditional RET falls through
            pc += d.len
    return seen

if __name__ == "__main__":
    code = walk([0x0000, 0x0038, 0x0066])
    for pc in sorted(code): print(f"{pc:04x}  {Z[pc:pc + code[pc][0]].hex():<8}  {code[pc][1]}")
