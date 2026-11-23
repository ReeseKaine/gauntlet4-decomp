/*
 * g4_level.c -- level data: pointer lookup and the 32x32 map decompressor
 * from Gauntlet 4 (Mega Drive, 1993-09), translated from the 68000
 * instruction by instruction (register model, see g4_m68k.h).
 *
 * Level records
 *   set_level_ptr maps the level number ($ffe758, 1-based) to a record:
 *     level-1 < 0x80 : base.l @0x4316 (0xA2E00) + offset.l from table 0x25BC4
 *                      ($ffe790 set) or 0x25DC4 ($ffe790 clear); with
 *                      $ffe799 set the index comes from $ffe766 instead
 *     level-1 >= 0x80: base.l @0x42AA (0x3DD56) + offset.l from table 0x25FC4,
 *                      index (level-1) & 0x7F
 *   Record layout
 *     +0..6   not read through the level pointer (unknown)
 *     +7      option flags (read by 0x2422 into $ffe7b4-$ffe7ba)
 *     +8      hi/lo nibbles -> $ffe7dc / $ffe7e0
 *     +9      hi/lo nibbles -> $ffe7de / $ffe7e2
 *     +10..13 tile palette P0..P3 -> $ffe972 / $ffe974 / $ffe976 / $ffe978
 *     +14..   compressed map
 *
 * Map stream (decoded into 32x32 bytes at A4, row-major; row 0 is
 * pre-filled with tile 1; "last" starts as P1)
 *   00tttttt          one tile t; last = t
 *   01ppnnnn          palette tile T = P[p] (p: 0 1 2 3 = $972 $976 $974 $978);
 *                     last = T; by T's top bits:
 *                       00: n+1 x T  (vertical, upward, if T is $976 or $978)
 *                       01: n+1 x 0, then T        10: T, then n+1 x 0
 *                       11: n+1 x 1, then T        (T written as T & 0x3F)
 *   101vnnnn          n+1 x tile 1 (v: vertical upward)
 *   100nnnnn          n+1 x last
 *   111nnnnn          n+1 x 0, then one 1          110nnnnn  n+1 x 0
 *   Vertical runs write upward from the current cell and stop at row 0;
 *   the cursor then advances by one cell. Decoding stops once 0x400 cells
 *   are reached; then every cell is remapped through a 64-byte table chosen
 *   by mode (0x31D2E, or for Quest 0x31D6E / 0x31DAE / 0x31DEE).
 */
#include "g4_m68k.h"

#define DBF(r)  (SETW((r), W(r) - 1), W(r) != 0xFFFFu)   /* dbf: decrement, true = loop again */

/* ------------------------------------------------------------------ */
/* 0x0040D8  set_level_ptr: $ffe758 -> level record pointer $ffe75c    */
/* ------------------------------------------------------------------ */
void g4_set_level_ptr(M68K *c)
{
    movem_push(c, MV_D(0) | MV_A(0));                   /* 40d8  movem.l {D0 A0},-(SP) */
    SETW(c->d[0], rd16(0xFFE758));                      /* 40dc  move.w ($ffe758).l,D0w */
    SETW(c->d[0], g4_subw(c, W(c->d[0]), 1));           /* 40e2  subq.w #1,D0w */
    if ((int8_t)B(c->d[0]) < 0) {                       /* 40e4  tst.b D0b / 40e6 bpl.b $4108 */
        c->a[0] = 0x025FC4;                             /* 40e8  lea ($25fc4).l,A0 */
        SETW(c->d[0], W(c->d[0]) & 0x7F);               /* 40ee  andi.w #$7f,D0w */
        SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* 40f2  add.w D0w,D0w */
        SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* 40f4  add.w D0w,D0w */
        c->d[0] = rd32(c->a[0] + sext16(W(c->d[0])));   /* 40f6  move.l (0,A0,D0w),D0 */
        {   uint32_t b = rd32(0x0042AA);                /* 40fa  add.l ($42aa).l,D0 */
            c->x = (c->d[0] + b) < c->d[0]; c->d[0] += b; }
        wr32(0xFFE75C, c->d[0]);                        /* 4100  move.l D0,($ffe75c).l */
        goto L4140;                                     /* 4106  bra.b $4140 */
    }
    c->a[0] = 0x025BC4;                                 /* 4108  lea ($25bc4).l,A0 */
    if (rd8(0xFFE790) == 0)                             /* 410e  tst.b ($ffe790).l / 4114 bne.b $411c */
        c->a[0] = 0x025DC4;                             /* 4116  lea ($25dc4).l,A0 */
    if (rd8(0xFFE799) != 0) {                           /* 411c  tst.b ($ffe799).l / 4122 beq.b $412c */
        SETW(c->d[0], rd16(0xFFE766));                  /* 4124  move.w ($ffe766).l,D0w */
        SETW(c->d[0], g4_subw(c, W(c->d[0]), 1));       /* 412a  subq.w #1,D0w */
    }
    SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* 412c  add.w D0w,D0w */
    SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* 412e  add.w D0w,D0w */
    c->d[0] = rd32(c->a[0] + sext16(W(c->d[0])));       /* 4130  move.l (0,A0,D0w),D0 */
    {   uint32_t b = rd32(0x004316);                    /* 4134  add.l ($4316).l,D0 */
        c->x = (c->d[0] + b) < c->d[0]; c->d[0] += b; }
    wr32(0xFFE75C, c->d[0]);                            /* 413a  move.l D0,($ffe75c).l */
L4140:
    movem_pop(c, MV_D(0) | MV_A(0));                    /* 4140  movem.l (SP)+,{D0 A0} */
}                                                       /* 4144  rts */

/* ------------------------------------------------------------------ */
/* 0x01930A  map_fill: D4.w copies of D0.b at A4+D2.w; D2 += D4        */
/* ------------------------------------------------------------------ */
void g4_map_fill(M68K *c)
{
    const uint32_t regs = MV_D(1) | MV_D(4) | MV_A(0) | MV_A(1);
    movem_push(c, regs);                                /* 1930a  movem.l {D1 D4 A0 A1},-(SP) */
    c->a[0] = c->a[4] + sext16(W(c->d[2]));             /* 1930e  lea (0,A4,D2w),A0 */
    SETW(c->d[1], W(c->d[4]));                          /* 19312  move.w D4w,D1w */
    SETW(c->d[1], g4_subw(c, W(c->d[1]), 1));           /* 19314  subq.w #1,D1w */
    do {
        wr8(c->a[0], B(c->d[0])); c->a[0] += 1;         /* 19316  move.b D0b,(A0)+ */
    } while (DBF(c->d[1]));                             /* 19318  dbf D1w,$19316 */
    SETW(c->d[2], g4_addw(c, W(c->d[2]), W(c->d[4])));  /* 1931c  add.w D4w,D2w */
    movem_pop(c, regs);                                 /* 1931e  movem.l (SP)+,{D1 D4 A0 A1} */
}                                                       /* 19322  rts */

/* ------------------------------------------------------------------ */
/* 0x019324  map_vrun: up to D4.w copies of D0.b going up from         */
/*   A4+D2.w (stops above row 0); then D2 += 1                         */
/* ------------------------------------------------------------------ */
void g4_map_vrun(M68K *c)
{
    const uint32_t regs = MV_D(1) | MV_D(2) | MV_D(4) | MV_A(1);
    movem_push(c, regs);                                /* 19324  movem.l {D1 D2 D4 A1},-(SP) */
    SETW(c->d[1], W(c->d[4]));                          /* 19328  move.w D4w,D1w */
    SETW(c->d[1], g4_subw(c, W(c->d[1]), 1));           /* 1932a  subq.w #1,D1w */
    for (;;) {
        uint16_t r;
        wr8(c->a[4] + sext16(W(c->d[2])), B(c->d[0]));  /* 1932c  move.b D0b,(0,A4,D2w) */
        r = g4_subw(c, W(c->d[2]), 0x20);               /* 19330  subi.w #$20,D2w */
        SETW(c->d[2], r);
        if (r & 0x8000u) break;                         /* 19334  dbmi D1w,$1932c  (N set: stop) */
        if (!DBF(c->d[1])) break;                       /*        else decrement, stop at -1 */
    }
    movem_pop(c, regs);                                 /* 19338  movem.l (SP)+,{D1 D2 D4 A1} */
    SETW(c->d[2], g4_addw(c, W(c->d[2]), 1));           /* 1933c  addq.w #1,D2w */
}                                                       /* 1933e  rts */

/* ------------------------------------------------------------------ */
/* 0x019340  map_remap: every cell of the 32x32 map at A4 through the  */
/*   mode's 64-byte table                                              */
/* ------------------------------------------------------------------ */
void g4_map_remap(M68K *c)
{
    const uint32_t regs = MV_D(0) | MV_D(1) | MV_A(0) | MV_A(1);
    movem_push(c, regs);                                /* 19340  movem.l {D0 D1 A0 A1},-(SP) */
    c->a[1] = 0x031D2E;                                 /* 19344  lea ($31d2e).l,A1 */
    if (rd16(0xFFE838) == 1) {                          /* 1934a  cmpi.w #1,($ffe838).l / 19352 bne.b $19386 */
        c->a[1] = 0x031D6E;                             /* 19354  lea ($31d6e).l,A1 */
        if (rd16(0xFFE842) == 1)                        /* 1935a  cmpi.w #1,($ffe842).l / 19362 bne.b $1936c */
            c->a[1] = 0x031DAE;                         /* 19364  lea ($31dae).l,A1 / 1936a bra.b $19386 */
        else if (rd8(0xFFE875) != 0                     /* 1936c  tst.b ($ffe875).l / 19372 bne.b $19380 */
                 || (int16_t)rd16(0xFFE846) > 0x0A)     /* 19374  cmpi.w #$a,($ffe846).l / 1937c bgt.b $19380 */
            c->a[1] = 0x031DEE;                         /* 19380  lea ($31dee).l,A1 */
    }
    c->a[0] = c->a[4];                                  /* 19386  movea.l A4,A0 */
    c->d[0] = 0;                                        /* 19388  moveq #0,D0 */
    SETW(c->d[1], 0x03FF);                              /* 1938a  move.w #$3ff,D1w */
    do {
        SETB(c->d[0], rd8(c->a[0]));                    /* 1938e  move.b (A0),D0b */
        wr8(c->a[0], rd8(c->a[1] + sext16(W(c->d[0])))); c->a[0] += 1;  /* 19390  move.b (0,A1,D0w),(A0)+ */
    } while (DBF(c->d[1]));                             /* 19394  dbf D1w,$1938e */
    movem_pop(c, regs);                                 /* 19398  movem.l (SP)+,{D0 D1 A0 A1} */
}                                                       /* 1939c  rts */

/* ------------------------------------------------------------------ */
/* 0x019122  map_decode: level record (pointer at 4(SP) on entry) ->   */
/*   32x32 map at A4                                                   */
/* ------------------------------------------------------------------ */
#define D4_N4_PLUS1()  (SETW(c->d[4], W(c->d[4]) & 0x0F), SETW(c->d[4], g4_addw(c, W(c->d[4]), 1)))

void g4_map_decode(M68K *c)
{
    const uint32_t regs = 0x1F | MV_A(1) | MV_A(2) | MV_A(3);   /* D0-D4 A1-A3 */
    movem_push(c, regs);                                /* 19122  movem.l {D0-D4 A1-A3},-(SP) */
    c->a[2] = rd32(c->a[7] + 0x24);                     /* 19126  movea.l ($24,SP),A2  record */
    c->a[2] += 8;                                       /* 1912a  addq.l #8,A2 */

    c->d[0] = 0;                                        /* 1912c  moveq #0,D0 */
    SETB(c->d[0], rd8(c->a[2])); c->a[2] += 1;          /* 1912e  move.b (A2)+,D0b */
    SETW(c->d[1], W(c->d[0]));                          /* 19130  move.w D0w,D1w */
    c->x = (W(c->d[0]) >> 3) & 1;                       /* 19132  lsr.w #4,D0w */
    SETW(c->d[0], W(c->d[0]) >> 4);
    wr16(0xFFE7DC, W(c->d[0]));                         /* 19134  move.w D0w,($ffe7dc).l */
    SETW(c->d[1], W(c->d[1]) & 0x0F);                   /* 1913a  andi.w #$f,D1w */
    wr16(0xFFE7E0, W(c->d[1]));                         /* 1913e  move.w D1w,($ffe7e0).l */
    SETB(c->d[0], rd8(c->a[2])); c->a[2] += 1;          /* 19144  move.b (A2)+,D0b */
    SETW(c->d[1], W(c->d[0]));                          /* 19146  move.w D0w,D1w */
    c->x = (W(c->d[0]) >> 3) & 1;                       /* 19148  lsr.w #4,D0w */
    SETW(c->d[0], W(c->d[0]) >> 4);
    wr16(0xFFE7DE, W(c->d[0]));                         /* 1914a  move.w D0w,($ffe7de).l */
    SETW(c->d[1], W(c->d[1]) & 0x0F);                   /* 19150  andi.w #$f,D1w */
    wr16(0xFFE7E2, W(c->d[1]));                         /* 19154  move.w D1w,($ffe7e2).l */
    wr8(0xFFE972, rd8(c->a[2])); c->a[2] += 1;          /* 1915a  move.b (A2)+,($ffe972).l  P0 */
    wr8(0xFFE974, rd8(c->a[2])); c->a[2] += 1;          /* 19160  move.b (A2)+,($ffe974).l  P1 */
    wr8(0xFFE976, rd8(c->a[2])); c->a[2] += 1;          /* 19166  move.b (A2)+,($ffe976).l  P2 */
    wr8(0xFFE978, rd8(c->a[2])); c->a[2] += 1;          /* 1916c  move.b (A2)+,($ffe978).l  P3 */
    c->d[3] = 0;                                        /* 19172  moveq #0,D3 */
    SETB(c->d[3], rd8(0xFFE974));                       /* 19174  move.b ($ffe974).l,D3b  last = P1 */
    c->d[2] = 0;                                        /* 1917a  moveq #0,D2  cursor */
    c->d[4] = 0x20;                                     /* 1917c  moveq #$20,D4 */
    SETW(c->d[0], 1);                                   /* 1917e  move.w #1,D0w */
    JSR(c, 0x019186, 0x01930A);                         /* 19182  bsr.w $1930a  row 0 = tile 1 */

L19186:
    if ((int16_t)W(c->d[2]) >= 0x400) goto L19302;      /* 19186  cmpi.w #$400,D2w / 1918a bge.w $19302 */
    c->d[0] = 0;                                        /* 1918e  moveq #0,D0 */
    SETB(c->d[0], rd8(c->a[2])); c->a[2] += 1;          /* 19190  move.b (A2)+,D0b */
    SETW(c->d[4], W(c->d[0]));                          /* 19192  move.w D0w,D4w */
    SETW(c->d[4], W(c->d[4]) & 0x1F);                   /* 19194  andi.w #$1f,D4w */
    SETW(c->d[1], W(c->d[0]));                          /* 19198  move.w D0w,D1w */
    SETW(c->d[1], W(c->d[1]) & 0xC0);                   /* 1919a  andi.w #$c0,D1w */
    switch (W(c->d[1])) {                               /* 1919e-191b6  cmpi.w #0/$40/$80/$c0,D1w ; beq */
    case 0x40: goto L191D8;
    case 0x80: goto L192A2;
    case 0xC0: goto L192DC;
    default:   break;                                   /* 0x00 (and fall-through) */
    }
    /* L191BA: 00tttttt -- one literal tile */
    c->d[4] = 1;                                        /* 191ba  moveq #1,D4 */
    SETW(c->d[3], W(c->d[0]));                          /* 191bc  move.w D0w,D3w  last = byte */
    SETW(c->d[0], W(c->d[0]) & 0x3F);                   /* 191be  andi.w #$3f,D0w */
    JSR(c, 0x0191C6, 0x01930A);                         /* 191c2  bsr.w $1930a */
    goto L19186;                                        /* 191c6  bra.b $19186 */

L191D8:  /* 01ppnnnn -- palette tile */
    SETW(c->d[0], W(c->d[0]) & 0x30);                   /* 191d8  andi.w #$30,D0w */
    c->x = (c->d[0] >> 1) & 1;                          /* 191dc  lsr.l #2,D0 */
    c->d[0] >>= 2;
    c->a[3] = rd32(0x0191C8 + sext16(W(c->d[0])));      /* 191de  movea.l ($191c8,PC,D0w),A3 */
    c->d[3] = 0;                                        /* 191e2  moveq #0,D3 */
    SETB(c->d[3], rd8(c->a[3]));                        /* 191e4  move.b (A3),D3b  last = P[p] */
    SETW(c->d[1], W(c->d[3]));                          /* 191e6  move.w D3w,D1w */
    SETW(c->d[1], W(c->d[1]) & 0xC0);                   /* 191e8  andi.w #$c0,D1w */
    switch (W(c->d[1])) {                               /* 191ec-19202 */
    case 0x40: goto L19244;
    case 0x80: goto L19262;
    case 0xC0: goto L19288;
    default:   break;
    }
    /* L19206: plain palette tile: vertical if it is P2 or P3 */
    c->d[0] = 0;                                        /* 19206  moveq #0,D0 */
    SETB(c->d[0], rd8(0xFFE976));                       /* 19208  move.b ($ffe976).l,D0b */
    if (W(c->d[0]) != W(c->d[3])) {                     /* 1920e  cmp.w D3w,D0w / 19210 beq.b $1921c */
        SETB(c->d[0], rd8(0xFFE978));                   /* 19212  move.b ($ffe978).l,D0b */
        if (W(c->d[0]) != W(c->d[3])) {                 /* 19218  cmp.w D3w,D0w / 1921a bne.b $19230 */
            D4_N4_PLUS1();                              /* 19230  andi.w #$f,D4w / 19234 addq.w #1,D4w */
            SETW(c->d[0], W(c->d[3]));                  /* 19236  move.w D3w,D0w */
            SETW(c->d[0], W(c->d[0]) & 0x3F);           /* 19238  andi.w #$3f,D0w */
            JSR(c, 0x019240, 0x01930A);                 /* 1923c  bsr.w $1930a */
            goto L19186;                                /* 19240  bra.w $19186 */
        }
    }
    D4_N4_PLUS1();                                      /* 1921c  andi.w #$f,D4w / 19220 addq.w #1,D4w */
    SETW(c->d[0], W(c->d[3]));                          /* 19222  move.w D3w,D0w */
    SETW(c->d[0], W(c->d[0]) & 0x3F);                   /* 19224  andi.w #$3f,D0w */
    JSR(c, 0x01922C, 0x019324);                         /* 19228  bsr.w $19324  vertical */
    goto L19186;                                        /* 1922c  bra.w $19186 */

L19244:  /* palette tile 01xxxxxx: n+1 x 0, then the tile */
    D4_N4_PLUS1();                                      /* 19244  andi.w #$f,D4w / 19248 addq.w #1,D4w */
    SETW(c->d[0], 0);                                   /* 1924a  move.w #0,D0w */
    JSR(c, 0x019252, 0x01930A);                         /* 1924e  bsr.w $1930a */
    c->d[4] = 1;                                        /* 19252  moveq #1,D4 */
    SETW(c->d[0], W(c->d[3]));                          /* 19254  move.w D3w,D0w */
    SETW(c->d[0], W(c->d[0]) & 0x3F);                   /* 19256  andi.w #$3f,D0w */
    JSR(c, 0x01925E, 0x01930A);                         /* 1925a  bsr.w $1930a */
    goto L19186;                                        /* 1925e  bra.w $19186 */

L19262:  /* palette tile 10xxxxxx: the tile, then n+1 x 0 */
    movem_push(c, MV_D(4));                             /* 19262  movem.l {D4},-(SP) */
    c->d[4] = 1;                                        /* 19266  moveq #1,D4 */
    SETW(c->d[0], W(c->d[3]));                          /* 19268  move.w D3w,D0w */
    SETW(c->d[0], W(c->d[0]) & 0x3F);                   /* 1926a  andi.w #$3f,D0w */
    JSR(c, 0x019272, 0x01930A);                         /* 1926e  bsr.w $1930a */
    movem_pop(c, MV_D(4));                              /* 19272  movem.l (SP)+,{D4} */
    D4_N4_PLUS1();                                      /* 19276  andi.w #$f,D4w / 1927a addq.w #1,D4w */
    SETW(c->d[0], 0);                                   /* 1927c  move.w #0,D0w */
    JSR(c, 0x019284, 0x01930A);                         /* 19280  bsr.w $1930a */
    goto L19186;                                        /* 19284  bra.w $19186 */

L19288:  /* palette tile 11xxxxxx: n+1 x 1, then the tile */
    D4_N4_PLUS1();                                      /* 19288  andi.w #$f,D4w / 1928c addq.w #1,D4w */
    SETW(c->d[0], 1);                                   /* 1928e  move.w #1,D0w */
    JSR(c, 0x019294, 0x01930A);                         /* 19292  bsr.b $1930a */
    c->d[4] = 1;                                        /* 19294  moveq #1,D4 */
    SETW(c->d[0], W(c->d[3]));                          /* 19296  move.w D3w,D0w */
    SETW(c->d[0], W(c->d[0]) & 0x3F);                   /* 19298  andi.w #$3f,D0w */
    JSR(c, 0x01929E, 0x01930A);                         /* 1929c  bsr.b $1930a */
    goto L19186;                                        /* 1929e  bra.w $19186 */

L192A2:  /* 10xxxxxx */
    if (c->d[0] & (1u << 5)) {                          /* 192a2  btst #5,D0 / 192a6 beq.b $192ce */
        if (c->d[0] & (1u << 4)) {                      /* 192a8  btst #4,D0 / 192ac beq.b $192be */
            D4_N4_PLUS1();                              /* 192ae  andi.w #$f,D4w / 192b2 addq.w #1,D4w */
            SETW(c->d[0], 1);                           /* 192b4  move.w #1,D0w */
            JSR(c, 0x0192BA, 0x019324);                 /* 192b8  bsr.b $19324  vertical 1s */
            goto L19186;                                /* 192ba  bra.w $19186 */
        }
        D4_N4_PLUS1();                                  /* 192be  andi.w #$f,D4w / 192c2 addq.w #1,D4w */
        SETW(c->d[0], 1);                               /* 192c4  move.w #1,D0w */
        JSR(c, 0x0192CA, 0x01930A);                     /* 192c8  bsr.b $1930a  run of 1s */
        goto L19186;                                    /* 192ca  bra.w $19186 */
    }
    SETW(c->d[4], g4_addw(c, W(c->d[4]), 1));           /* 192ce  addq.w #1,D4w */
    SETW(c->d[0], W(c->d[3]));                          /* 192d0  move.w D3w,D0w */
    SETW(c->d[0], W(c->d[0]) & 0x3F);                   /* 192d2  andi.w #$3f,D0w */
    JSR(c, 0x0192D8, 0x01930A);                         /* 192d6  bsr.b $1930a  repeat last */
    goto L19186;                                        /* 192d8  bra.w $19186 */

L192DC:  /* 11xxxxxx */
    if (c->d[0] & (1u << 5)) {                          /* 192dc  btst #5,D0 / 192e0 beq.b $192f6 */
        SETW(c->d[4], g4_addw(c, W(c->d[4]), 1));       /* 192e2  addq.w #1,D4w */
        SETW(c->d[0], 0);                               /* 192e4  move.w #0,D0w */
        JSR(c, 0x0192EA, 0x01930A);                     /* 192e8  bsr.b $1930a */
        c->d[4] = 1;                                    /* 192ea  moveq #1,D4 */
        SETW(c->d[0], 1);                               /* 192ec  move.w #1,D0w */
        JSR(c, 0x0192F2, 0x01930A);                     /* 192f0  bsr.b $1930a */
        goto L19186;                                    /* 192f2  bra.w $19186 */
    }
    SETW(c->d[4], g4_addw(c, W(c->d[4]), 1));           /* 192f6  addq.w #1,D4w */
    SETW(c->d[0], 0);                                   /* 192f8  move.w #0,D0w */
    JSR(c, 0x0192FE, 0x01930A);                         /* 192fc  bsr.b $1930a */
    goto L19186;                                        /* 192fe  bra.w $19186 */

L19302:
    JSR(c, 0x019304, 0x019340);                         /* 19302  bsr.b $19340  remap */
    movem_pop(c, regs);                                 /* 19304  movem.l (SP)+,{D0-D4 A1-A3} */
}                                                       /* 19308  rts */
