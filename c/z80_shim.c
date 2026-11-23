/*
 * z80_shim.c -- test-only glue for the Z80 translation: hardware callbacks
 * set from Python, and a way to stop the endless program (longjmp) after a
 * given YM2612 write.
 */
#include <setjmp.h>
#include "g4_z80.h"

uint8_t *g4_mem;     /* 68000 memory (ROM) for the bank window */
G4_IO    g4_io;      /* no 68000-side hooks needed */

typedef int  (*ymr_cb)(int port);
typedef int  (*ymw_cb)(int port, int v);   /* returns 1 to stop after this write */
typedef void (*bank_cb)(int v);

static jmp_buf jb;
static ymr_cb hr; static ymw_cb hw; static bank_cb hb;
static int bad_pc;
static long budget, count;

/* called before every instruction (built with -DG4_Z80_COUNT): stop after exactly `budget` */
typedef void (*trace_cb)(Z80 *z, int pc);
static trace_cb htrace;
void z80_set_trace(trace_cb t) { htrace = t; }
void g4z80_tick(Z80 *z, uint16_t pc) { if (++count > budget) longjmp(jb, 1); if (htrace) htrace(z, pc); }

static uint8_t r_(uint16_t p) { return (uint8_t)hr(p); }
static void w_(uint16_t p, uint8_t v) { (void)hw(p, v); }
static void b_(uint8_t v) { if (hb) hb(v); }
static void bad_(Z80 *z, uint16_t pc) { (void)z; bad_pc = pc; longjmp(jb, 2); }

/* run exactly `steps` instructions from reset: 1 = done, -1 - pc = jumped somewhere untranslated */
int z80_test_run(Z80 *z, ymr_cb r, ymw_cb w, bank_cb b, long steps)
{
    int k;
    budget = steps; count = 0;
    hr = r; hw = w; hb = b;
    g4z80_io.ym_read = r_; g4z80_io.ym_write = w_; g4z80_io.bank_write = b_; g4z80_io.bad = bad_;
    k = setjmp(jb);
    if (k == 1) return 1;
    if (k == 2) return -1 - bad_pc;
    g4z80_run(z);
    return 0;
}
