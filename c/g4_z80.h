/*
 * g4_z80.h -- register model for the Gauntlet 4 Z80 sample player (Z80 RAM
 * image at ROM 0x3D470), translated to C one statement per Z80 instruction
 * by lift_z80.py into gen_z80/g4_z80.c.
 *
 * Memory map, as the Z80 sees the Mega Drive bus:
 *   0x0000-0x3FFF  Z80 RAM, 8 KB (mirrored)
 *   0x4000-0x5FFF  YM2612 (4 ports, mirrored): g4z80_io.ym_read / ym_write
 *   0x6000-0x60FF  bank register: each write shifts bit 0 in as bank bit 8
 *   0x7F00-0x7FFF  VDP / PSG: g4z80_io.other_write (reads give 0xFF)
 *   0x8000-0xFFFF  32 KB window into 68000 memory at bank << 15 (rd8 of g4_m68k.h)
 *
 * Flags are kept exactly, undocumented bits 3 (X) and 5 (Y) included.
 */
#ifndef G4_Z80_H
#define G4_Z80_H
#include "g4_m68k.h"

typedef struct {
    uint8_t a, f, b, c, d, e, h, l;          /* main set */
    uint8_t a_, f_, b_, c_, d_, e_, h_, l_;  /* shadow set (EX AF,AF' / EXX) */
    uint16_t ix, iy, sp;
    uint8_t iff, im;                         /* interrupts: DI only in this program */
    uint16_t pc;                             /* where g4z80_run resumes */
    int32_t budget;                          /* T-states left to run */
} Z80;

typedef struct {
    uint8_t (*ym_read)(uint16_t port);                /* port 0-3 */
    void    (*ym_write)(uint16_t port, uint8_t v);
    void    (*bank_write)(uint8_t v);                 /* optional: told about bank writes */
    void    (*other_write)(uint16_t addr, uint8_t v); /* VDP / PSG / unmapped */
    void    (*bad)(Z80 *z, uint16_t pc);              /* a jump the translation cannot follow */
} G4Z80IO;

extern uint8_t  g4z80_ram[0x2000];
extern uint16_t g4z80_bank;
extern G4Z80IO  g4z80_io;

static inline uint8_t zrd(uint16_t a)
{
    if (a < 0x4000) return g4z80_ram[a & 0x1FFF];
    if (a < 0x6000) return g4z80_io.ym_read ? g4z80_io.ym_read(a & 3) : 0;
    if (a < 0x8000) return 0xFF;
    return rd8(((uint32_t)g4z80_bank << 15) | (a & 0x7FFFu));
}
static inline void zwr(uint16_t a, uint8_t v)
{
    if (a < 0x4000) { g4z80_ram[a & 0x1FFF] = v; return; }
    if (a < 0x6000) { if (g4z80_io.ym_write) g4z80_io.ym_write(a & 3, v); return; }
    if (a < 0x6100) {
        g4z80_bank = (uint16_t)(((g4z80_bank >> 1) | ((v & 1u) << 8)) & 0x1FF);
        if (g4z80_io.bank_write) g4z80_io.bank_write(v);
        return;
    }
    if (a < 0x8000) { if (g4z80_io.other_write) g4z80_io.other_write(a, v); return; }
    wr8(((uint32_t)g4z80_bank << 15) | (a & 0x7FFFu), v);
}

/* register pairs */
#define BC(z) ((uint16_t)(((z)->b << 8) | (z)->c))
#define DE(z) ((uint16_t)(((z)->d << 8) | (z)->e))
#define HL(z) ((uint16_t)(((z)->h << 8) | (z)->l))
#define SETBC(z, v) ((z)->b = (uint8_t)((v) >> 8), (z)->c = (uint8_t)(v))
#define SETDE(z, v) ((z)->d = (uint8_t)((v) >> 8), (z)->e = (uint8_t)(v))
#define SETHL(z, v) ((z)->h = (uint8_t)((v) >> 8), (z)->l = (uint8_t)(v))

/* flags */
enum { FC = 0x01, FN = 0x02, FPV = 0x04, FX = 0x08, FH = 0x10, FY = 0x20, FZ = 0x40, FS = 0x80 };
static inline uint8_t zpar(uint8_t v) { v ^= v >> 4; v ^= v >> 2; v ^= v >> 1; return (v & 1) ? 0 : FPV; }
static inline uint8_t zszxy(uint8_t r) { return (uint8_t)((r & (FS | FY | FX)) | (r ? 0 : FZ)); }

static inline uint8_t z_add8(Z80 *z, uint8_t a, uint8_t v, int carry)
{
    unsigned r = (unsigned)a + v + (unsigned)carry; uint8_t r8 = (uint8_t)r;
    z->f = (uint8_t)(zszxy(r8) | ((a ^ v ^ r8) & FH) | ((((a ^ ~v) & (a ^ r8)) & 0x80) ? FPV : 0) | (r > 0xFF ? FC : 0));
    return r8;
}
static inline uint8_t z_sub8(Z80 *z, uint8_t a, uint8_t v, int carry)
{
    unsigned r = (unsigned)a - v - (unsigned)carry; uint8_t r8 = (uint8_t)r;
    z->f = (uint8_t)(zszxy(r8) | ((a ^ v ^ r8) & FH) | ((((a ^ v) & (a ^ r8)) & 0x80) ? FPV : 0) | FN | (r > 0xFF ? FC : 0));
    return r8;
}
static inline void z_cp(Z80 *z, uint8_t a, uint8_t v)            /* X and Y come from the operand */
{
    (void)z_sub8(z, a, v, 0);
    z->f = (uint8_t)((z->f & ~(FX | FY)) | (v & (FX | FY)));
}
static inline uint8_t z_and(Z80 *z, uint8_t r) { z->f = (uint8_t)(zszxy(r) | FH | zpar(r)); return r; }
static inline uint8_t z_or (Z80 *z, uint8_t r) { z->f = (uint8_t)(zszxy(r) | zpar(r)); return r; }
static inline uint8_t z_inc8(Z80 *z, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    z->f = (uint8_t)((z->f & FC) | zszxy(r) | ((r & 0x0F) == 0 ? FH : 0) | (r == 0x80 ? FPV : 0));
    return r;
}
static inline uint16_t z_add16(Z80 *z, uint16_t a, uint16_t v)
{
    uint32_t r = (uint32_t)a + v;
    z->f = (uint8_t)((z->f & (FS | FZ | FPV)) | (((a ^ v ^ r) >> 8) & FH) | ((r >> 8) & (FX | FY)) | (r > 0xFFFF ? FC : 0));
    return (uint16_t)r;
}
static inline uint16_t z_sbc16(Z80 *z, uint16_t a, uint16_t v)
{
    uint32_t r = (uint32_t)a - v - (uint32_t)(z->f & FC); uint16_t r16 = (uint16_t)r;
    z->f = (uint8_t)(((r16 >> 8) & (FS | FX | FY)) | (r16 ? 0 : FZ) | (((a ^ v ^ r16) >> 8) & FH)
                     | ((((a ^ v) & (a ^ r16)) & 0x8000) ? FPV : 0) | FN | (r > 0xFFFF ? FC : 0));
    return r16;
}
static inline void z_rrca(Z80 *z) { uint8_t c = z->a & 1; z->a = (uint8_t)((z->a >> 1) | (c << 7));
                                    z->f = (uint8_t)((z->f & (FS | FZ | FPV)) | (z->a & (FX | FY)) | c); }
static inline void z_rla(Z80 *z)  { uint8_t c = z->a >> 7; z->a = (uint8_t)((z->a << 1) | (z->f & FC));
                                    z->f = (uint8_t)((z->f & (FS | FZ | FPV)) | (z->a & (FX | FY)) | c); }
static inline uint8_t z_rl(Z80 *z, uint8_t v)  { uint8_t c = v >> 7; uint8_t r = (uint8_t)((v << 1) | (z->f & FC));
                                                 z->f = (uint8_t)(zszxy(r) | zpar(r) | c); return r; }
static inline uint8_t z_sra(Z80 *z, uint8_t v) { uint8_t c = v & 1; uint8_t r = (uint8_t)((v >> 1) | (v & 0x80));
                                                 z->f = (uint8_t)(zszxy(r) | zpar(r) | c); return r; }

/* stack */
static inline void z_push(Z80 *z, uint16_t v) { z->sp--; zwr(z->sp, (uint8_t)(v >> 8)); z->sp--; zwr(z->sp, (uint8_t)v); }
static inline uint16_t z_pop(Z80 *z) { uint16_t v = zrd(z->sp); z->sp++; v |= (uint16_t)(zrd(z->sp) << 8); z->sp++; return v; }

/* before each instruction (cyc = its T-states). Tests (-DG4_Z80_COUNT) count instructions
   and stop the program through a hook; otherwise the program returns when its budget is
   spent, z->pc = the instruction to resume at */
#ifdef G4_Z80_COUNT
void g4z80_tick(Z80 *z, uint16_t pc);
#define ZT(at, cyc) g4z80_tick(z, (at))
#else
#define ZT(at, cyc) do { if (z->budget <= 0) { z->pc = (at); return; } z->budget -= (cyc); } while (0)
#endif

/* the program from z->pc (0 = reset), for z->budget T-states */
void g4z80_run(Z80 *z);

#endif
