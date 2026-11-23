/*
 * test_shim.c -- test-only glue: routes g4_external() and hardware accesses
 * to callbacks set from Python (ctypes), and lets a callback stop a routine
 * that never returns (the main loop) by longjmp back to g4_test_run().
 */
#include <setjmp.h>
#include <stddef.h>
#include "g4_m68k.h"

typedef int      (*ext_cb)(M68K *c, uint32_t target);   /* returns bit0 = Z, bit1 = stop, bit2 = X,
                                                          bit3 = full CCR in bits 8-12 (N Z V C X as on the CPU) */
typedef uint32_t (*rd_cb)(uint32_t addr, int size);
typedef void     (*wr_cb)(uint32_t addr, uint32_t value, int size);

void (*g4_lookup(uint32_t addr))(M68K *);

static ext_cb  host_ext;
static jmp_buf stop_jb;

#ifdef G4_TRACE
typedef void (*trace_cb)(M68K *c, uint32_t pc);
static trace_cb host_trace;
void g4_test_set_trace(trace_cb t) { host_trace = t; }
void g4_trace(M68K *c, uint32_t pc) { if (host_trace) host_trace(c, pc); }
#endif

void g4_test_set(ext_cb e, rd_cb r, wr_cb w)
{
    host_ext = e; g4_io.read = r; g4_io.write = w;
}

void g4_external(M68K *c, uint32_t target)
{
    int r = host_ext(c, target);
    c->z = (uint32_t)(r & 1);
    c->x = (uint32_t)((r >> 2) & 1);
    if (r & 8) {                                   /* full condition codes from the script */
        uint32_t ccr = ((uint32_t)r >> 8) & 0x1F;
        c->x = (ccr >> 4) & 1; c->n = (ccr >> 3) & 1; c->z = (ccr >> 2) & 1; c->v = (ccr >> 1) & 1; c->cf = ccr & 1;
    }
    if (r & 2) longjmp(stop_jb, 1);
}

/* Like g4_test_run, for an interrupt handler: pushes an exception frame
 * (return PC, then SR) the way the CPU does; the handler's rte is the pop. */
int g4_test_run_irq(M68K *c, uint32_t addr, uint32_t sr)
{
    void (*fn)(M68K *) = g4_lookup(addr);
    if (!fn) return -1;
    if (setjmp(stop_jb)) return 1;
    push32(c, 0xE00000);
    c->a[7] -= 2; wr16(c->a[7], (uint16_t)sr);
    fn(c);
    { uint16_t s = rd16(c->a[7]); c->a[7] += 2;   /* rte: SR, then PC */
      c->sr = s; c->x = (s >> 4) & 1; c->n = (s >> 3) & 1; c->z = (s >> 2) & 1; c->v = (s >> 1) & 1; c->cf = s & 1; }
    (void)pop32(c);
    return 0;
}

/* Runs the translated routine at addr (its rts is the caller's pop, as with
 * CALL()). Returns 0 if it returned normally, 1 if a callback stopped it,
 * -1 if addr is not translated. */
int g4_test_run(M68K *c, uint32_t addr)
{
    void (*fn)(M68K *) = g4_lookup(addr);
    if (!fn) return -1;
    if (setjmp(stop_jb)) return 1;
    push32(c, 0xE00000);           /* return address = SENTINEL, as the ROM run gets */
    fn(c);
    (void)pop32(c);
    return 0;
}
