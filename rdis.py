#!/usr/bin/env python3
"""
rdis.py ENTRY [ENTRY...] [--rom ROM] [--local LO-HI]

Recursive-descent 68000 disassembly (capstone): follows branches from each
entry, never runs into inline data, and prints each routine's instructions in
address order with the routines it calls. With --local, call targets inside
LO-HI are disassembled as routines too.
"""
import argparse, os, re
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

ap = argparse.ArgumentParser()
ap.add_argument("entries", nargs="+")
ap.add_argument("--rom", default=os.environ.get("G4ROM", os.path.expanduser("~/GAMEPROJECT/gauntlet4-199309.bin")))
ap.add_argument("--local", default=None)
a = ap.parse_args()
ROM = open(a.rom, "rb").read()
md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
lo, hi = (int(x, 16) for x in a.local.split("-")) if a.local else (0, 0)

def one(addr):
    i = next(md.disasm(ROM[addr:addr + 12], addr), None)
    return i

def routine(entry):
    seen, work, calls, ends = {}, [entry], set(), set()
    while work:
        pc = work.pop()
        while pc not in seen:
            i = one(pc)
            if i is None: ends.add(pc); break
            seen[pc] = i
            m, op = i.mnemonic, i.op_str
            t = re.search(r"\$([0-9a-f]+)(?:\.[lw])?$", op)
            tgt = int(t.group(1), 16) if t else None
            nxt = pc + i.size
            if m in ("rts", "rte", "rtr"): break
            if m.startswith("jmp"):
                if tgt is not None and "(" not in op: work.append(tgt)
                break
            if m in ("bra", "bra.b", "bra.w", "bra.s"):
                if tgt is not None: work.append(tgt)
                break
            if m.startswith(("bsr", "jsr")):
                if tgt is not None and "(" not in op: calls.add(tgt)
                elif "(" in op: calls.add(("indirect", pc))
                pc = nxt; continue
            if (m.startswith("b") and not m.startswith(("bchg", "bclr", "bset", "btst"))) or m.startswith("db"):
                if tgt is not None: work.append(tgt)
            pc = nxt
    return seen, calls

done, queue = set(), [int(x, 16) for x in a.entries]
while queue:
    e = queue.pop(0)
    if e in done: continue
    done.add(e)
    ins, calls = routine(e)
    print(f"; ===== routine {e:06x}  ({len(ins)} instructions)")
    for pc in sorted(ins):
        i = ins[pc]
        print(f"{pc:06x}  {i.bytes.hex():<20} {i.mnemonic} {i.op_str}")
    ext = sorted(c for c in calls if isinstance(c, int))
    print(f"; calls: {' '.join('%06x' % c for c in ext)}" + ("  (+ indirect)" if any(not isinstance(c, int) for c in calls) else ""))
    for c in ext:
        if lo <= c < hi and c not in done: queue.append(c)
