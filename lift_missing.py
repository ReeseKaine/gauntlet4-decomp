#!/usr/bin/env python3
"""
lift_missing.py -- find code that generated C refers to but nothing has
translated, lift it, and repeat until nothing is missing.

Looks in every c/gen/*.c for
  * constant call / jump targets: JSR(c, ret, 0x..), g4_jmp(c, 0x..)
  * call and jump tables (from the instruction comments):
        movea.l $T(pc, dN.w), aN   ... jsr/jmp (aN)
        lea.l $T(pc), aN / lea.l $T.l, aN ; movea.l (aN, dX.w), aN ... jsr/jmp (aN)
    whose entries are read from the ROM while they look like code (max 128)
and lifts what has no C version yet into c/gen/extra_<n>.c (closure over the
whole ROM code area). Reports addresses that cannot be lifted.
"""
import glob, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import lift

ROM = lift.ROM
DEAD = (0x39E1C, 0x3B400)            # unreferenced old copy of the sound driver

def lifted():
    s = open(os.path.join(HERE, "c", "g4_dispatch.c")).read()
    out = {int(a, 16) for a in re.findall(r"\{ 0x([0-9A-F]{6}), g4_", s)}
    for f in glob.glob(os.path.join(HERE, "c", "gen", "*.c")):
        out |= {int(a, 16) for a in re.findall(r"^/\* ([0-9a-f]{6}) \*/", open(f).read(), re.M)}
    return out

def table(t):
    out = []
    for k in range(128):
        v = int.from_bytes(ROM[t + 4 * k:t + 4 * k + 4], "big")
        if not lift.plausible_code(v): break
        out.append(v)
    return out

def instruction_starts():
    out = set()
    for f in glob.glob(os.path.join(HERE, "c", "gen", "*.c")):
        out |= {int(a, 16) for a in re.findall(r"/\* ([0-9a-f]{6})  ", open(f).read())}
    return out

def candidates():
    found = {}
    starts = instruction_starts()
    for f in glob.glob(os.path.join(HERE, "c", "gen", "*.c")):
        src = open(f).read()
        for t in re.findall(r"JSR\(c, [^,]+, 0x([0-9A-F]+)u?\)", src): found.setdefault(int(t, 16), "call")
        for t in re.findall(r"g4_jmp\(c, 0x([0-9A-F]+)\)", src): found.setdefault(int(t, 16), "jump")
        # an address loaded as a constant (lea / movea.l #) that is an instruction start in lifted
        # code is a code pointer, called somewhere through a register (e.g. lea $fec8c.l,a6 ... jsr (a6))
        for t in re.findall(r"/\* [0-9a-f]{6}  (?:lea\.l \$([0-9a-f]+)(?:\(pc\)|\.l)|movea\.l #\$([0-9a-f]+)), a\d \*/", src):
            v = int(t[0] or t[1], 16)
            if v in starts: found.setdefault(v, "code pointer")
        for body in re.split(r"\n/\* [0-9a-f]{6} \*/\n", src):      # one function at a time
            asm = re.findall(r"/\* ([0-9a-f]{6})  ([^*]*?) \*/", body)
            base = {}                                          # register -> table address (lea)
            for k, (a, text) in enumerate(asm):
                tab = reg = None
                m = re.match(r"movea\.l \$([0-9a-f]+)\(pc, d\d\.w\), (a\d)$", text)
                if m: tab, reg = int(m.group(1), 16), m.group(2)
                m = re.match(r"(?:lea\.l \$([0-9a-f]+)(?:\(pc\)|\.l)|movea\.l #\$([0-9a-f]+)), (a\d)$", text)
                if m: base[m.group(3)] = int(m.group(1) or m.group(2), 16); continue

                m = re.match(r"movea\.l \((a\d), d\d\.w\), (a\d)$", text)
                if m and m.group(1) in base: tab, reg = base[m.group(1)], m.group(2)
                if tab is None: continue
                follow = [x[1] for x in asm[k + 1:k + 5]]
                if any(re.match(rf"(jsr|jmp) \({reg}\)$", x) for x in follow):
                    for v in table(tab): found.setdefault(v, f"table {tab:06x}")
    return found

rnd = 0
while True:
    have = lifted()
    want = {a: why for a, why in candidates().items()
            if a not in have and 0x400 <= a < len(ROM) and not (DEAD[0] <= a < DEAD[1])}
    if not want: break
    rnd += 1
    print(f"round {rnd}: {len(want)} missing, e.g. " + ", ".join(f"{a:06x} ({w})" for a, w in list(want.items())[:6]))
    r = subprocess.run([sys.executable, "-I", os.path.join(HERE, "lift.py"), f"extra_{rnd}", *[f"{a:x}" for a in sorted(want)],
                        "--keep-going", "--closure=400-100000"], capture_output=True, text=True)
    print("  " + (r.stdout + r.stderr).strip().splitlines()[-1])
    if not (lifted() - have):
        print("  nothing new could be lifted; still missing:", " ".join(f"{a:06x}" for a in sorted(want)))
        break
    if rnd > 20: print("giving up after 20 rounds"); break
print("nothing missing" if rnd == 0 or not want else "")
