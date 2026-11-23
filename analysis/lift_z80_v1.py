#!/usr/bin/env python3
"""
lift_z80.py -- translate the Gauntlet 4 Z80 sample player (Z80 RAM image at
ROM 0x3D470) to C: c/gen_z80/g4_z80.c, one statement per Z80 instruction,
annotated with address and instruction, flags exact (c/g4_z80.h).

Handles exactly the instructions this program uses; anything else stops the
generator. Its two self-modifying spots are translated as what they do:
  $01CD  JP nn whose target the program rewrites at $01CE -> jump to the word at $01CE
  $01A4-$01AC  nine bytes rewritten to LD (HL),L ($75) or LD (HL),A ($77) -> each
         reads its own opcode from RAM and does that
RET and the $01CD jump go through one dispatch switch over every instruction.

  python3 lift_z80.py [ROM]
"""
import os, re, sys
HERE = os.path.dirname(os.path.abspath(__file__))
sys.argv = [sys.argv[0]] + sys.argv[1:]
sys.path.insert(0, HERE)
import z80map as zm

R8 = {"A": "z->a", "B": "z->b", "C": "z->c", "D": "z->d", "E": "z->e", "H": "z->h", "L": "z->l"}
R16 = {"BC": "BC(z)", "DE": "DE(z)", "HL": "HL(z)", "SP": "z->sp", "IX": "z->ix"}
SET16 = {"BC": "SETBC(z, {})", "DE": "SETDE(z, {})", "HL": "SETHL(z, {})", "SP": "z->sp = (uint16_t)({})", "IX": "z->ix = (uint16_t)({})"}
CC = {"z": "(z->f & FZ)", "nz": "!(z->f & FZ)", "c": "(z->f & FC)", "nc": "!(z->f & FC)"}
SELFMOD_JP = 0x01CD
SELFMOD_LD = range(0x01A4, 0x01AD)

class Unhandled(Exception):
    pass

def num(t):
    return int(t, 0)

def src8(t):
    """C expression reading an 8-bit operand."""
    if t in R8: return R8[t]
    m = re.match(r"^\((BC|DE|HL)\)$", t)
    if m: return f"zrd({R16[m.group(1)]})"
    m = re.match(r"^\(IX([+-]\d+)\)$", t)
    if m: return f"zrd((uint16_t)(z->ix + {int(m.group(1))}))"
    m = re.match(r"^\((0x[0-9a-f]+)\)$", t)
    if m: return f"zrd(0x{num(m.group(1)):04X})"
    if re.match(r"^(0x[0-9a-f]+|\d+)$", t): return f"0x{num(t) & 0xFF:02X}"
    raise Unhandled(t)

def dst8(t, v):
    if t in R8: return f"{R8[t]} = (uint8_t)({v});"
    m = re.match(r"^\((BC|DE|HL)\)$", t)
    if m: return f"zwr({R16[m.group(1)]}, (uint8_t)({v}));"
    m = re.match(r"^\((0x[0-9a-f]+)\)$", t)
    if m: return f"zwr(0x{num(m.group(1)):04X}, (uint8_t)({v}));"
    raise Unhandled(t)

def translate(pc, size, text):
    nxt = pc + size
    if pc == SELFMOD_JP:
        return "zpc = (uint16_t)(zrd(0x01CE) | (zrd(0x01CF) << 8)); goto L_DISPATCH;   /* target rewritten at $01CE */"
    if pc in SELFMOD_LD:
        return (f"{{ uint8_t op = zrd(0x{pc:04X});   /* rewritten: $75 LD (HL),L or $77 LD (HL),A */\n"
                f"      if (op == 0x75) zwr(HL(z), z->l); else if (op == 0x77) zwr(HL(z), z->a); else {{ zpc = 0x{pc:04X}; goto L_BAD; }} }}")
    op, _, args = text.partition(" ")
    a = [x.strip() for x in args.split(",")] if args else []
    if op == "NOP": return ";"
    if op == "DI": return "z->iff = 0;"
    if op == "IM": return f"z->im = {a[0]};"
    if op == "EXX":
        return ("{ uint8_t t; t = z->b; z->b = z->b_; z->b_ = t; t = z->c; z->c = z->c_; z->c_ = t; "
                "t = z->d; z->d = z->d_; z->d_ = t; t = z->e; z->e = z->e_; z->e_ = t; "
                "t = z->h; z->h = z->h_; z->h_ = t; t = z->l; z->l = z->l_; z->l_ = t; }")
    if op == "LD":
        d, s = a
        if d in R16 and (s.startswith("0x") or s.isdigit()): return SET16[d].format(f"0x{num(s):04X}") + ";"
        m = re.match(r"^\((0x[0-9a-f]+)\)$", d)
        if m and s == "HL":
            n = num(m.group(1)); return f"zwr(0x{n:04X}, z->l); zwr(0x{n + 1:04X}, z->h);"
        return dst8(d, src8(s))
    if op == "INC":
        if a[0] in R16: return SET16[a[0]].format(f"{R16[a[0]]} + 1") + ";"
        return dst8(a[0], f"z_inc8(z, {src8(a[0])})")
    if op == "DEC":
        if a[0] in R16: return SET16[a[0]].format(f"{R16[a[0]]} - 1") + ";"
        raise Unhandled(text)
    if op == "ADD":
        if a[0] == "A": return f"z->a = z_add8(z, z->a, {src8(a[1])}, 0);"
        if a[0] == "HL": return f"SETHL(z, z_add16(z, HL(z), {R16[a[1]]}));"
        raise Unhandled(text)
    if op == "SBC" and a[0] == "HL": return f"SETHL(z, z_sbc16(z, HL(z), {R16[a[1]]}));"
    if op == "SUB": return f"z->a = z_sub8(z, z->a, {src8(a[0])}, 0);"
    if op == "AND": return f"z->a = z_and(z, (uint8_t)(z->a & {src8(a[0])}));"
    if op == "OR":  return f"z->a = z_or(z, (uint8_t)(z->a | {src8(a[0])}));"
    if op == "XOR": return f"z->a = z_or(z, (uint8_t)(z->a ^ {src8(a[0])}));"
    if op == "CP":  return f"z_cp(z, z->a, {src8(a[0])});"
    if op == "RRCA": return "z_rrca(z);"
    if op == "RLA": return "z_rla(z);"
    if op == "RL":  return dst8(a[0], f"z_rl(z, {src8(a[0])})")
    if op == "SRA": return dst8(a[0], f"z_sra(z, {src8(a[0])})")
    if op == "PUSH": return f"z_push(z, {R16[a[0]]});"
    if op == "POP": return SET16[a[0]].format("z_pop(z)") + ";"
    if op in ("JP", "JR"):
        if len(a) == 1: return f"goto L{num(a[0]):04X};"
        return f"if ({CC[a[0]]}) goto L{num(a[1]):04X};"
    if op == "DJNZ": return f"z->b = (uint8_t)(z->b - 1); if (z->b) goto L{num(a[0]):04X};"
    if op == "CALL":
        if len(a) == 1: return f"z_push(z, 0x{nxt:04X}); goto L{num(a[0]):04X};"
        raise Unhandled(text)
    if op == "RET":
        if not a: return "zpc = z_pop(z); goto L_DISPATCH;"
        raise Unhandled(text)
    raise Unhandled(text)

def terminal(pc, text):
    op, _, args = text.partition(" ")
    if pc == SELFMOD_JP: return True
    if op in ("JP", "JR") and "," not in args: return True
    if op == "RET" and not args: return True
    return False

def main():
    code = zm.walk([0x0000, 0x01E4, 0x01F4, 0x020F, 0x022E])
    addrs = sorted(code)
    out = ["/*", " * gen_z80/g4_z80.c -- GENERATED by lift_z80.py from the 1993-09 ROM (Z80 image at 0x3D470); do not edit.",
           " * The Gauntlet 4 Z80 sample player: one statement per Z80 instruction.", " */",
           '#include "../g4_z80.h"', "", "uint8_t  g4z80_ram[0x2000];", "uint16_t g4z80_bank;", "G4Z80IO  g4z80_io;", "",
           "void g4z80_run(Z80 *z)", "{", "    uint16_t zpc;"]
    for k, pc in enumerate(addrs):
        size, text = code[pc]
        out.append(f"L{pc:04X}:")
        out.append(f"    {{ ZT(0x{pc:04X}); {translate(pc, size, text)} }}  /* {pc:04x}  {text} */")
        nxt = pc + size
        if not terminal(pc, text) and (k + 1 >= len(addrs) or addrs[k + 1] != nxt):
            if nxt not in code: raise Unhandled(f"{pc:04x} falls through to {nxt:04x}")
            out.append(f"    goto L{nxt:04X};")
    out.append("L_DISPATCH:")
    out.append("    switch (zpc) {")
    out += [f"    case 0x{pc:04X}: goto L{pc:04X};" for pc in addrs]
    out.append("    default: break;")
    out.append("    }")
    out.append("L_BAD:")
    out.append("    if (g4z80_io.bad) g4z80_io.bad(z, zpc);   /* not code this translation knows */")
    out.append("}")
    d = os.path.join(HERE, "c", "gen_z80"); os.makedirs(d, exist_ok=True)
    open(os.path.join(d, "g4_z80.c"), "w").write("\n".join(out) + "\n")
    print(f"gen_z80/g4_z80.c: {len(addrs)} instructions")

if __name__ == "__main__":
    main()
