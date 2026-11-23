#!/usr/bin/env python3
"""cdis.py START END [ROM] -- linear capstone disassembly of a ROM range."""
import os, sys
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
rom = open(sys.argv[3] if len(sys.argv) > 3 else os.environ.get('G4ROM', os.path.expanduser('~/GAMEPROJECT/gauntlet4-199309.bin')), 'rb').read()
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
for i in md.disasm(rom[lo:hi], lo):
    print('%06x %-20s %s %s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str))
