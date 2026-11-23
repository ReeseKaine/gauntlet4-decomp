/*
 * g4_ops.h -- exact 68000 ALU / flag helpers used by lifter-generated code
 * (files in gen/). Every helper updates the condition codes the way the CPU does:
 * X N Z V C live in c->x, c->n, c->z, c->v, c->cf (0 or 1 each).
 * Sizes are given in bytes: 1 = .b, 2 = .w, 4 = .l.
 */
#ifndef G4_OPS_H
#define G4_OPS_H
#include "g4_m68k.h"

#define SZM(sz) ((sz) == 1 ? 0xFFu : (sz) == 2 ? 0xFFFFu : 0xFFFFFFFFu)
#define SZB(sz) ((sz) == 1 ? 0x80u : (sz) == 2 ? 0x8000u : 0x80000000u)

/* ---- registers and memory by size */
static inline uint32_t getd(M68K *c, int n, int sz) { return c->d[n] & SZM(sz); }
static inline void setd(M68K *c, int n, int sz, uint32_t v)
{ c->d[n] = (c->d[n] & ~SZM(sz)) | (v & SZM(sz)); }
static inline uint32_t rdsz(uint32_t a, int sz) { return sz == 1 ? rd8(a) : sz == 2 ? rd16(a) : rd32(a); }
/* Character select: the hero class of a record (lift.py emits this at the sites in class_sites.txt
   instead of reading $B4). -1 in the table = the record's own $B4, as in the original game. */
extern int16_t g4_hero_class_tab[4];
static inline uint32_t g4_hero_class(uint32_t a)
{
    uint32_t o = (a & 0xFFFFFFu) - 0xFF0040u;
    if (o < 4u * 0x2C4u && o % 0x2C4u == 0 && g4_hero_class_tab[o / 0x2C4u] >= 0) return (uint32_t)g4_hero_class_tab[o / 0x2C4u];
    return rd16(a + 0xB4u);
}
static inline void wrsz(uint32_t a, int sz, uint32_t v)
{ if (sz == 1) wr8(a, (uint8_t)v); else if (sz == 2) wr16(a, (uint16_t)v); else wr32(a, v); }
static inline uint32_t sext(int sz, uint32_t v)
{ return sz == 1 ? (uint32_t)(int32_t)(int8_t)v : sz == 2 ? (uint32_t)(int32_t)(int16_t)v : v; }
/* (An)+ / -(An) step: bytes on A7 keep the stack word-aligned */
static inline uint32_t step(int an, int sz) { return (an == 7 && sz == 1) ? 2 : (uint32_t)sz; }

/* ---- condition codes */
static inline uint32_t f_nz(M68K *c, int sz, uint32_t r)
{ r &= SZM(sz); c->n = (r & SZB(sz)) != 0; c->z = r == 0; return r; }
static inline uint32_t f_logic(M68K *c, int sz, uint32_t r)   /* move, and, or, eor, not, tst, ... */
{ r = f_nz(c, sz, r); c->v = 0; c->cf = 0; return r; }
static inline uint32_t f_add(M68K *c, int sz, uint32_t d, uint32_t s)
{
    uint32_t m = SZM(sz), r; d &= m; s &= m; r = (d + s) & m;
    c->cf = c->x = sz == 4 ? (r < d) : ((d + s) > m);
    c->v = ((~(d ^ s)) & (d ^ r) & SZB(sz)) != 0;
    f_nz(c, sz, r); return r;
}
static inline uint32_t f_sub(M68K *c, int sz, uint32_t d, uint32_t s)   /* d - s */
{
    uint32_t m = SZM(sz), r; d &= m; s &= m; r = (d - s) & m;
    c->cf = c->x = s > d;
    c->v = ((d ^ s) & (d ^ r) & SZB(sz)) != 0;
    f_nz(c, sz, r); return r;
}
static inline void f_cmp(M68K *c, int sz, uint32_t d, uint32_t s)      /* like sub, X untouched */
{
    uint32_t m = SZM(sz), r; d &= m; s &= m; r = (d - s) & m;
    c->cf = s > d; c->v = ((d ^ s) & (d ^ r) & SZB(sz)) != 0; f_nz(c, sz, r);
}
static inline uint32_t f_addx(M68K *c, int sz, uint32_t d, uint32_t s)
{
    uint32_t m = SZM(sz), x = c->x, r; uint64_t w; d &= m; s &= m;
    w = (uint64_t)d + s + x; r = (uint32_t)w & m;
    c->cf = c->x = w > m; c->v = ((~(d ^ s)) & (d ^ r) & SZB(sz)) != 0;
    c->n = (r & SZB(sz)) != 0; if (r) c->z = 0; return r;
}
static inline uint32_t f_subx(M68K *c, int sz, uint32_t d, uint32_t s)
{
    uint32_t m = SZM(sz), x = c->x, r; d &= m; s &= m; r = (d - s - x) & m;
    c->cf = c->x = ((uint64_t)s + x) > d; c->v = ((d ^ s) & (d ^ r) & SZB(sz)) != 0;
    c->n = (r & SZB(sz)) != 0; if (r) c->z = 0; return r;
}

/* ---- shifts and rotates: kind 0 asl 1 asr 2 lsl 3 lsr 4 rol 5 ror 6 roxl 7 roxr */
static inline uint32_t f_shift(M68K *c, int kind, int sz, uint32_t v, unsigned cnt)
{
    uint32_t m = SZM(sz), msb = SZB(sz), out = 0, v0;
    unsigned i; int vflag = 0;
    v &= m; v0 = v; cnt &= 63;
    if (cnt == 0) {
        c->cf = (kind >= 6) ? c->x : 0; c->v = 0; f_nz(c, sz, v); return v;
    }
    for (i = 0; i < cnt; i++) {
        switch (kind) {
        case 0:                                   /* asl; V: the sign bit changed at any step */
            out = (v & msb) != 0; v = (v << 1) & m;
            if (((v & msb) != 0) != ((v0 & msb) != 0)) vflag = 1;
            v0 = v;
            break;
        case 1: out = v & 1; v = (v >> 1) | (v & msb); break;
        case 2: out = (v & msb) != 0; v = (v << 1) & m; break;
        case 3: out = v & 1; v >>= 1; break;
        case 4: out = (v & msb) != 0; v = ((v << 1) | out) & m; break;
        case 5: out = v & 1; v = (v >> 1) | (out ? msb : 0); break;
        case 6: out = (v & msb) != 0; v = ((v << 1) | c->x) & m; c->x = out; break;
        case 7: out = v & 1; v = (v >> 1) | (c->x ? msb : 0); c->x = out; break;
        }
    }
    c->cf = out;
    if (kind <= 3) c->x = out;                /* rotates (4, 5) leave X alone */
    c->v = (kind == 0) ? vflag : 0;
    f_nz(c, sz, v);
    return v;
}

/* ---- bit operations: Z = !bit (bit taken mod 32 for Dn, mod 8 for memory) */
static inline void f_btst(M68K *c, uint32_t v, unsigned bit) { c->z = ((v >> bit) & 1) == 0; }

/* ---- multiply / divide (.w forms) */
static inline uint32_t f_mulu(M68K *c, uint32_t d, uint32_t s)
{ uint32_t r = (d & 0xFFFF) * (s & 0xFFFF); c->n = r >> 31; c->z = r == 0; c->v = 0; c->cf = 0; return r; }
static inline uint32_t f_muls(M68K *c, uint32_t d, uint32_t s)
{ uint32_t r = (uint32_t)((int32_t)(int16_t)d * (int32_t)(int16_t)s); c->n = r >> 31; c->z = r == 0; c->v = 0; c->cf = 0; return r; }
static inline uint32_t f_divu(M68K *c, uint32_t d, uint32_t s)
{
    uint32_t q, r; s &= 0xFFFF; c->cf = 0;
    if (s == 0) return d;                     /* divide-by-zero trap: not used by the game */
    q = d / s; r = d % s;
    if (q > 0xFFFF) { c->v = 1; c->z = 0; return d; }   /* overflow: N, Z undefined on the 68000; as Unicorn: N kept, Z clear */
    c->v = 0; c->n = (q >> 15) & 1; c->z = q == 0; return (r << 16) | q;
}
static inline uint32_t f_divs(M68K *c, uint32_t d, uint32_t s)
{
    int32_t dd = (int32_t)d, ss = (int16_t)s, q, r; c->cf = 0;
    if (ss == 0) return d;
    if (ss == -1 && dd == INT32_MIN) { c->v = 1; c->z = 0; return d; }   /* quotient overflow (and a C trap) */
    q = dd / ss; r = dd % ss;
    if (q > 32767 || q < -32768) { c->v = 1; c->z = 0; return d; }
    c->v = 0; c->n = (q >> 15) & 1; c->z = q == 0; return ((uint32_t)(r & 0xFFFF) << 16) | ((uint32_t)q & 0xFFFF);
}

/* ---- status register */
static inline uint16_t get_sr(M68K *c)
{ return (uint16_t)((c->sr & 0xFF00u) | (c->x << 4) | (c->n << 3) | (c->z << 2) | (c->v << 1) | c->cf); }
static inline void set_ccr(M68K *c, uint32_t v)
{ c->x = (v >> 4) & 1; c->n = (v >> 3) & 1; c->z = (v >> 2) & 1; c->v = (v >> 1) & 1; c->cf = v & 1; }
static inline void set_sr(M68K *c, uint32_t v)
{
    /* the S bit selects the stack pointer: on a change, A7 and the inactive one (kept in usp) swap */
    if ((c->sr ^ v) & 0x2000u) { uint32_t t = c->a[7]; c->a[7] = c->usp; c->usp = t; }
    c->sr = v & 0xFFFF; set_ccr(c, v);
}

/* ---- movem: registers 0-7 D0-D7, 8-15 A0-A7, ascending memory order */
static inline uint32_t *reg16(M68K *c, int i) { return i < 8 ? &c->d[i] : &c->a[i - 8]; }
static inline uint32_t movem_store(M68K *c, uint32_t mask, uint32_t addr, int sz)
{
    int i;
    for (i = 0; i < 16; i++) if (mask & (1u << i)) { wrsz(addr, sz, *reg16(c, i)); addr += (uint32_t)sz; }
    return addr;
}
static inline uint32_t movem_load(M68K *c, uint32_t mask, uint32_t addr, int sz)
{
    int i;
    for (i = 0; i < 16; i++) if (mask & (1u << i)) { *reg16(c, i) = sext(sz, rdsz(addr, sz)); addr += (uint32_t)sz; }
    return addr;
}
static inline uint32_t movem_count(uint32_t mask) { uint32_t n = 0; while (mask) { n += mask & 1; mask >>= 1; } return n; }

/* ---- condition tests */
#define CC_T   1
#define CC_F   0
#define CC_HI  (!c->cf && !c->z)
#define CC_LS  (c->cf || c->z)
#define CC_CC  (!c->cf)
#define CC_CS  (c->cf)
#define CC_NE  (!c->z)
#define CC_EQ  (c->z)
#define CC_VC  (!c->v)
#define CC_VS  (c->v)
#define CC_PL  (!c->n)
#define CC_MI  (c->n)
#define CC_GE  (c->n == c->v)
#define CC_LT  (c->n != c->v)
#define CC_GT  (!c->z && c->n == c->v)
#define CC_LE  (c->z || c->n != c->v)

/* per-instruction trace hook: compiled in only with -DG4_TRACE (debugging divergences) */
#ifdef G4_TRACE
void g4_trace(M68K *c, uint32_t pc);
#define G4T(pc) g4_trace(c, (pc))
#elif defined(G4_TICK)
/* host build: instructions count down; at zero the host runs a frame (its interrupts arrive
   between two instructions, as on the CPU) -- see host/g4host.c */
extern long g4_ticks;
extern uint32_t g4_tick_pc;
void g4_tick_frame(M68K *c);
#define G4T(pc) do { if (--g4_ticks <= 0) { g4_tick_pc = (pc); g4_tick_frame(c); } } while (0)
#else
#define G4T(pc) ((void)0)
#endif

/* jmp to another routine: it returns straight to our caller (no push / pop) */
void g4_jmp(M68K *c, uint32_t target);

#endif
