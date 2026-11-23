#!/usr/bin/env python3
"""
lift.py -- translate Gauntlet 4 68000 routines to C in the register-model
style of c/g4_m68k.h: one statement block per instruction, each annotated
with its address and instruction, condition codes kept exactly (g4_ops.h).

  python3 lift.py GROUP ADDR[=name] [ADDR[=name] ...] [--rom=ROM] [--closure=LO-HI]

--keep-going skips routines already translated and records ones that fail to
lift in gen/GROUP.failed.txt instead of stopping.
--closure also lifts every routine called (with a constant target) from the
lifted code whose address is in LO-HI, recursively.

Writes c/gen/<GROUP>.c with one function per routine (g4_<name>, default
g4_sub_<addr>) and regenerates c/gen/g4_gen_table.inc / g4_gen_decls.inc
(the dispatcher's entries for every generated file). A routine's body is
found by recursive descent from its entry (branches followed, calls not);
calls go through JSR() / g4_jmp(), so callees may be C or external.
Routines already translated by hand (listed in c/g4_dispatch.c before the
include) are refused.
"""
import os, re, sys
from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000

HERE = os.path.dirname(os.path.abspath(__file__))
GEN = os.path.join(HERE, "c", "gen")
args = [a for a in sys.argv[1:] if not a.startswith("--")]
KEEP_GOING = "--keep-going" in sys.argv
CLOSURE = next((tuple(int(x, 16) for x in a.split("=", 1)[1].split("-")) for a in sys.argv[1:] if a.startswith("--closure=")), None)
rom_path = next((a.split("=", 1)[1] for a in sys.argv[1:] if a.startswith("--rom=")), os.environ.get("G4ROM", os.path.expanduser("~/GAMEPROJECT/gauntlet4-199309.bin")))
ROM = open(rom_path, "rb").read()
md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)

class LiftError(Exception):
    pass

def insn(pc):
    i = next(md.disasm(ROM[pc:pc + 12], pc), None)
    if i is None: raise LiftError(f"undecodable instruction at {pc:06x}")
    return i

# ------------------------------------------------------------------ discovery
BRANCH_CC = {"bra": "T", "bhi": "HI", "bls": "LS", "bcc": "CC", "bhs": "CC", "bcs": "CS", "blo": "CS",
             "bne": "NE", "beq": "EQ", "bvc": "VC", "bvs": "VS", "bpl": "PL", "bmi": "MI",
             "bge": "GE", "blt": "LT", "bgt": "GT", "ble": "LE"}
DB_CC = {"dbt": "T", "dbf": "F", "dbra": "F", "dbhi": "HI", "dbls": "LS", "dbcc": "CC", "dbcs": "CS",
         "dbne": "NE", "dbeq": "EQ", "dbvc": "VC", "dbvs": "VS", "dbpl": "PL", "dbmi": "MI",
         "dbge": "GE", "dblt": "LT", "dbgt": "GT", "dble": "LE"}
S_CC = {"st": "T", "sf": "F", "shi": "HI", "sls": "LS", "scc": "CC", "shs": "CC", "scs": "CS", "slo": "CS",
        "sne": "NE", "seq": "EQ", "svc": "VC", "svs": "VS", "spl": "PL", "smi": "MI",
        "sge": "GE", "slt": "LT", "sgt": "GT", "sle": "LE"}

def base(m): return m.split(".")[0]
def target_of(i):
    t = re.search(r"\$([0-9a-f]+)(?:\.[lw])?$", i.op_str)     # also "jmp $9918.l"
    return int(t.group(1), 16) if t else None

TABLES = {}        # entry -> {jmp pc: [targets]}
RETURNS = {}       # entry -> set of local continuation addresses pushed on the stack

def plausible_code(a):
    if a & 1 or not (0x400 <= a < len(ROM) - 4): return False
    try: insn(a); return True
    except LiftError: return False

def jump_table(prev, i):
    """jmp (An) right after movea.l $T(pc,Dn.w),An: the table's entries, up to the first non-code
    value or the first target placed right after the table."""
    if prev is None or not prev.mnemonic.startswith("movea.l"): return None
    m = re.match(r"^\$([0-9a-f]+)\(pc, d\d\.w\), (a\d)$", prev.op_str)
    if not m or i.op_str.strip() != f"({m.group(2)})": return None
    t = int(m.group(1), 16); out = []
    for k in range(128):
        a = t + 4 * k
        if out and a >= min([x for x in out if x > t] or [1 << 30]): break
        v = int.from_bytes(ROM[a:a + 4], "big")
        if not plausible_code(v): break
        out.append(v)
    return out or None

FOLLOW_PUSHED = True   # lift_unit.py turns this off: a single instruction is lifted alone

def pushed_continuation(i, entry):
    """move.l #R,-(a7) / move.l #R,(a7) with R code near the routine: a hand-made return address."""
    if not FOLLOW_PUSHED or not i.mnemonic.startswith("move.l"): return None
    m = re.match(r"^#\$([0-9a-f]+), (-\(a7\)|\(a7\))$", i.op_str)
    if not m: return None
    r = int(m.group(1), 16)
    return r if abs(r - entry) < 0x4000 and plausible_code(r) else None

def discover(entry):
    seen, work = {}, [entry]
    TABLES[entry] = {}; RETURNS[entry] = set()
    while work:
        pc = work.pop(); prev = None
        while pc not in seen:
            i = insn(pc); seen[pc] = i
            b = base(i.mnemonic)
            r = pushed_continuation(i, entry)
            if r is not None: RETURNS[entry].add(r); work.append(r)
            if b in ("rts", "rte", "rtr"): break
            if b == "jmp":
                t = target_of(i)
                if t is not None and "(" not in i.op_str and abs(t - entry) < 0x4000: work.append(t)
                else:
                    tab = jump_table(prev, i)
                    if tab: TABLES[entry][pc] = tab; work.extend(tab)
                break
            if b == "bra":
                work.append(target_of(i)); break
            if b in BRANCH_CC or b in DB_CC:
                work.append(target_of(i))
            prev = i
            pc += i.size
    # a jmp to an absolute address outside what we would otherwise reach is a tail call, not code
    return seen

# ------------------------------------------------------------------ operands
REG = re.compile(r"^([da])([0-7])$")
def regno(r):
    r = r.strip()
    if r == "sp": return ("a", 7)
    m = REG.match(r)
    if not m: raise LiftError(f"register {r!r}")
    return (m.group(1), int(m.group(2)))

def num(s):
    s = s.strip(); neg = s.startswith("-")
    if neg: s = s[1:]
    v = int(s[1:], 16) if s.startswith("$") else int(s, 0)
    return -v if neg else v

def split_ops(s):
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "(": depth += 1
        if ch == ")": depth -= 1
        if ch == "," and depth == 0: out.append(cur.strip()); cur = ""
        else: cur += ch
    if cur.strip(): out.append(cur.strip())
    return out

def reglist(s):
    mask = 0
    for part in s.split("/"):
        if "-" in part:
            a, b = part.split("-"); (ka, na), (kb, nb) = regno(a), regno(b)
            lo, hi = (0 if ka == "d" else 8) + na, (0 if kb == "d" else 8) + nb
            for k in range(lo, hi + 1): mask |= 1 << k
        else:
            k, n = regno(part); mask |= 1 << ((0 if k == "d" else 8) + n)
    return mask

class Op:
    """An operand: kind in dreg, areg, imm, mem, sr, ccr, usp, list; mem has an EA expression."""
    def __init__(self, text, sz):
        self.text, self.sz = text, sz
        t = text.strip()
        self.pre = []          # statements to run before the access (pre-decrement)
        self.post = []         # statements after (post-increment)
        if t in ("sr", "ccr", "usp"): self.kind = t; return
        if re.match(r"^[da][0-7]$|^sp$", t) and "/" not in t:
            k, n = regno(t); self.kind = "dreg" if k == "d" else "areg"; self.n = n; return
        if re.match(r"^([da][0-7](-[da][0-7])?)(/[da][0-7](-[da][0-7])?)*$", t):
            self.kind = "list"; self.mask = reglist(t); return
        if t.startswith("#"):
            self.kind = "imm"; self.val = num(t[1:]) & 0xFFFFFFFF; return
        self.kind = "mem"
        m = re.match(r"^\(([da][0-7]|sp)\)\+$", t)
        if m:
            _, n = regno(m.group(1)); self.n = n; self.mode = "postinc"; self.ea = f"c->a[{n}]"; return
        m = re.match(r"^-\(([da][0-7]|sp)\)$", t)
        if m:
            _, n = regno(m.group(1)); self.n = n; self.mode = "predec"; self.ea = f"c->a[{n}]"; return
        m = re.match(r"^(-?\$?[0-9a-f]+)?\(([^)]*)\)$", t)
        if m:
            disp = num(m.group(1)) if m.group(1) else 0
            parts = [p.strip() for p in m.group(2).split(",")]
            self.mode = "ind"
            if parts[0] == "pc":
                ea = f"0x{disp & 0xFFFFFF:06X}u"
            else:
                _, n = regno(parts[0]); ea = f"c->a[{n}]" + (f" + {disp:#x}u" if disp > 0 else f" - {(-disp):#x}u" if disp < 0 else "")
            if len(parts) > 1:
                ix = parts[1]; isz = "w"
                if "." in ix: ix, isz = ix.split(".")
                k, n = regno(ix); r = f"c->{'d' if k == 'd' else 'a'}[{n}]"
                ea += f" + sext16(W({r}))" if isz == "w" else f" + {r}"
            self.ea = ea; return
        m = re.match(r"^\$([0-9a-f]+)(\.[wl])?$", t)
        if m:
            v = int(m.group(1), 16)
            if m.group(2) == ".w" and v & 0x8000: v |= 0xFFFF0000
            self.mode = "abs"; self.ea = f"0x{v & 0xFFFFFFFF:06X}u"; return
        raise LiftError(f"operand {text!r}")

    def addr(self):
        """C expression for the effective address (memory operands); emits pre/post steps."""
        if self.mode == "predec":
            self.pre.append(f"c->a[{self.n}] -= {step_expr(self.n, self.sz)};")
            return f"c->a[{self.n}]"
        if self.mode == "postinc":
            self.post.append(f"c->a[{self.n}] += {step_expr(self.n, self.sz)};")
            return f"c->a[{self.n}]"
        return self.ea

def step_expr(n, sz): return str(2 if (n == 7 and sz == 1) else sz)

# ------------------------------------------------------------------ code generation
def lift_routine(entry, name, local_names):
    ins = discover(entry)
    addrs = sorted(ins)
    labels = set()
    for k, pc in enumerate(addrs):
        i = ins[pc]; b = base(i.mnemonic)
        if b in BRANCH_CC or b in DB_CC or b == "jmp":
            t = target_of(i)
            if t is not None and t in ins and "(" not in i.op_str: labels.add(t)
        for t in TABLES[entry].get(pc, []): labels.add(t)
    labels |= RETURNS[entry]
    for k, pc in enumerate(addrs):
        i = ins[pc]
        nxt = pc + i.size
        if not is_terminal(i) and (k + 1 >= len(addrs) or addrs[k + 1] != nxt):
            if nxt not in ins: raise LiftError(f"{pc:06x} falls through to {nxt:06x}, outside the routine")
            labels.add(nxt)
    if entry != addrs[0]: labels.add(entry)
    # computed jumps (jmp (An), any form): every instruction of the body is a possible target, as on
    # the CPU, so they all go through one dispatch switch at the end of the function
    dispatch = any(base(ins[pc].mnemonic) == "jmp" and "(" in ins[pc].op_str for pc in addrs)
    if dispatch: labels |= set(addrs)
    out = [f"/* {entry:06x} */", f"void {name}(M68K *c)", "{", "    (void)c;"]
    if dispatch: out.append("    uint32_t jt;   /* target of a computed jump (see L_DISPATCH) */")
    if entry != addrs[0]: out.append(f"    goto L{entry:06X};")
    for k, pc in enumerate(addrs):
        i = ins[pc]
        if pc in labels: out.append(f"L{pc:06X}:")
        body = translate(i, ins, local_names, entry)
        if pc in CLASS_SITES:                 # the hero's class, not the player slot (class_sites.txt)
            body, k2 = re.subn(r"rdsz\((c->a\[\d\]) \+ 0xb4u, 2\)", r"g4_hero_class(\1)", body)
            if not k2: raise LiftError(f"{pc:06x}: class site without a $b4 word read")
        out.append(f"    {{ G4T(0x{pc:06X}); {body} }}  /* {pc:06x}  {i.mnemonic} {i.op_str}".rstrip() + " */")
        nxt = pc + i.size
        if not is_terminal(i) and (k + 1 >= len(addrs) or addrs[k + 1] != nxt):
            out.append(f"    goto L{nxt:06X};")
    if dispatch:
        cases = "\n".join(f"    case 0x{a:06X}u: goto L{a:06X};" for a in addrs)
        out.append("L_DISPATCH:   /* computed jump: into this routine's code, or out of it */")
        out.append("    switch (jt) {\n" + cases + "\n    default: break;\n    }")
        out.append(f"    g4_jmp(c, jt); {rts_code(entry)}")
    out.append("}")
    return "\n".join(out), len(addrs)

def is_terminal(i):
    b = base(i.mnemonic)
    return b in ("rts", "rte", "rtr", "bra", "jmp")

# instructions whose $b4(aN) read means the hero class (character select): see class_sites.txt
CLASS_SITES = {int(l.split()[0], 16) for l in open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "class_sites.txt"))
               if l.strip() and not l.startswith("#")}

def sz_of(m):
    if m.endswith(".b"): return 1
    if m.endswith(".w"): return 2
    if m.endswith(".l"): return 4
    return 0

_TMP = __import__("itertools").count(1)          # temporary-variable numbers (deterministic)

def read(op, sz):
    """(statements, C expression) reading op at size sz (value masked to sz)."""
    if op.kind == "dreg": return [], f"getd(c, {op.n}, {sz})"
    if op.kind == "areg": return [], f"(c->a[{op.n}] & SZM({sz}))"
    if op.kind == "imm": return [], f"0x{op.val & ((1 << (8 * sz)) - 1):X}u"
    if op.kind == "mem":
        # read into a temporary now, so a later (An)+ increment cannot move the address under it
        a = op.addr(); t = f"s{next(_TMP)}"      # numbered in order: the same name every run
        return op.pre[:] + [f"uint32_t {t} = rdsz({a}, {sz});"], t
    raise LiftError(f"cannot read {op.text}")

def write(op, sz, val):
    if op.kind == "dreg": return [f"setd(c, {op.n}, {sz}, {val});"]
    if op.kind == "areg": return [f"c->a[{op.n}] = {val};"]
    if op.kind == "mem":
        a = op.addr(); return op.pre[:] + [f"wrsz({a}, {sz}, {val});"]
    raise LiftError(f"cannot write {op.text}")

def rmw(op, sz, fn):
    """read-modify-write op: fn(value_expr) -> (statements, new_value_expr or None)."""
    if op.kind == "dreg":
        st, nv = fn(f"getd(c, {op.n}, {sz})")
        return st + ([f"setd(c, {op.n}, {sz}, {nv});"] if nv else [])
    if op.kind == "mem":
        a = op.addr()
        st = op.pre[:] + [f"uint32_t ea = {a};"]
        st2, nv = fn(f"rdsz(ea, {sz})")
        return st + st2 + ([f"wrsz(ea, {sz}, {nv});"] if nv else []) + op.post
    raise LiftError(f"cannot modify {op.text}")

SHIFT = {"asl": 0, "asr": 1, "lsl": 2, "lsr": 3, "rol": 4, "ror": 5, "roxl": 6, "roxr": 7}

def rts_code(entry):
    """rts: to a continuation this routine pushed itself, stay inside; anything else is a real return."""
    rets = sorted(RETURNS.get(entry, ()))
    if not rets: return "return;"
    cases = " ".join(f"case 0x{r:06X}u: goto L{r:06X};" for r in rets)
    return f"{{ uint32_t r = pop32(c); switch (r) {{ {cases} default: push32(c, r); return; }} }}"

def translate(i, ins, local_names, entry=None):
    m = i.mnemonic; b = base(m); sz = sz_of(m)
    ops_t = split_ops(i.op_str)
    nxt = i.address + i.size
    def O(k, size=None): return Op(ops_t[k], size if size is not None else sz)
    st = []

    if b in ("nop",): return ";"
    if b == "rte": return "return;"
    if b == "rts": return rts_code(entry)
    if b in BRANCH_CC:
        t = target_of(i)
        if b == "bra": return f"goto L{t:06X};"
        return f"if (CC_{BRANCH_CC[b]}) goto L{t:06X};"
    if b in DB_CC:
        r = O(0, 2); t = target_of(i)
        cond = DB_CC[b]
        pre = "" if cond == "F" else f"if (!(CC_{cond})) "
        return pre + f"{{ SETW(c->d[{r.n}], W(c->d[{r.n}]) - 1); if (W(c->d[{r.n}]) != 0xFFFFu) goto L{t:06X}; }}"
    if b in S_CC and b not in ("swap",):
        d = O(0, 1)
        return " ".join(write(d, 1, f"(CC_{S_CC[b]}) ? 0xFFu : 0u") + d.post)
    if b == "bsr":
        return f"JSR(c, 0x{nxt:06X}, 0x{target_of(i):06X});"
    if b == "jsr":                                          # absolute, (An), d(An,Xn), d(PC,Xn)
        return f"JSR(c, 0x{nxt:06X}, {O(0, 4).addr()});"
    if b == "jmp":
        # a jump out of the routine runs code that ends with rts: that rts pops whatever is on the
        # stack now, which may be a continuation this routine pushed itself -- so finish as rts does
        o = O(0, 4); t = target_of(i); after = rts_code(entry)
        if t is not None and "(" not in i.op_str:
            if t in ins: return f"goto L{t:06X};"
            return f"g4_jmp(c, 0x{t:06X}); {after}"
        return f"jt = G4_ADDR({o.addr()}); goto L_DISPATCH;"

    if b == "moveq":
        s, d = O(0, 4), O(1, 4)
        return f"c->d[{d.n}] = f_logic(c, 4, 0x{(s.val & 0xFF) | (0xFFFFFF00 if s.val & 0x80 else 0):X}u);"   # 8-bit, sign-extended
    if b == "move" and ops_t[-1] in ("sr", "ccr"):
        s = O(0, 2); st2, v = read(s, 2)
        fn = "set_sr" if ops_t[-1] == "sr" else "set_ccr"
        return " ".join(st2 + [f"{fn}(c, {v});"] + s.post)
    if b == "move" and ops_t[0] == "sr":
        d = O(1, 2); return " ".join(write(d, 2, "get_sr(c)") + d.post)
    if b == "move" and "usp" in ops_t:
        if ops_t[1] == "usp": s = O(0, 4); return f"c->usp = c->a[{s.n}];"
        d = O(1, 4); return f"c->a[{d.n}] = c->usp;"
    if b == "move":
        s, d = O(0), O(1)
        st2, v = read(s, sz)
        st = st2 + [f"uint32_t v = f_logic(c, {sz}, {v});"] + s.post
        st += write(d, sz, "v") + d.post
        return " ".join(st)
    if b == "movea":
        s, d = O(0), O(1, 4)
        st2, v = read(s, sz)
        # the source's (An)+ happens before the destination is written: movea.l (a2)+,a2 keeps the loaded value
        return " ".join(st2 + s.post + [f"c->a[{d.n}] = sext({sz}, {v});"])
    if b == "movem":
        a, b2 = O(0), O(1)
        for o in (a, b2):                                      # a one-register list parses as a register
            if o.kind in ("dreg", "areg"): o.kind, o.mask = "list", 1 << (o.n + (8 if o.kind == "areg" else 0))
        if a.kind == "mem" and "(pc" in ops_t[0]:
            # capstone takes PC as the instruction + 2, but movem's register mask comes first:
            # the extension word, and so PC, is at the instruction + 4
            m = re.match(r"^0x([0-9A-F]+)u", a.ea)
            a.ea = f"0x{int(m.group(1), 16) + 2:06X}u" + a.ea[m.end():]
        if a.kind == "list":                                   # registers -> memory
            mask = a.mask
            if b2.mode == "predec":
                return (f"c->a[{b2.n}] -= {sz}u * movem_count(0x{mask:X}u); "
                        f"(void)movem_store(c, 0x{mask:X}u, c->a[{b2.n}], {sz});")
            return f"(void)movem_store(c, 0x{mask:X}u, {b2.ea}, {sz});"
        mask = b2.mask                                           # memory -> registers
        if a.mode == "postinc":
            return f"{{ uint32_t e = movem_load(c, 0x{mask:X}u, c->a[{a.n}], {sz}); c->a[{a.n}] = e; }}"
        return f"(void)movem_load(c, 0x{mask:X}u, {a.ea}, {sz});"
    if b == "lea":
        s, d = O(0, 4), O(1, 4)
        return f"c->a[{d.n}] = {s.ea};"
    if b == "pea":
        s = O(0, 4)
        return f"{{ uint32_t e = {s.ea}; push32(c, e); }}"
    if b == "clr":
        d = O(0)
        return " ".join(write(d, sz, "0") + d.post + [f"(void)f_logic(c, {sz}, 0);"])
    if b == "tst":
        s = O(0); st2, v = read(s, sz)
        return " ".join(st2 + [f"(void)f_logic(c, {sz}, {v});"] + s.post)
    if b in ("cmp", "cmpi", "cmpa"):
        s, d = O(0), O(1)
        st2, v = read(s, sz)
        if b == "cmpa" or d.kind == "areg":
            return " ".join(st2 + [f"f_cmp(c, 4, c->a[{d.n}], sext({sz}, {v}));"] + s.post)
        st3, dv = read(d, sz)
        return " ".join(st2 + s.post + st3 + [f"f_cmp(c, {sz}, {dv}, {v});"] + d.post)
    if b in ("add", "addi", "addq", "sub", "subi", "subq", "adda", "suba"):
        s, d = O(0), O(1)
        add = b.startswith("add")
        st2, v = read(s, sz)
        if d.kind == "areg" or b in ("adda", "suba"):           # address register: no flags, 32-bit
            sign = "+" if add else "-"
            return " ".join(st2 + s.post + [f"c->a[{d.n}] {sign}= sext({sz}, {v});"])
        fn = "f_add" if add else "f_sub"
        return " ".join(st2 + s.post + rmw(d, sz, lambda x: ([], f"{fn}(c, {sz}, {x}, {v})")))
    if b in ("and", "andi", "or", "ori", "eor", "eori"):
        s, d = O(0), O(1)
        opc = {"and": "&", "andi": "&", "or": "|", "ori": "|", "eor": "^", "eori": "^"}[b]
        if d.kind in ("sr", "ccr"):
            fn = "set_sr" if d.kind == "sr" else "set_ccr"
            return f"{fn}(c, get_sr(c) {opc} 0x{s.val:X}u);" if d.kind == "sr" else \
                   f"set_ccr(c, (get_sr(c) & 0x1F) {opc} 0x{s.val & 0x1F:X}u);"
        st2, v = read(s, sz)
        return " ".join(st2 + s.post + rmw(d, sz, lambda x: ([], f"f_logic(c, {sz}, {x} {opc} {v})")))
    if b == "not":
        d = O(0); return " ".join(rmw(d, sz, lambda x: ([], f"f_logic(c, {sz}, ~{x})")))
    if b == "neg":
        d = O(0); return " ".join(rmw(d, sz, lambda x: ([], f"f_sub(c, {sz}, 0, {x})")))
    if b == "negx":
        d = O(0); return " ".join(rmw(d, sz, lambda x: ([], f"f_subx(c, {sz}, 0, {x})")))
    if b in ("addx", "subx"):
        s, d = O(0), O(1)
        if s.kind != "dreg": raise LiftError("addx/subx memory form")
        fn = "f_addx" if b == "addx" else "f_subx"
        return f"setd(c, {d.n}, {sz}, {fn}(c, {sz}, getd(c, {d.n}, {sz}), getd(c, {s.n}, {sz})));"
    if b == "ext":
        d = O(0)
        if sz == 2: return f"setd(c, {d.n}, 2, f_logic(c, 2, sext(1, c->d[{d.n}])));"
        return f"c->d[{d.n}] = f_logic(c, 4, sext(2, c->d[{d.n}]));"
    if b == "swap":
        d = O(0, 4)
        return f"c->d[{d.n}] = f_logic(c, 4, (c->d[{d.n}] << 16) | (c->d[{d.n}] >> 16));"
    if b == "exg":
        a, d = O(0, 4), O(1, 4)
        ra = f"c->{'d' if a.kind == 'dreg' else 'a'}[{a.n}]"; rb = f"c->{'d' if d.kind == 'dreg' else 'a'}[{d.n}]"
        return f"{{ uint32_t t = {ra}; {ra} = {rb}; {rb} = t; }}"
    if b in SHIFT:
        k = SHIFT[b]
        if len(ops_t) == 1:                                     # memory, by one
            d = O(0, 2); return " ".join(rmw(d, 2, lambda x: ([], f"f_shift(c, {k}, 2, {x}, 1)")))
        s, d = O(0), O(1)
        cnt = f"{s.val}" if s.kind == "imm" else f"(c->d[{s.n}] & 63)"
        return f"setd(c, {d.n}, {sz}, f_shift(c, {k}, {sz}, getd(c, {d.n}, {sz}), {cnt}));"
    if b in ("btst", "bset", "bclr", "bchg"):
        s, d = O(0, 1), O(1, 1)
        bit = f"({s.val} & {'31' if d.kind == 'dreg' else '7'})" if s.kind == "imm" else f"(c->d[{s.n}] & {'31' if d.kind == 'dreg' else '7'})"
        if d.kind == "dreg":
            if b == "btst": return f"f_btst(c, c->d[{d.n}], {bit});"
            op = {"bset": "|=", "bclr": "&= ~", "bchg": "^="}[b]
            return f"f_btst(c, c->d[{d.n}], {bit}); c->d[{d.n}] {op}(1u << {bit});"
        if b == "btst":
            st2, v = read(d, 1); return " ".join(st2 + [f"f_btst(c, {v}, {bit});"] + d.post)
        op = {"bset": "|", "bclr": "& ~", "bchg": "^"}[b]
        return " ".join(rmw(d, 1, lambda x: ([f"f_btst(c, {x}, {bit});"], f"({x} {op}(1u << {bit}))")))
    if b in ("mulu", "muls", "divu", "divs"):
        s, d = O(0, 2), O(1, 4)
        st2, v = read(s, 2)
        return " ".join(st2 + s.post + [f"c->d[{d.n}] = f_{b}(c, c->d[{d.n}], {v});"])
    if b == "link":
        r, dsp = O(0, 4), O(1, 2)
        return f"push32(c, c->a[{r.n}]); c->a[{r.n}] = c->a[7]; c->a[7] += sext(2, 0x{dsp.val & 0xFFFF:X}u);"
    if b == "unlk":
        r = O(0, 4); return f"c->a[7] = c->a[{r.n}]; c->a[{r.n}] = pop32(c);"
    if b == "tas":
        d = O(0, 1); return " ".join(rmw(d, 1, lambda x: ([f"(void)f_logic(c, 1, {x});"], f"({x} | 0x80u)")))
    raise LiftError(f"{i.address:06x}: no translation for {m} {i.op_str}")

# ------------------------------------------------------------------ files
def hand_translated():
    s = open(os.path.join(HERE, "c", "g4_dispatch.c")).read().split("#include \"gen/g4_gen_table.inc\"")[0]
    return {int(a, 16) for a in re.findall(r"\{ 0x([0-9A-F]{6}), g4_", s)}

def regen_index():
    files = sorted(f for f in os.listdir(GEN) if f.endswith(".c"))
    table, decls = [], []
    for f in files:
        for a, n in re.findall(r"^/\* ([0-9a-f]{6}) \*/\nvoid (g4_\w+)\(M68K \*c\)", open(os.path.join(GEN, f)).read(), re.M):
            table.append(f"    {{ 0x{int(a, 16):06X}, {n} }},   /* gen/{f} */")
            decls.append(f"void {n}(M68K *c);   /* 0x{int(a, 16):06X} gen/{f} */")
    open(os.path.join(GEN, "g4_gen_table.inc"), "w").write("/* generated by lift.py */\n" + "\n".join(table) + "\n")
    open(os.path.join(GEN, "g4_gen_decls.inc"), "w").write("/* generated by lift.py */\n" + "\n".join(decls) + "\n")

FAILED = []

if __name__ == "__main__":
    group, specs = args[0], args[1:]
    hand = hand_translated()
    other = set()                                  # entries already generated in other files
    for f in os.listdir(GEN) if os.path.isdir(GEN) else []:
        if f.endswith(".c") and f != f"{group}.c":
            other |= {int(a, 16) for a in re.findall(r"^/\* ([0-9a-f]{6}) \*/", open(os.path.join(GEN, f)).read(), re.M)}
    todo = []
    for sp in specs:
        a, _, n = sp.partition("=")
        a = int(a, 16)
        if a in hand or a in other:
            if KEEP_GOING: continue
            sys.exit(f"{a:06x} is already translated" + (" by hand" if a in hand else " in another gen file"))
        todo.append((a, f"g4_{n}" if n else f"g4_sub_{a:06x}"))
    names = {a: n for a, n in todo}
    parts = [f"/*\n * gen/{group}.c -- GENERATED by lift.py from the 1993-09 ROM; do not edit.\n"
             " * One block per 68000 instruction (address and instruction in the comment);\n"
             " * condition codes exact (g4_ops.h); calls through JSR()/g4_jmp().\n */",
             "#include \"../g4_ops.h\"", ""]
    total = 0; done = set()
    while todo:
        a, n = todo.pop(0)
        if a in done: continue
        done.add(a)
        try:
            txt, cnt = lift_routine(a, n, names)
        except (LiftError, KeyError, ValueError, AttributeError) as e:
            if not KEEP_GOING: raise
            FAILED.append((a, str(e))); continue
        total += cnt
        parts.append(txt); parts.append("")
        if CLOSURE:                                    # also lift callees inside the range
            lo, hi = CLOSURE
            for t in re.findall(r"JSR\(c, 0x[0-9A-F]+, 0x([0-9A-F]+)u?\)", txt):
                t = int(t, 16)
                if lo <= t < hi and t not in done and t not in hand and t not in other: todo.append((t, f"g4_sub_{t:06x}"))
    todo = list(done)
    os.makedirs(GEN, exist_ok=True)
    open(os.path.join(GEN, f"{group}.c"), "w").write("\n".join(parts))
    regen_index()
    print(f"gen/{group}.c: {len(done) - len(FAILED)} routines, {total} instructions" + (f", {len(FAILED)} failed" if FAILED else ""))
    if FAILED:
        with open(os.path.join(GEN, f"{group}.failed.txt"), "w") as fh:
            for a, e in FAILED: fh.write(f"{a:06x}  {e}\n")
