/*
 * g4_m68k.h -- minimal 68000 register/memory model for translating
 * Gauntlet 4 (Mega Drive, 1993-09) routines to standard C, instruction by
 * instruction.
 *
 * Conventions
 *   - D0-D7 and A0-A7 are 32-bit; A7 is the (supervisor) stack pointer.
 *   - Memory is a flat 16 MiB big-endian byte array covering the 68000's
 *     24-bit address space. Load the ROM at offset 0; work RAM is at
 *     0xFF0000-0xFFFFFF.
 *   - Word/byte operations only change the low 16/8 bits of a register,
 *     exactly like the CPU.
 *   - Condition codes:
 *       X  modelled everywhere: put_bits rotates bits through it with roxl, so
 *          every instruction in the translated routines that changes X
 *          updates c->x.
 *       Z  set only where a routine returns its result in Z (pw_decode's
 *          final moveq). Branches on Z inside a routine are written as plain
 *          C conditions instead.
 *       N/V/C are not stored: no caller reads them.
 *   - Calls: CALL() pushes the real return address, as bsr/jsr do. A C
 *     function covers its routine up to (not including) the final rts; the
 *     caller pops the return slot.
 *   - JSR() does the same through the dispatcher (g4_dispatch.c): a routine
 *     that has been translated runs as C; any other target goes to
 *     g4_external(), which the host provides (an emulator core, or a test
 *     script). Callees that return a result in Z set c->z.
 *   - Hardware: accesses to 0xA00000-0xDFFFFF (Z80 area, I/O, VDP, PSG) go
 *     to g4_io.read / g4_io.write with their size (1 or 2; long accesses are
 *     two word accesses, high word first, as on the 68000 bus). With no
 *     hooks set they fall through to the flat memory array.
 *   - c->sr holds SR as last loaded by "move #imm,SR" (g4_move_sr), which
 *     also sets the flags from it; otherwise the condition codes live in x/z
 *     (hand-translated code) or x/n/z/v/cf (lifter-generated code, gen/).
 */
#ifndef G4_M68K_H
#define G4_M68K_H

#include <stdint.h>

typedef struct {
    uint32_t d[8];      /* D0-D7 */
    uint32_t a[8];      /* A0-A7 (a[7] = SP) */
    uint32_t x;         /* X (extend) flag: 0 or 1 */
    uint32_t z;         /* Z flag, only where a routine returns its result in it */
    uint32_t sr;        /* status register as last set by move #imm,SR */
    uint32_t usp;       /* user stack pointer (move An,USP) */
    uint32_t n, v, cf;  /* N, V, C: maintained by lifter-generated code (gen/, g4_ops.h) */
} M68K;

/* 16 MiB memory image; must be set by the caller before any routine runs. */
extern uint8_t *g4_mem;

#define G4_ADDR(x) ((uint32_t)(x) & 0x00FFFFFFu)

/* Hardware access hooks (see Conventions). Defined in g4_dispatch.c. */
typedef struct {
    uint32_t (*read)(uint32_t addr, int size);
    void     (*write)(uint32_t addr, uint32_t value, int size);
} G4_IO;
extern G4_IO g4_io;
static inline int g4_is_io(uint32_t a) { a = G4_ADDR(a); return a >= 0xA00000u && a < 0xE00000u; }

static inline uint8_t  rd8 (uint32_t a)
{
    if (g4_io.read && g4_is_io(a)) return (uint8_t)g4_io.read(G4_ADDR(a), 1);
    return g4_mem[G4_ADDR(a)];
}
static inline uint16_t rd16(uint32_t a)
{
    if (g4_io.read && g4_is_io(a)) return (uint16_t)g4_io.read(G4_ADDR(a), 2);
    return (uint16_t)((g4_mem[G4_ADDR(a)] << 8) | g4_mem[G4_ADDR(a + 1)]);
}
static inline uint32_t rd32(uint32_t a) { return ((uint32_t)rd16(a) << 16) | rd16(a + 2); }
static inline void wr8 (uint32_t a, uint8_t v)
{
    if (g4_io.write && g4_is_io(a)) { g4_io.write(G4_ADDR(a), v, 1); return; }
    g4_mem[G4_ADDR(a)] = v;
}
static inline void wr16(uint32_t a, uint16_t v)
{
    if (g4_io.write && g4_is_io(a)) { g4_io.write(G4_ADDR(a), v, 2); return; }
    g4_mem[G4_ADDR(a)] = (uint8_t)(v >> 8); g4_mem[G4_ADDR(a + 1)] = (uint8_t)v;
}
static inline void wr32(uint32_t a, uint32_t v) { wr16(a, (uint16_t)(v >> 16)); wr16(a + 2, (uint16_t)v); }

/* Low word of a register, and "write .w" (upper word preserved). */
#define W(r)        ((uint16_t)(r))
#define SETW(r, v)  ((r) = ((r) & 0xFFFF0000u) | (uint16_t)(v))

/* Low byte of a register, and "write .b" (upper 24 bits preserved). */
#define B(r)        ((uint8_t)(r))
#define SETB(r, v)  ((r) = ((r) & 0xFFFFFF00u) | (uint8_t)(v))

/* Sign-extend a .w index register, as (d8,An,Dn.w) addressing does. */
static inline uint32_t sext16(uint16_t v) { return (uint32_t)(int32_t)(int16_t)v; }

/* Stack helpers (A7 grows down). */
static inline void     push32(M68K *c, uint32_t v) { c->a[7] -= 4; wr32(c->a[7], v); }
static inline uint32_t pop32 (M68K *c)             { uint32_t v = rd32(c->a[7]); c->a[7] += 4; return v; }

/* bsr/jsr: push the real return address, run the callee, then the callee's rts */
#define CALL(c, ret_addr, fn) do { push32((c), (ret_addr)); fn(c); (void)pop32(c); } while (0)

/* Dispatcher (g4_dispatch.c) */
void g4_jsr(M68K *c, uint32_t ret_addr, uint32_t target);   /* push ret, run target, pop */
void g4_external(M68K *c, uint32_t target);                 /* host-provided: untranslated code */
extern int g4_use_translated;   /* 0: send every JSR to g4_external (isolated testing) */
#define JSR(c, ret_addr, target) g4_jsr((c), (ret_addr), (target))

/* move #imm,SR: loads the condition codes too (X = bit 4, Z = bit 2) */
static inline void g4_move_sr(M68K *c, uint16_t v)
{ c->sr = v; c->x = (v >> 4) & 1u; c->n = (v >> 3) & 1u; c->z = (v >> 2) & 1u; c->v = (v >> 1) & 1u; c->cf = v & 1u; }

/* X flag helpers for add/sub on a word or long */
static inline uint16_t g4_addw(M68K *c, uint16_t a, uint16_t b) { c->x = ((uint32_t)a + b) > 0xFFFFu; return (uint16_t)(a + b); }
static inline uint16_t g4_subw(M68K *c, uint16_t a, uint16_t b) { c->x = a < b; return (uint16_t)(a - b); }
static inline uint32_t g4_subl(M68K *c, uint32_t a, uint32_t b) { c->x = a < b; return a - b; }

/* movem.l register masks (normal order: D0 = bit 0 ... A6 = bit 14) */
#define MV_D(n) (1u << (n))
#define MV_A(n) (1u << (8 + (n)))
static inline uint32_t *mv_reg(M68K *c, int i) { return i < 8 ? &c->d[i] : &c->a[i - 8]; }

/* movem.l <list>,-(SP): lowest-numbered register ends up at the lowest address */
static inline void movem_push(M68K *c, uint32_t mask)
{
    int i, n = 0;
    uint32_t p;
    for (i = 0; i < 15; i++) if (mask & (1u << i)) n++;
    c->a[7] -= 4u * (uint32_t)n;
    for (i = 0, p = c->a[7]; i < 15; i++)
        if (mask & (1u << i)) { wr32(p, *mv_reg(c, i)); p += 4; }
}

/* movem.l (SP)+,<list> */
static inline void movem_pop(M68K *c, uint32_t mask)
{
    int i;
    for (i = 0; i < 15; i++)
        if (mask & (1u << i)) { *mv_reg(c, i) = rd32(c->a[7]); c->a[7] += 4; }
}

/* link.w A5,#disp / unlk A5 */
static inline void link_a5(M68K *c, int16_t disp) { push32(c, c->a[5]); c->a[5] = c->a[7]; c->a[7] += sext16((uint16_t)disp); }
static inline void unlk_a5(M68K *c)               { c->a[7] = c->a[5]; c->a[5] = pop32(c); }

/* divu.w #imm,Dn: 32/16 -> remainder:quotient; unchanged on quotient overflow (X unaffected) */
static inline void divu_w(uint32_t *dn, uint16_t divisor)
{
    uint32_t q = *dn / divisor, r = *dn % divisor;
    if (q <= 0xFFFFu) *dn = (r << 16) | q;
}

/* -(A3) / (A3)+ on the field stack the decoders build in their link frame */
static inline void     g4_a3_push16(M68K *c, uint16_t v) { c->a[3] -= 2; wr16(c->a[3], v); }
static inline void     g4_a3_push32(M68K *c, uint32_t v) { c->a[3] -= 4; wr32(c->a[3], v); }
static inline uint16_t g4_a3_pop16(M68K *c) { uint16_t v = rd16(c->a[3]); c->a[3] += 2; return v; }
static inline uint32_t g4_a3_pop32(M68K *c) { uint32_t v = rd32(c->a[3]); c->a[3] += 4; return v; }

/* sub.w Ds,Dd (X = borrow) */
static inline void g4_sub_w(M68K *c, int dd, int ds)
{
    c->x = W(c->d[dd]) < W(c->d[ds]);
    SETW(c->d[dd], W(c->d[dd]) - W(c->d[ds]));
}

/* add.b Ds,Dd (X = carry) */
static inline void g4_add_b(M68K *c, int dd, int ds)
{
    unsigned s = (unsigned)B(c->d[dd]) + B(c->d[ds]);
    c->x = s > 0xFFu;
    SETB(c->d[dd], s);
}

/* ROM tables used by the password routines */
#define PW_SCRAMBLE   0x029CD7u   /* 32 chars "J2H=K7+U0W9GTR3F4:6LC-1Y8EXMD5PA" */
#define TRI_TABLE     0x012E2Cu   /* 13 bytes: 0 13 25 36 46 55 63 70 76 81 85 88 90 */

/* Routines translated so far */
void     g4_ck_step(M68K *c);   /* 0x012F4A: checksum step for one password bit */
uint32_t g4_get_bits(M68K *c);  /* 0x012F1A: read D1 bits from the bit buffer at A4 */
void     g4_put_bits(M68K *c);  /* 0x012EDE: write the low D1 bits of D0 to the bit buffer at A4 */
void     g4_split_tri(M68K *c); /* 0x012DE2: D0.w (0..90) -> +$cc/+$ce pair */
uint32_t g4_join_tri(M68K *c);  /* 0x012E0E: +$cc/+$ce pair -> D0.w (0..90) */
void     g4_pw_encode(M68K *c); /* 0x012986: player struct at A0 -> 30-char password at A0+$17f */
int      g4_pw_decode(M68K *c); /* 0x012C08: password at A0+$17f -> player struct; result in Z (1 = valid) */
void     g4_pwB_encode(M68K *c);/* 0x0183E6: Record mode: player + progress -> 32-char password at A0+$1d3 */
int      g4_pwB_decode(M68K *c);/* 0x018542: Record mode: password at A0+$1d3 -> player + progress; Z (1 = valid) */

/* Main loop and game setup (g4_main.c) */
void g4_entry(M68K *c);          /* 0x000400: reset: hardware init, then the endless main loop */
void g4_game_init(M68K *c);      /* 0x000584: power-on defaults */
void g4_play_level(M68K *c);     /* 0x0008C2: load a level and run it frame by frame until it ends */
void g4_level_hud_a58(M68K *c);  /* 0x000A58: per-mode panels drawn when a level starts */
void g4_start_skip(M68K *c);     /* 0x000ADE: Start newly pressed by any player ends the intro countdown */
void g4_level_hud_b1e(M68K *c);  /* 0x000B1E: panel redraw when the intro countdown ends */
void g4_game_setup(M68K *c);     /* 0x000B5A: new game: common state, then the per-mode setup */
void g4_setup_arcade(M68K *c);   /* 0x000C26 */
void g4_setup_quest(M68K *c);    /* 0x000C92 */
void g4_setup_battle(M68K *c);   /* 0x000D3A */
void g4_setup_record(M68K *c);   /* 0x000DD8 */
void g4_set_flags_e79c(M68K *c); /* 0x000E1C: D0.b -> 22 flag bytes $ffe79c-$ffe7b2 (skips $ffe7ab) */
void g4_hiscore_init(M68K *c);   /* 0x000EA2: default high-score tables */
void g4_level_end(M68K *c);      /* 0x000EDC: per-mode handler after each level */
/* Level data (g4_level.c) */
void g4_set_level_ptr(M68K *c);  /* 0x0040D8: level number $ffe758 -> record pointer $ffe75c */
void g4_map_decode(M68K *c);     /* 0x019122: record (pointer at 4(SP)) -> 32x32 map at A4 */
void g4_map_fill(M68K *c);       /* 0x01930A: D4.w x D0.b at A4+D2.w, D2 += D4 */
void g4_map_vrun(M68K *c);       /* 0x019324: up to D4.w x D0.b upward from A4+D2.w, D2 += 1 */
void g4_map_remap(M68K *c);      /* 0x019340: map cells through the mode's 64-byte table */
/* After-level handlers (g4_levelend.c), table 0xEF6 */
void g4_arcade_next(M68K *c);      /* 0x000F06: Arcade: next level, treasure rooms and their bonus */
void g4_arcade_speed(M68K *c);     /* 0x00118C: D0.w from table 0x11EC by level count */
void g4_quest_next(M68K *c);       /* 0x0019EC: Quest: stage or hub handler by $ffe842 */
void g4_quest_from_hub(M68K *c);   /* 0x001A0E: doorway -> area and first stage; end of quest */
void g4_quest_from_stage(M68K *c); /* 0x001B2E: stairs -> next stage, else back to the hub */
void g4_quest_tally(M68K *c);      /* 0x001C6E: end-of-stage E.P./gold screen */
void g4_battle_next(M68K *c);      /* 0x001ED6: next Battle arena */
void g4_record_next(M68K *c);      /* 0x001F22: Record: 0x186D4, then arcade_next */

#endif
