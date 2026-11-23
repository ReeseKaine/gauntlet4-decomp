#!/usr/bin/env python3
"""
lift_all.py -- lift every remaining game routine, in address-range groups.

Entries: Ghidra's function list (functions_full.tsv) minus routines already
translated by hand or generated, and minus the unreferenced old copy of the
sound driver (0x39E1C-0x3B400). Each group gets --closure over its own range,
so routines Ghidra missed but that are called from it are lifted too; calls
between groups go through the dispatcher.
"""
import csv, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
GROUPS = [("game_00", 0x000400, 0x004000), ("game_04", 0x004000, 0x008000), ("game_08", 0x008000, 0x00C000),
          ("game_0c", 0x00C000, 0x010000), ("game_10", 0x010000, 0x014000), ("game_14", 0x014000, 0x018000),
          ("game_18", 0x018000, 0x01C000), ("game_1c", 0x01C000, 0x020000), ("game_20", 0x020000, 0x039E1C),
          ("game_fe", 0x0FE000, 0x0FF700)]
F = sorted(int(r["address"], 16) for r in csv.DictReader(open(os.path.join(HERE, "functions_full.tsv")), delimiter="\t"))
py = sys.executable
for name, lo, hi in GROUPS:
    entries = [f"{a:x}" for a in F if lo <= a < hi]
    if not entries: continue
    r = subprocess.run([py, "-I", os.path.join(HERE, "lift.py"), name, *entries, "--keep-going", f"--closure={lo:x}-{hi:x}"],
                       capture_output=True, text=True)
    print((r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else f"{name}: no output")
