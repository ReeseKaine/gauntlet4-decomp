/*
 * g4_password.c -- Quest-mode password encoder/decoder from Gauntlet 4
 * (1993-09), translated from the 68000 instruction by instruction
 * (register model; see g4_m68k.h).
 *
 *   0x012986  pw_encode   player struct at A0 -> 30-char password at A0+$17f
 *   0x012C08  pw_decode   password at A0+$17f -> player struct; result in Z
 *   0x012DE2  split_tri   D0.w (0..90) -> (+$cc, +$ce) pair
 *   0x012E0E  join_tri    (+$cc, +$ce) pair -> D0.w
 *
 * Both coders use a caller-supplied 64-byte bit buffer at A4 and the player's
 * name at A0+$16f as the scrambling key. Standard C only.
 */
#include "g4_m68k.h"

/* ------------------------------------------------------------------ */
/* 0x012DE2  split_tri                                                 */
/*   in:  D0.w = packed value 0..90                                    */
/*   out: +$cc(A0) = row, +$ce(A0) = column; registers preserved       */
/* ------------------------------------------------------------------ */
void g4_split_tri(M68K *c)
{
    movem_push(c, MV_D(0) | MV_D(1) | MV_D(2) | MV_A(1));     /* 12DE2  movem.l {D0 D1 D2 A1},-(SP) */
    c->a[1] = TRI_TABLE;                                      /* 12DE6  lea    ($12e2c).l,A1 */
    c->d[2] = 0;                                              /* 12DEC  moveq  #0,D2         */
    c->d[1] = 0xC;                                            /* 12DEE  moveq  #$c,D1        */
    do {
        SETB(c->d[2], rd8(c->a[1] + sext16(W(c->d[1]))));    /* 12DF0  move.b (0,A1,D1.w),D2 */
        if (!((int16_t)W(c->d[0]) < (int16_t)W(c->d[2]))) {  /* 12DF4  cmp.w  D2,D0 / 12DF6 blt.b $12e04 */
            wr16(c->a[0] + 0xCC, W(c->d[1]));                 /* 12DF8  move.w D1,$cc(A0)    */
            g4_sub_w(c, 0, 2);                                   /* 12DFC  sub.w  D2,D0         */
            wr16(c->a[0] + 0xCE, W(c->d[0]));                 /* 12DFE  move.w D0,$ce(A0)    */
            break;                                            /* 12E02  bra.b  $12e08        */
        }
        SETW(c->d[1], W(c->d[1]) - 1);                        /* 12E04  dbf    D1,$12df0     */
    } while (W(c->d[1]) != 0xFFFFu);
    movem_pop(c, MV_D(0) | MV_D(1) | MV_D(2) | MV_A(1));      /* 12E08  movem.l (SP)+,{D0 D1 D2 A1} */
}                                                             /* 12E0C  rts */

/* ------------------------------------------------------------------ */
/* 0x012E0E  join_tri                                                  */
/*   in:  +$cc(A0) = row, +$ce(A0) = column                            */
/*   out: D0 = TRI_TABLE[row] + column (upper word 0); D1/A1 preserved */
/* ------------------------------------------------------------------ */
uint32_t g4_join_tri(M68K *c)
{
    movem_push(c, MV_D(1) | MV_A(1));                         /* 12E0E  movem.l {D1 A1},-(SP)  */
    c->a[1] = TRI_TABLE;                                      /* 12E12  lea    ($12e2c).l,A1   */
    SETW(c->d[1], rd16(c->a[0] + 0xCC));                      /* 12E18  move.w $cc(A0),D1      */
    c->d[0] = 0;                                              /* 12E1C  moveq  #0,D0           */
    SETB(c->d[0], rd8(c->a[1] + sext16(W(c->d[1]))));        /* 12E1E  move.b (0,A1,D1.w),D0  */
    {                                                         /* 12E22  add.w  $ce(A0),D0  (X = carry) */
        uint16_t m = rd16(c->a[0] + 0xCE);
        c->x = ((uint32_t)W(c->d[0]) + m) > 0xFFFFu;
        SETW(c->d[0], W(c->d[0]) + m);
    }
    movem_pop(c, MV_D(1) | MV_A(1));                          /* 12E26  movem.l (SP)+,{D1 A1}  */
    return c->d[0];                                           /* 12E2A  rts */
}

/* ------------------------------------------------------------------ */
/* 0x012986  pw_encode                                                 */
/*   in:  A0 = player struct, A4 = 64-byte work buffer                 */
/*   out: 30 password chars + NUL at A0+$17f; registers preserved.     */
/*        No return value (D0 is restored).                            */
/* ------------------------------------------------------------------ */
#define ENC_SAVED (MV_D(0)|MV_D(1)|MV_D(2)|MV_D(3)|MV_D(4)|MV_D(5)|MV_D(6)|MV_D(7)|MV_A(1)|MV_A(2)|MV_A(3))

void g4_pw_encode(M68K *c)
{
    uint32_t a0;

    movem_push(c, ENC_SAVED);                  /* 12986  movem.l {D0-D7 A1-A3},-(SP) */
    a0 = c->a[0];
    c->d[6] = 0;                               /* 1298A  moveq  #0,D6   checksum   */
    c->d[7] = 0;                               /* 1298C  moveq  #0,D7   bit pos    */

    /* --- pack the fields (140 bits) --- */
    c->d[0] = rd32(a0 + 0x134);                /* 1298E  move.l $134(A0),D0 */
    divu_w(&c->d[0], 0x64);                    /* 12992  divu.w #100,D0     */
    c->d[1] = 10;                              /* 12996  moveq  #10,D1      */
    CALL(c, 0x01299Cu, g4_put_bits);           /* 12998  bsr.w  put_bits    */

    c->a[1] = a0 + 0x96;                       /* 1299C  lea    $96(A0),A1  */
    c->d[1] = 1;                               /* 129A0  moveq  #1,D1       */
    c->d[2] = 5;                               /* 129A2  moveq  #5,D2       */
    do {
        SETW(c->d[0], rd16(c->a[1])); c->a[1] += 2;   /* 129A4  move.w (A1)+,D0 */
        CALL(c, 0x0129AAu, g4_put_bits);              /* 129A6  bsr.w  put_bits */
        SETW(c->d[2], W(c->d[2]) - 1);                /* 129AA  dbf    D2,$129a4 */
    } while (W(c->d[2]) != 0xFFFFu);

    SETB(c->d[0], rd8(a0 + 0x16D));            /* 129AE  move.b $16d(A0),D0 */
    c->d[1] = 4;                               /* 129B2  moveq  #4,D1       */
    CALL(c, 0x0129B8u, g4_put_bits);           /* 129B4  bsr.w  put_bits    */

    c->d[0] = rd32(a0 + 0x124);                /* 129B8  move.l $124(A0),D0 */
    c->d[1] = 16;                              /* 129BC  moveq  #16,D1      */
    CALL(c, 0x0129C2u, g4_put_bits);           /* 129BE  bsr.w  put_bits    */

    c->d[0] = rd32(a0 + 0x128);                /* 129C2  move.l $128(A0),D0 */
    c->d[1] = 16;                              /* 129C6  moveq  #16,D1      */
    CALL(c, 0x0129CCu, g4_put_bits);           /* 129C8  bsr.w  put_bits    */

    c->a[1] = a0 + 0x2A0;                      /* 129CC  lea    $2a0(A0),A1 */
    c->d[1] = 3;                               /* 129D0  moveq  #3,D1       */
    c->d[2] = 5;                               /* 129D2  moveq  #5,D2       */
    do {
        SETW(c->d[0], rd16(c->a[1])); c->a[1] += 2;   /* 129D4  move.w (A1)+,D0 */
        CALL(c, 0x0129DAu, g4_put_bits);              /* 129D6  bsr.w  put_bits */
        SETW(c->d[2], W(c->d[2]) - 1);                /* 129DA  dbf    D2,$129d4 */
    } while (W(c->d[2]) != 0xFFFFu);

    c->a[1] = a0 + 0x298;                      /* 129DE  lea    $298(A0),A1 */
    c->d[1] = 3;                               /* 129E2  moveq  #3,D1       */
    SETW(c->d[0], rd16(c->a[1])); c->a[1] += 2;        /* 129E4  move.w (A1)+,D0 */
    CALL(c, 0x0129EAu, g4_put_bits);           /* 129E6  bsr.w  put_bits    */
    c->d[1] = 2;                               /* 129EA  moveq  #2,D1       */
    SETW(c->d[0], rd16(c->a[1])); c->a[1] += 2;        /* 129EC  move.w (A1)+,D0 */
    CALL(c, 0x0129F2u, g4_put_bits);           /* 129EE  bsr.w  put_bits    */
    c->d[1] = 2;                               /* 129F2  moveq  #2,D1       */
    SETW(c->d[0], rd16(c->a[1])); c->a[1] += 2;        /* 129F4  move.w (A1)+,D0 */
    CALL(c, 0x0129FAu, g4_put_bits);           /* 129F6  bsr.w  put_bits    */
    c->d[1] = 1;                               /* 129FA  moveq  #1,D1       */
    SETW(c->d[0], rd16(c->a[1])); c->a[1] += 2;        /* 129FC  move.w (A1)+,D0 */
    CALL(c, 0x012A02u, g4_put_bits);           /* 129FE  bsr.w  put_bits    */

    c->d[1] = 6;                               /* 12A02  moveq  #6,D1       */
    SETB(c->d[0], rd8(a0 + 0x166));            /* 12A04  move.b $166(A0),D0 */
    CALL(c, 0x012A0Cu, g4_put_bits);           /* 12A08  bsr.w  put_bits    */

    push32(c, 0x012A10u);                      /* 12A0C  bsr.w  join_tri    */
    (void)g4_join_tri(c);
    (void)pop32(c);
    c->d[1] = 7;                               /* 12A10  moveq  #7,D1       */
    CALL(c, 0x012A16u, g4_put_bits);           /* 12A12  bsr.w  put_bits    */

    c->d[1] = 3;                               /* 12A16  moveq  #3,D1       */
    SETB(c->d[0], rd8(a0 + 0x1A2));            /* 12A18  move.b $1a2(A0),D0 */
    CALL(c, 0x012A20u, g4_put_bits);           /* 12A1C  bsr.w  put_bits    */

    c->d[1] = 13;                              /* 12A20  moveq  #13,D1      */
    c->d[0] = rd32(a0 + 0x20C);                /* 12A22  move.l $20c(A0),D0 frames */
    divu_w(&c->d[0], 0x3C);                    /* 12A26  divu.w #60,D0      -> seconds */
    if (!((int16_t)W(c->d[0]) < 0x176F))       /* 12A2A  cmpi.w #5999,D0 / 12A2E blt.b $12a34 */
        SETW(c->d[0], 0x176F);                 /* 12A30  move.w #5999,D0    cap at 99:59 */
    CALL(c, 0x012A38u, g4_put_bits);           /* 12A34  bsr.w  put_bits    */

    c->d[1] = 17;                              /* 12A38  moveq  #17,D1      */
    c->d[0] = rd32(a0 + 0x1FC);                /* 12A3A  move.l $1fc(A0),D0 */
    if (!((int32_t)c->d[0] < 0x186A0))         /* 12A3E  cmpi.l #100000,D0 / 12A44 blt.b $12a4c */
        c->d[0] = 0x186A0;                     /* 12A46  move.l #100000,D0  */
    CALL(c, 0x012A50u, g4_put_bits);           /* 12A4C  bsr.w  put_bits    */

    c->d[1] = 7;                               /* 12A50  moveq  #7,D1       */
    c->d[0] = rd32(a0 + 0x200);                /* 12A52  move.l $200(A0),D0 */
    if (!((int16_t)W(c->d[0]) < 0x7F))         /* 12A56  cmpi.w #127,D0 / 12A5A blt.b $12a60 */
        SETW(c->d[0], 0x7F);                   /* 12A5C  move.w #127,D0     */
    CALL(c, 0x012A64u, g4_put_bits);           /* 12A60  bsr.w  put_bits    */

    c->d[1] = 2;                               /* 12A64  moveq  #2,D1       */
    SETW(c->d[0], rd16(a0 + 0xF0));            /* 12A66  move.w $f0(A0),D0  */
    CALL(c, 0x012A6Eu, g4_put_bits);           /* 12A6A  bsr.w  put_bits    */

    c->d[1] = 2;                               /* 12A6E  moveq  #2,D1       */
    SETW(c->d[0], rd16(a0 + 0xB4));            /* 12A70  move.w $b4(A0),D0  class */
    CALL(c, 0x012A78u, g4_put_bits);           /* 12A74  bsr.w  put_bits    */

    c->d[1] = 5;                               /* 12A78  moveq  #5,D1       */
    SETW(c->d[0], rd16(a0 + 0x14A));           /* 12A7A  move.w $14a(A0),D0 */
    if (!((int16_t)W(c->d[0]) < 0x1F))         /* 12A7E  cmpi.w #31,D0 / 12A82 blt.b $12a88 */
        SETW(c->d[0], 0x1F);                   /* 12A84  move.w #31,D0      */
    CALL(c, 0x012A8Cu, g4_put_bits);           /* 12A88  bsr.w  put_bits    */

    SETW(c->d[0], W(c->d[6]));                 /* 12A8C  move.w D6,D0       checksum of the 140 bits */
    c->d[1] = 10;                              /* 12A8E  moveq  #10,D1      */
    CALL(c, 0x012A94u, g4_put_bits);           /* 12A90  bsr.w  put_bits    */

    /* --- scramble 150 bits into 30 characters --- */
    c->a[1] = a0 + 0x17F;                      /* 12A94  lea    $17f(A0),A1 password out */
    c->a[3] = a0 + 0x16F;                      /* 12A98  lea    $16f(A0),A3 key (name)   */
    c->a[2] = PW_SCRAMBLE;                     /* 12A9C  lea    ($29cd7).l,A2            */
    c->d[6] = 0;                               /* 12AA2  moveq  #0,D6       */
    c->d[7] = 0;                               /* 12AA4  moveq  #0,D7       */
    c->d[5] = 0x1D;                            /* 12AA6  moveq  #29,D5      */
    do {
        c->d[1] = 5;                                          /* 12AA8  moveq  #5,D1     */
        push32(c, 0x012AAEu);                                 /* 12AAA  bsr.w  get_bits  */
        (void)g4_get_bits(c);
        (void)pop32(c);
        SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;            /* 12AAE  move.b (A3)+,D2  */
        if (B(c->d[2]) == 0) {                                /* 12AB0  bne.b  $12ab8    */
            c->a[3] = a0 + 0x16F;                             /* 12AB2  lea    $16f(A0),A3 key wraps */
            SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;        /* 12AB6  move.b (A3)+,D2  */
        }
        g4_add_b(c, 0, 5);                                       /* 12AB8  add.b  D5,D0     */
        g4_add_b(c, 0, 2);                                       /* 12ABA  add.b  D2,D0     */
        SETW(c->d[0], W(c->d[0]) & 0x1F);                     /* 12ABC  andi.w #$1f,D0   */
        wr8(c->a[1], rd8(c->a[2] + sext16(W(c->d[0]))));     /* 12AC0  move.b (0,A2,D0.w),(A1)+ */
        c->a[1] += 1;
        SETW(c->d[5], W(c->d[5]) - 1);                        /* 12AC4  dbf    D5,$12aa8  */
    } while (W(c->d[5]) != 0xFFFFu);
    wr8(c->a[1], 0);                           /* 12AC8  move.b #0,(A1)     terminator */

    movem_pop(c, ENC_SAVED);                   /* 12ACC  movem.l (SP)+,{D0-D7 A1-A3} */
}                                              /* 12AD0  rts */

/* ------------------------------------------------------------------ */
/* 0x012C08  pw_decode                                                 */
/*   in:  A0 = player struct (password at +$17f, name at +$16f),       */
/*        A4 = 64-byte work buffer                                     */
/*   out: Z = 1 and the fields written into the struct if the password */
/*        is valid; Z = 0 and the struct untouched if not. All         */
/*        registers preserved (D0 included), so the result is only in  */
/*        Z; the C function also returns it (1 = valid).               */
/* ------------------------------------------------------------------ */
#define DEC_SAVED ENC_SAVED

int g4_pw_decode(M68K *c)
{
    uint32_t a0;

    link_a5(c, -0x80);                         /* 12C08  link.w A5,#-$80   field stack */
    movem_push(c, DEC_SAVED);                  /* 12C0C  movem.l {D0-D7 A1-A3},-(SP) */
    a0 = c->a[0];

    /* --- descramble 30 characters into 150 bits --- */
    c->a[1] = a0 + 0x17F;                      /* 12C10  lea    $17f(A0),A1 password */
    c->a[3] = a0 + 0x16F;                      /* 12C14  lea    $16f(A0),A3 key      */
    c->a[2] = PW_SCRAMBLE;                     /* 12C18  lea    ($29cd7).l,A2        */
    c->d[2] = 0;                               /* 12C1E  moveq  #0,D2       */
    c->d[7] = 0;                               /* 12C20  moveq  #0,D7       */
    c->d[5] = 0x1D;                            /* 12C22  moveq  #29,D5      */
    do {
        SETB(c->d[4], rd8(c->a[1])); c->a[1] += 1;            /* 12C24  move.b (A1)+,D4  */
        c->d[0] = 0x1F;                                       /* 12C26  moveq  #$1f,D0   */
        for (;;) {                                            /* 12C28  cmp.b  (0,A2,D0.w),D4 */
            if (B(c->d[4]) == rd8(c->a[2] + sext16(W(c->d[0]))))
                break;                                        /* 12C2C  dbeq   D0,$12c28: found */
            SETW(c->d[0], W(c->d[0]) - 1);
            if (W(c->d[0]) == 0xFFFFu)
                break;                                        /*        not found: D0.w = -1 */
        }
        SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;            /* 12C30  move.b (A3)+,D2  */
        if (B(c->d[2]) == 0) {                                /* 12C32  bne.b  $12c3a    */
            c->a[3] = a0 + 0x16F;                             /* 12C34  lea    $16f(A0),A3 key wraps */
            SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;        /* 12C38  move.b (A3)+,D2  */
        }
        g4_sub_w(c, 0, 2);                                       /* 12C3A  sub.w  D2,D0     */
        g4_sub_w(c, 0, 5);                                       /* 12C3C  sub.w  D5,D0     */
        SETW(c->d[0], W(c->d[0]) & 0x1F);                     /* 12C3E  andi.w #$1f,D0   */
        c->d[1] = 5;                                          /* 12C42  moveq  #5,D1     */
        CALL(c, 0x012C48u, g4_put_bits);                      /* 12C44  bsr.w  put_bits  */
        SETW(c->d[5], W(c->d[5]) - 1);                        /* 12C48  dbf    D5,$12c24 */
    } while (W(c->d[5]) != 0xFFFFu);

    /* --- parse the fields onto the frame stack, checking ranges --- */
    c->d[6] = 0;                               /* 12C4C  moveq  #0,D6       checksum */
    c->d[7] = 0;                               /* 12C4E  moveq  #0,D7       bit pos  */
    c->a[3] = c->a[5];                         /* 12C50  movea.l A5,A3      */

    c->d[1] = 10;                              /* 12C52  moveq  #10,D1      */
    CALL(c, 0x012C58u, g4_get_bits);           /* 12C54  bsr.w  get_bits    */
    c->d[0] = (uint32_t)W(c->d[0]) * 0x64;     /* 12C58  mulu.w #100,D0     */
    g4_a3_push32(c, c->d[0]);                     /* 12C5C  move.l D0,-(A3)    */
    if ((int32_t)c->d[0] < 0x7D0) goto fail;   /* 12C5E  cmpi.l #2000,D0 / 12C64 blt.w fail */
    if ((int32_t)c->d[0] > 0x1863C) goto fail; /* 12C68  cmpi.l #99900,D0 / 12C6E bgt.w fail */

    c->d[1] = 1;                               /* 12C72  moveq  #1,D1       */
    c->d[2] = 5;                               /* 12C74  moveq  #5,D2       */
    do {
        CALL(c, 0x012C7Au, g4_get_bits);                      /* 12C76  bsr.w  get_bits  */
        g4_a3_push16(c, W(c->d[0]));                             /* 12C7A  move.w D0,-(A3)  */
        if ((int16_t)W(c->d[0]) < 0) goto fail;               /* 12C7C  cmpi.w #0,D0 / 12C80 blt.w fail */
        if ((int16_t)W(c->d[0]) > 1) goto fail;               /* 12C84  cmpi.w #1,D0 / 12C88 bgt.w fail */
        SETW(c->d[2], W(c->d[2]) - 1);                        /* 12C8C  dbf    D2,$12c76 */
    } while (W(c->d[2]) != 0xFFFFu);

    c->d[1] = 4;                               /* 12C90  moveq  #4,D1       */
    CALL(c, 0x012C96u, g4_get_bits);           /* 12C92  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12C96  move.w D0,-(A3)    */
    c->d[1] = 16;                              /* 12C98  moveq  #16,D1      */
    CALL(c, 0x012C9Eu, g4_get_bits);           /* 12C9A  bsr.w  get_bits    */
    g4_a3_push32(c, c->d[0]);                     /* 12C9E  move.l D0,-(A3)    */
    c->d[1] = 16;                              /* 12CA0  moveq  #16,D1      */
    CALL(c, 0x012CA6u, g4_get_bits);           /* 12CA2  bsr.w  get_bits    */
    g4_a3_push32(c, c->d[0]);                     /* 12CA6  move.l D0,-(A3)    */

    c->d[1] = 3;                               /* 12CA8  moveq  #3,D1       */
    c->d[2] = 5;                               /* 12CAA  moveq  #5,D2       */
    do {
        CALL(c, 0x012CB0u, g4_get_bits);                      /* 12CAC  bsr.w  get_bits  */
        g4_a3_push16(c, W(c->d[0]));                             /* 12CB0  move.w D0,-(A3)  */
        if ((int16_t)W(c->d[0]) < 0) goto fail;               /* 12CB2  cmpi.w #0,D0 / 12CB6 blt.w fail */
        if ((int16_t)W(c->d[0]) > 7) goto fail;               /* 12CBA  cmpi.w #7,D0 / 12CBE bgt.w fail */
        SETW(c->d[2], W(c->d[2]) - 1);                        /* 12CC2  dbf    D2,$12cac */
    } while (W(c->d[2]) != 0xFFFFu);

    c->d[1] = 3;                               /* 12CC6  moveq  #3,D1       */
    CALL(c, 0x012CCCu, g4_get_bits);           /* 12CC8  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12CCC  move.w D0,-(A3)    */
    c->d[1] = 2;                               /* 12CCE  moveq  #2,D1       */
    CALL(c, 0x012CD4u, g4_get_bits);           /* 12CD0  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12CD4  move.w D0,-(A3)    */
    c->d[1] = 2;                               /* 12CD6  moveq  #2,D1       */
    CALL(c, 0x012CDCu, g4_get_bits);           /* 12CD8  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12CDC  move.w D0,-(A3)    */
    c->d[1] = 1;                               /* 12CDE  moveq  #1,D1       */
    CALL(c, 0x012CE4u, g4_get_bits);           /* 12CE0  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12CE4  move.w D0,-(A3)    */
    c->d[1] = 6;                               /* 12CE6  moveq  #6,D1       */
    CALL(c, 0x012CECu, g4_get_bits);           /* 12CE8  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12CEC  move.w D0,-(A3)    */

    c->d[1] = 7;                               /* 12CEE  moveq  #7,D1       */
    CALL(c, 0x012CF4u, g4_get_bits);           /* 12CF0  bsr.w  get_bits    */
    if ((int16_t)W(c->d[0]) >= 0x5B) goto fail;/* 12CF4  cmpi.w #91,D0 / 12CF8 bge.w fail */
    g4_a3_push16(c, W(c->d[0]));                  /* 12CFC  move.w D0,-(A3)    */

    c->d[1] = 3;                               /* 12CFE  moveq  #3,D1       */
    CALL(c, 0x012D04u, g4_get_bits);           /* 12D00  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12D04  move.w D0,-(A3)    */
    c->d[1] = 13;                              /* 12D06  moveq  #13,D1      */
    CALL(c, 0x012D0Cu, g4_get_bits);           /* 12D08  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12D0C  move.w D0,-(A3)    */
    c->d[1] = 17;                              /* 12D0E  moveq  #17,D1      */
    CALL(c, 0x012D14u, g4_get_bits);           /* 12D10  bsr.w  get_bits    */
    g4_a3_push32(c, c->d[0]);                     /* 12D14  move.l D0,-(A3)    */
    c->d[1] = 7;                               /* 12D16  moveq  #7,D1       */
    CALL(c, 0x012D1Cu, g4_get_bits);           /* 12D18  bsr.w  get_bits    */
    g4_a3_push32(c, c->d[0]);                     /* 12D1C  move.l D0,-(A3)    */
    c->d[1] = 2;                               /* 12D1E  moveq  #2,D1       */
    CALL(c, 0x012D24u, g4_get_bits);           /* 12D20  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12D24  move.w D0,-(A3)    */
    c->d[1] = 2;                               /* 12D26  moveq  #2,D1       */
    CALL(c, 0x012D2Cu, g4_get_bits);           /* 12D28  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12D2C  move.w D0,-(A3)    */
    c->d[1] = 5;                               /* 12D2E  moveq  #5,D1       */
    CALL(c, 0x012D34u, g4_get_bits);           /* 12D30  bsr.w  get_bits    */
    g4_a3_push16(c, W(c->d[0]));                  /* 12D34  move.w D0,-(A3)    */

    /* --- checksum: stored 10 bits must equal the checksum of the 140 data bits --- */
    SETW(c->d[2], W(c->d[6]));                 /* 12D36  move.w D6,D2       */
    c->d[1] = 10;                              /* 12D38  moveq  #10,D1      */
    CALL(c, 0x012D3Eu, g4_get_bits);           /* 12D3A  bsr.w  get_bits    */
    SETW(c->d[0], W(c->d[0]) ^ W(c->d[2]));    /* 12D3E  eor.w  D2,D0       */
    c->x = (W(c->d[1]) == 0);                  /* 12D40  subq.w #1,D1       (X = borrow) */
    SETW(c->d[1], W(c->d[1]) - 1);
    do {
        c->x = W(c->d[0]) & 1;                 /* 12D42  lsr.w  #1,D0       X = C = bit out */
        SETW(c->d[0], W(c->d[0]) >> 1);
        if (c->x) goto fail;                   /* 12D44  bcs.w  fail        */
        SETW(c->d[1], W(c->d[1]) - 1);         /* 12D48  dbf    D1,$12d42   */
    } while (W(c->d[1]) != 0xFFFFu);

    /* --- valid: commit the fields to the player struct --- */
    wr16(a0 + 0x14A, g4_a3_pop16(c));            /* 12D4C  move.w (A3)+,$14a(A0) */
    SETW(c->d[0], g4_a3_pop16(c));               /* 12D50  move.w (A3)+,D0       */
    if (W(c->d[0]) != rd16(a0 + 0xB4)) {      /* 12D52  cmp.w  $b4(A0),D0 / 12D56 beq.b $12d5c */
        uint16_t v = rd16(a0 + 0x14A);        /* 12D58  addq.w #1,$14a(A0)  (X = carry) */
        c->x = (v == 0xFFFFu);
        wr16(a0 + 0x14A, (uint16_t)(v + 1));
    }
    wr16(a0 + 0xF0,  g4_a3_pop16(c));            /* 12D5C  move.w (A3)+,$f0(A0)  */
    wr32(a0 + 0x200, g4_a3_pop32(c));            /* 12D60  move.l (A3)+,$200(A0) */
    wr32(a0 + 0x1FC, g4_a3_pop32(c));            /* 12D64  move.l (A3)+,$1fc(A0) */
    SETW(c->d[0], g4_a3_pop16(c));               /* 12D68  move.w (A3)+,D0       */
    c->d[0] = (uint32_t)W(c->d[0]) * 0x3C;    /* 12D6A  mulu.w #60,D0         seconds -> frames */
    wr32(a0 + 0x20C, c->d[0]);                /* 12D6E  move.l D0,$20c(A0)    */
    SETW(c->d[0], g4_a3_pop16(c));               /* 12D72  move.w (A3)+,D0       */
    wr8(a0 + 0x1A2, B(c->d[0]));              /* 12D74  move.b D0,$1a2(A0)    */
    SETW(c->d[0], g4_a3_pop16(c));               /* 12D78  move.w (A3)+,D0       */
    CALL(c, 0x012D7Cu, g4_split_tri);         /* 12D7A  bsr.b  split_tri      */
    SETW(c->d[0], g4_a3_pop16(c));               /* 12D7C  move.w (A3)+,D0       */
    wr8(a0 + 0x166, B(c->d[0]));              /* 12D7E  move.b D0,$166(A0)    */
    wr16(a0 + 0x29E, g4_a3_pop16(c));            /* 12D82  move.w (A3)+,$29e(A0) */
    wr16(a0 + 0x29C, g4_a3_pop16(c));            /* 12D86  move.w (A3)+,$29c(A0) */
    wr16(a0 + 0x29A, g4_a3_pop16(c));            /* 12D8A  move.w (A3)+,$29a(A0) */
    wr16(a0 + 0x298, g4_a3_pop16(c));            /* 12D8E  move.w (A3)+,$298(A0) */
    wr16(a0 + 0x2AA, g4_a3_pop16(c));            /* 12D92  move.w (A3)+,$2aa(A0) */
    wr16(a0 + 0x2A8, g4_a3_pop16(c));            /* 12D96  move.w (A3)+,$2a8(A0) */
    wr16(a0 + 0x2A6, g4_a3_pop16(c));            /* 12D9A  move.w (A3)+,$2a6(A0) */
    wr16(a0 + 0x2A4, g4_a3_pop16(c));            /* 12D9E  move.w (A3)+,$2a4(A0) */
    wr16(a0 + 0x2A2, g4_a3_pop16(c));            /* 12DA2  move.w (A3)+,$2a2(A0) */
    wr16(a0 + 0x2A0, g4_a3_pop16(c));            /* 12DA6  move.w (A3)+,$2a0(A0) */
    wr32(a0 + 0x128, g4_a3_pop32(c));            /* 12DAA  move.l (A3)+,$128(A0) */
    wr32(a0 + 0x124, g4_a3_pop32(c));            /* 12DAE  move.l (A3)+,$124(A0) */
    SETW(c->d[0], g4_a3_pop16(c));               /* 12DB2  move.w (A3)+,D0       */
    wr8(a0 + 0x16D, B(c->d[0]));              /* 12DB4  move.b D0,$16d(A0)    */
    wr16(a0 + 0xA0, g4_a3_pop16(c));             /* 12DB8  move.w (A3)+,$a0(A0)  */
    wr16(a0 + 0x9E, g4_a3_pop16(c));             /* 12DBC  move.w (A3)+,$9e(A0)  */
    wr16(a0 + 0x9C, g4_a3_pop16(c));             /* 12DC0  move.w (A3)+,$9c(A0)  */
    wr16(a0 + 0x9A, g4_a3_pop16(c));             /* 12DC4  move.w (A3)+,$9a(A0)  */
    wr16(a0 + 0x98, g4_a3_pop16(c));             /* 12DC8  move.w (A3)+,$98(A0)  */
    wr16(a0 + 0x96, g4_a3_pop16(c));             /* 12DCC  move.w (A3)+,$96(A0)  */
    wr32(a0 + 0x134, g4_a3_pop32(c));            /* 12DD0  move.l (A3)+,$134(A0) */
    c->d[0] = 0;                              /* 12DD4  moveq  #0,D0   Z = 1: valid */
    c->z = 1;
    goto out;                                 /* 12DD6  bra.b  $12dda          */

fail:
    c->d[0] = 1;                              /* 12DD8  moveq  #1,D0   Z = 0: invalid */
    c->z = 0;
out:
    movem_pop(c, DEC_SAVED);                  /* 12DDA  movem.l (SP)+,{D0-D7 A1-A3} (flags unchanged) */
    unlk_a5(c);                               /* 12DDE  unlk   A5      */
    return (int)c->z;                         /* 12DE0  rts            */
}
