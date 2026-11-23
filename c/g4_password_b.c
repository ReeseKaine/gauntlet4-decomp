/*
 * g4_password_b.c -- Record-mode password encoder/decoder from Gauntlet 4
 * (1993-09), translated from the 68000 instruction by instruction
 * (register model; see g4_m68k.h).
 *
 *   0x0183E6  pwB_encode   player struct at A0 + global progress
 *                          -> 32-char password at A0+$1d3
 *   0x018542  pwB_decode   password at A0+$1d3 -> player struct + global
 *                          progress; result in Z
 *
 * Same machinery as the Quest coder (g4_password.c): put_bits/get_bits/
 * ck_step, the PW_SCRAMBLE alphabet and a name key, here at A0+$1c3. The
 * differences: 160 bits (151 data + 9 checksum) in 32 characters, the first
 * field is the character class and must match the player, no other range
 * checks, and five global progress words are included. Calls are jsr abs.l
 * (return address = call + 6). Standard C only.
 */
#include "g4_m68k.h"

/* Global progress words in work RAM, part of the Record-mode password */
#define G_FFE758  0xFFE758u    /* 7 bits */
#define G_FFE75A  0xFFE75Au    /* 3 bits */
#define G_FFE764  0xFFE764u    /* 3 bits */
#define G_FFE768  0xFFE768u    /* 3 bits */
#define G_FFE770  0xFFE770u    /* 2 bits */

#define SAVED (MV_D(0)|MV_D(1)|MV_D(2)|MV_D(3)|MV_D(4)|MV_D(5)|MV_D(6)|MV_D(7)|MV_A(1)|MV_A(2)|MV_A(3))

/* ------------------------------------------------------------------ */
/* 0x0183E6  pwB_encode                                                */
/*   in:  A0 = player struct, A4 = 64-byte work buffer                 */
/*   out: 32 password chars + NUL at A0+$1d3; registers preserved.     */
/*        No return value (D0 is restored).                            */
/* ------------------------------------------------------------------ */
void g4_pwB_encode(M68K *c)
{
    uint32_t a0;

    movem_push(c, SAVED);                      /* 183E6  movem.l {D0-D7 A1-A3},-(SP) */
    a0 = c->a[0];
    c->d[6] = 0;                               /* 183EA  moveq  #0,D6   checksum */
    c->d[7] = 0;                               /* 183EC  moveq  #0,D7   bit pos  */

    /* --- pack the fields (151 bits) --- */
    SETW(c->d[0], rd16(a0 + 0xB4));            /* 183EE  move.w $b4(A0),D0   class */
    c->d[1] = 2;                               /* 183F2  moveq  #2,D1        */
    CALL(c, 0x0183FAu, g4_put_bits);           /* 183F4  jsr    put_bits     */
    SETW(c->d[0], rd16(a0 + 0x1B0));           /* 183FA  move.w $1b0(A0),D0  */
    c->d[1] = 7;                               /* 183FE  moveq  #7,D1        */
    CALL(c, 0x018406u, g4_put_bits);           /* 18400  jsr    put_bits     */
    SETW(c->d[0], rd16(a0 + 0x1B2));           /* 18406  move.w $1b2(A0),D0  */
    c->d[1] = 4;                               /* 1840A  moveq  #4,D1        */
    CALL(c, 0x018412u, g4_put_bits);           /* 1840C  jsr    put_bits     */
    SETW(c->d[0], rd16(a0 + 0xD0));            /* 18412  move.w $d0(A0),D0   */
    c->d[1] = 7;                               /* 18416  moveq  #7,D1        */
    CALL(c, 0x01841Eu, g4_put_bits);           /* 18418  jsr    put_bits     */
    c->d[0] = rd32(a0 + 0xB6);                 /* 1841E  move.l $b6(A0),D0   */
    c->d[1] = 17;                              /* 18422  moveq  #17,D1       */
    CALL(c, 0x01842Au, g4_put_bits);           /* 18424  jsr    put_bits     */
    SETW(c->d[0], rd16(a0 + 0xCC));            /* 1842A  move.w $cc(A0),D0   */
    c->d[1] = 4;                               /* 1842E  moveq  #4,D1        */
    CALL(c, 0x018436u, g4_put_bits);           /* 18430  jsr    put_bits     */
    SETW(c->d[0], rd16(a0 + 0xCE));            /* 18436  move.w $ce(A0),D0   */
    c->d[1] = 4;                               /* 1843A  moveq  #4,D1        */
    CALL(c, 0x018442u, g4_put_bits);           /* 1843C  jsr    put_bits     */

    c->d[1] = 1;                               /* 18442  moveq  #1,D1        */
    c->a[1] = a0 + 0x96;                       /* 18444  lea    $96(A0),A1   */
    c->d[2] = 5;                               /* 18448  moveq  #5,D2        */
    do {
        SETW(c->d[0], rd16(c->a[1])); c->a[1] += 2;   /* 1844A  move.w (A1)+,D0  */
        CALL(c, 0x018452u, g4_put_bits);              /* 1844C  jsr    put_bits  */
        SETW(c->d[2], W(c->d[2]) - 1);                /* 18452  dbf    D2,$1844a */
    } while (W(c->d[2]) != 0xFFFFu);

    SETW(c->d[0], rd16(a0 + 0x1AA));           /* 18456  move.w $1aa(A0),D0  */
    c->d[1] = 14;                              /* 1845A  moveq  #14,D1       */
    CALL(c, 0x018462u, g4_put_bits);           /* 1845C  jsr    put_bits     */
    c->d[0] = rd32(a0 + 0x204);                /* 18462  move.l $204(A0),D0  */
    c->d[1] = 16;                              /* 18466  moveq  #16,D1       */
    CALL(c, 0x01846Eu, g4_put_bits);           /* 18468  jsr    put_bits     */
    c->d[0] = rd32(a0 + 0xC6);                 /* 1846E  move.l $c6(A0),D0   */
    c->d[1] = 28;                              /* 18472  moveq  #28,D1       */
    CALL(c, 0x01847Au, g4_put_bits);           /* 18474  jsr    put_bits     */
    c->d[0] = rd32(a0 + 0x20C);                /* 1847A  move.l $20c(A0),D0  frames, stored as-is */
    c->d[1] = 21;                              /* 1847E  moveq  #21,D1       */
    CALL(c, 0x018486u, g4_put_bits);           /* 18480  jsr    put_bits     */

    SETW(c->d[0], rd16(G_FFE758));             /* 18486  move.w ($ffe758).l,D0 */
    c->d[1] = 7;                               /* 1848C  moveq  #7,D1        */
    CALL(c, 0x018494u, g4_put_bits);           /* 1848E  jsr    put_bits     */
    SETW(c->d[0], rd16(G_FFE75A));             /* 18494  move.w ($ffe75a).l,D0 */
    c->d[1] = 3;                               /* 1849A  moveq  #3,D1        */
    CALL(c, 0x0184A2u, g4_put_bits);           /* 1849C  jsr    put_bits     */
    SETW(c->d[0], rd16(G_FFE770));             /* 184A2  move.w ($ffe770).l,D0 */
    c->d[1] = 2;                               /* 184A8  moveq  #2,D1        */
    CALL(c, 0x0184B0u, g4_put_bits);           /* 184AA  jsr    put_bits     */
    SETW(c->d[0], rd16(a0 + 0x1C0));           /* 184B0  move.w $1c0(A0),D0  */
    c->d[1] = 3;                               /* 184B4  moveq  #3,D1        */
    CALL(c, 0x0184BCu, g4_put_bits);           /* 184B6  jsr    put_bits     */
    SETW(c->d[0], rd16(G_FFE768));             /* 184BC  move.w ($ffe768).l,D0 */
    c->d[1] = 3;                               /* 184C2  moveq  #3,D1        */
    CALL(c, 0x0184CAu, g4_put_bits);           /* 184C4  jsr    put_bits     */
    SETW(c->d[0], rd16(G_FFE764));             /* 184CA  move.w ($ffe764).l,D0 */
    c->d[1] = 3;                               /* 184D0  moveq  #3,D1        */
    CALL(c, 0x0184D8u, g4_put_bits);           /* 184D2  jsr    put_bits     */

    SETW(c->d[0], W(c->d[6]));                 /* 184D8  move.w D6,D0        checksum of the 151 bits */
    c->d[1] = 9;                               /* 184DA  moveq  #9,D1        */
    CALL(c, 0x0184E2u, g4_put_bits);           /* 184DC  jsr    put_bits     */

    /* --- scramble 160 bits into 32 characters --- */
    c->a[1] = a0 + 0x1D3;                      /* 184E2  lea    $1d3(A0),A1  password out */
    c->a[3] = a0 + 0x1C3;                      /* 184E6  lea    $1c3(A0),A3  key (name)   */
    c->a[2] = PW_SCRAMBLE;                     /* 184EA  lea    ($29cd7).l,A2 */
    c->d[6] = 0;                               /* 184F0  moveq  #0,D6        */
    c->d[7] = 0;                               /* 184F2  moveq  #0,D7        */
    c->d[5] = 0x1F;                            /* 184F4  moveq  #31,D5       */
    do {
        c->d[1] = 5;                                          /* 184F6  moveq  #5,D1     */
        push32(c, 0x0184FEu);                                 /* 184F8  jsr    get_bits  */
        (void)g4_get_bits(c);
        (void)pop32(c);
        SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;            /* 184FE  move.b (A3)+,D2  */
        if (B(c->d[2]) == 0) {                                /* 18500  bne.b  $18508    */
            c->a[3] = a0 + 0x1C3;                             /* 18502  lea    $1c3(A0),A3 key wraps */
            SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;        /* 18506  move.b (A3)+,D2  */
        }
        g4_add_b(c, 0, 5);                                    /* 18508  add.b  D5,D0     */
        g4_add_b(c, 0, 2);                                    /* 1850A  add.b  D2,D0     */
        SETW(c->d[0], W(c->d[0]) & 0x1F);                     /* 1850C  andi.w #$1f,D0   */
        wr8(c->a[1], rd8(c->a[2] + sext16(W(c->d[0]))));     /* 18510  move.b (0,A2,D0.w),(A1)+ */
        c->a[1] += 1;
        SETW(c->d[5], W(c->d[5]) - 1);                        /* 18514  dbf    D5,$184f6  */
    } while (W(c->d[5]) != 0xFFFFu);
    wr8(c->a[1], 0);                           /* 18518  move.b #0,(A1)      terminator */

    movem_pop(c, SAVED);                       /* 1851C  movem.l (SP)+,{D0-D7 A1-A3} */
}                                              /* 18520  rts */

/* ------------------------------------------------------------------ */
/* 0x018542  pwB_decode                                                */
/*   in:  A0 = player struct (password at +$1d3, name at +$1c3,        */
/*        class at +$b4), A4 = 64-byte work buffer                     */
/*   out: Z = 1 and the fields + global progress written if valid;     */
/*        Z = 0 and nothing written if not. All registers preserved    */
/*        (D0 included), so the result is only in Z; the C function    */
/*        also returns it (1 = valid).                                 */
/* ------------------------------------------------------------------ */
int g4_pwB_decode(M68K *c)
{
    uint32_t a0;

    link_a5(c, -0x80);                         /* 18542  link.w A5,#-$80   field stack */
    movem_push(c, SAVED);                      /* 18546  movem.l {D0-D7 A1-A3},-(SP) */
    a0 = c->a[0];

    /* --- descramble 32 characters into 160 bits --- */
    c->a[1] = a0 + 0x1D3;                      /* 1854A  lea    $1d3(A0),A1  password */
    c->a[3] = a0 + 0x1C3;                      /* 1854E  lea    $1c3(A0),A3  key      */
    c->a[2] = PW_SCRAMBLE;                     /* 18552  lea    ($29cd7).l,A2 */
    c->d[2] = 0;                               /* 18558  moveq  #0,D2        */
    c->d[7] = 0;                               /* 1855A  moveq  #0,D7        */
    c->d[5] = 0x1F;                            /* 1855C  moveq  #31,D5       */
    do {
        SETB(c->d[4], rd8(c->a[1])); c->a[1] += 1;            /* 1855E  move.b (A1)+,D4  */
        c->d[0] = 0x1F;                                       /* 18560  moveq  #$1f,D0   */
        for (;;) {                                            /* 18562  cmp.b  (0,A2,D0.w),D4 */
            if (B(c->d[4]) == rd8(c->a[2] + sext16(W(c->d[0]))))
                break;                                        /* 18566  dbeq   D0,$18562: found */
            SETW(c->d[0], W(c->d[0]) - 1);
            if (W(c->d[0]) == 0xFFFFu)
                break;                                        /*        not found: D0.w = -1 */
        }
        SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;            /* 1856A  move.b (A3)+,D2  */
        if (B(c->d[2]) == 0) {                                /* 1856C  bne.b  $18574    */
            c->a[3] = a0 + 0x1C3;                             /* 1856E  lea    $1c3(A0),A3 key wraps */
            SETB(c->d[2], rd8(c->a[3])); c->a[3] += 1;        /* 18572  move.b (A3)+,D2  */
        }
        g4_sub_w(c, 0, 2);                                    /* 18574  sub.w  D2,D0     */
        g4_sub_w(c, 0, 5);                                    /* 18576  sub.w  D5,D0     */
        SETW(c->d[0], W(c->d[0]) & 0x1F);                     /* 18578  andi.w #$1f,D0   */
        c->d[1] = 5;                                          /* 1857C  moveq  #5,D1     */
        CALL(c, 0x018584u, g4_put_bits);                      /* 1857E  jsr    put_bits  */
        SETW(c->d[5], W(c->d[5]) - 1);                        /* 18584  dbf    D5,$1855e */
    } while (W(c->d[5]) != 0xFFFFu);

    /* --- parse the fields onto the frame stack --- */
    c->d[6] = 0;                               /* 18588  moveq  #0,D6        checksum */
    c->d[7] = 0;                               /* 1858A  moveq  #0,D7        bit pos  */
    c->a[3] = c->a[5];                         /* 1858C  movea.l A5,A3       */

    c->d[1] = 2;                               /* 1858E  moveq  #2,D1        */
    CALL(c, 0x018596u, g4_get_bits);           /* 18590  jsr    get_bits     class */
    if (W(c->d[0]) != rd16(a0 + 0xB4))         /* 18596  cmp.w  $b4(A0),D0   */
        goto fail;                             /* 1859A  bne.w  fail         wrong character */

    c->d[1] = 7;                               /* 1859E  moveq  #7,D1        */
    CALL(c, 0x0185A6u, g4_get_bits);           /* 185A0  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 185A6  move.w D0,-(A3)     -> $1b0 */
    c->d[1] = 4;                               /* 185A8  moveq  #4,D1        */
    CALL(c, 0x0185B0u, g4_get_bits);           /* 185AA  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 185B0  move.w D0,-(A3)     -> $1b2 */
    c->d[1] = 7;                               /* 185B2  moveq  #7,D1        */
    CALL(c, 0x0185BAu, g4_get_bits);           /* 185B4  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 185BA  move.w D0,-(A3)     -> $d0  */
    c->d[1] = 17;                              /* 185BC  moveq  #17,D1       */
    CALL(c, 0x0185C4u, g4_get_bits);           /* 185BE  jsr    get_bits     */
    g4_a3_push32(c, c->d[0]);                  /* 185C4  move.l D0,-(A3)     -> $b6  */
    c->d[1] = 4;                               /* 185C6  moveq  #4,D1        */
    CALL(c, 0x0185CEu, g4_get_bits);           /* 185C8  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 185CE  move.w D0,-(A3)     -> $cc  */
    c->d[1] = 4;                               /* 185D0  moveq  #4,D1        */
    CALL(c, 0x0185D8u, g4_get_bits);           /* 185D2  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 185D8  move.w D0,-(A3)     -> $ce  */

    c->d[2] = 5;                               /* 185DA  moveq  #5,D2        */
    c->d[1] = 1;                               /* 185DC  moveq  #1,D1        */
    do {
        CALL(c, 0x0185E4u, g4_get_bits);                      /* 185DE  jsr    get_bits  */
        g4_a3_push16(c, W(c->d[0]));                          /* 185E4  move.w D0,-(A3)  -> $96..$a0 */
        SETW(c->d[2], W(c->d[2]) - 1);                        /* 185E6  dbf    D2,$185de */
    } while (W(c->d[2]) != 0xFFFFu);

    c->d[1] = 14;                              /* 185EA  moveq  #14,D1       */
    CALL(c, 0x0185F2u, g4_get_bits);           /* 185EC  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 185F2  move.w D0,-(A3)     -> $1aa */
    c->d[1] = 16;                              /* 185F4  moveq  #16,D1       */
    CALL(c, 0x0185FCu, g4_get_bits);           /* 185F6  jsr    get_bits     */
    g4_a3_push32(c, c->d[0]);                  /* 185FC  move.l D0,-(A3)     -> $204 */
    c->d[1] = 28;                              /* 185FE  moveq  #28,D1       */
    CALL(c, 0x018606u, g4_get_bits);           /* 18600  jsr    get_bits     */
    g4_a3_push32(c, c->d[0]);                  /* 18606  move.l D0,-(A3)     -> $c6  */
    c->d[1] = 21;                              /* 18608  moveq  #21,D1       */
    CALL(c, 0x018610u, g4_get_bits);           /* 1860A  jsr    get_bits     */
    g4_a3_push32(c, c->d[0]);                  /* 18610  move.l D0,-(A3)     -> $20c */
    c->d[1] = 7;                               /* 18612  moveq  #7,D1        */
    CALL(c, 0x01861Au, g4_get_bits);           /* 18614  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 1861A  move.w D0,-(A3)     -> $ffe758 */
    c->d[1] = 3;                               /* 1861C  moveq  #3,D1        */
    CALL(c, 0x018624u, g4_get_bits);           /* 1861E  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 18624  move.w D0,-(A3)     -> $ffe75a */
    c->d[1] = 2;                               /* 18626  moveq  #2,D1        */
    CALL(c, 0x01862Eu, g4_get_bits);           /* 18628  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 1862E  move.w D0,-(A3)     -> $ffe770 */
    c->d[1] = 3;                               /* 18630  moveq  #3,D1        */
    CALL(c, 0x018638u, g4_get_bits);           /* 18632  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 18638  move.w D0,-(A3)     -> $1c0 */
    c->d[1] = 3;                               /* 1863A  moveq  #3,D1        */
    CALL(c, 0x018642u, g4_get_bits);           /* 1863C  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 18642  move.w D0,-(A3)     -> $ffe768 */
    c->d[1] = 3;                               /* 18644  moveq  #3,D1        */
    CALL(c, 0x01864Cu, g4_get_bits);           /* 18646  jsr    get_bits     */
    g4_a3_push16(c, W(c->d[0]));               /* 1864C  move.w D0,-(A3)     -> $ffe764 */

    /* --- checksum: stored 9 bits must equal the checksum of the 151 data bits --- */
    SETW(c->d[2], W(c->d[6]));                 /* 1864E  move.w D6,D2        */
    c->d[1] = 9;                               /* 18650  moveq  #9,D1        */
    CALL(c, 0x018658u, g4_get_bits);           /* 18652  jsr    get_bits     */
    SETW(c->d[0], W(c->d[0]) ^ W(c->d[2]));    /* 18658  eor.w  D2,D0        */
    c->x = (W(c->d[1]) == 0);                  /* 1865A  subq.w #1,D1        (X = borrow) */
    SETW(c->d[1], W(c->d[1]) - 1);
    do {
        c->x = W(c->d[0]) & 1;                 /* 1865C  lsr.w  #1,D0        X = C = bit out */
        SETW(c->d[0], W(c->d[0]) >> 1);
        if (c->x) goto fail;                   /* 1865E  bcs.b  fail         */
        SETW(c->d[1], W(c->d[1]) - 1);         /* 18660  dbf    D1,$1865c    */
    } while (W(c->d[1]) != 0xFFFFu);

    /* --- valid: commit (fields come off the frame stack in reverse order) --- */
    wr16(G_FFE764,   g4_a3_pop16(c));          /* 18664  move.w (A3)+,($ffe764).l */
    wr16(G_FFE768,   g4_a3_pop16(c));          /* 1866A  move.w (A3)+,($ffe768).l */
    wr16(a0 + 0x1C0, g4_a3_pop16(c));          /* 18670  move.w (A3)+,$1c0(A0)    */
    wr16(G_FFE770,   g4_a3_pop16(c));          /* 18674  move.w (A3)+,($ffe770).l */
    wr16(G_FFE75A,   g4_a3_pop16(c));          /* 1867A  move.w (A3)+,($ffe75a).l */
    wr16(G_FFE758,   g4_a3_pop16(c));          /* 18680  move.w (A3)+,($ffe758).l */
    wr32(a0 + 0x20C, g4_a3_pop32(c));          /* 18686  move.l (A3)+,$20c(A0)    */
    wr32(a0 + 0xC6,  g4_a3_pop32(c));          /* 1868A  move.l (A3)+,$c6(A0)     */
    wr32(a0 + 0x204, g4_a3_pop32(c));          /* 1868E  move.l (A3)+,$204(A0)    */
    wr16(a0 + 0x1AA, g4_a3_pop16(c));          /* 18692  move.w (A3)+,$1aa(A0)    */
    wr16(a0 + 0xA0,  g4_a3_pop16(c));          /* 18696  move.w (A3)+,$a0(A0)     */
    wr16(a0 + 0x9E,  g4_a3_pop16(c));          /* 1869A  move.w (A3)+,$9e(A0)     */
    wr16(a0 + 0x9C,  g4_a3_pop16(c));          /* 1869E  move.w (A3)+,$9c(A0)     */
    wr16(a0 + 0x9A,  g4_a3_pop16(c));          /* 186A2  move.w (A3)+,$9a(A0)     */
    wr16(a0 + 0x98,  g4_a3_pop16(c));          /* 186A6  move.w (A3)+,$98(A0)     */
    wr16(a0 + 0x96,  g4_a3_pop16(c));          /* 186AA  move.w (A3)+,$96(A0)     */
    wr16(a0 + 0xCE,  g4_a3_pop16(c));          /* 186AE  move.w (A3)+,$ce(A0)     */
    wr16(a0 + 0xCC,  g4_a3_pop16(c));          /* 186B2  move.w (A3)+,$cc(A0)     */
    wr32(a0 + 0xB6,  g4_a3_pop32(c));          /* 186B6  move.l (A3)+,$b6(A0)     */
    wr16(a0 + 0xD0,  g4_a3_pop16(c));          /* 186BA  move.w (A3)+,$d0(A0)     */
    wr16(a0 + 0x1B2, g4_a3_pop16(c));          /* 186BE  move.w (A3)+,$1b2(A0)    */
    wr16(a0 + 0x1B0, g4_a3_pop16(c));          /* 186C2  move.w (A3)+,$1b0(A0)    */
    c->d[0] = 0;                               /* 186C6  moveq  #0,D0   Z = 1: valid */
    c->z = 1;
    goto out;                                  /* 186C8  bra.b  $186cc          */

fail:
    c->d[0] = 1;                               /* 186CA  moveq  #1,D0   Z = 0: invalid */
    c->z = 0;
out:
    movem_pop(c, SAVED);                       /* 186CC  movem.l (SP)+,{D0-D7 A1-A3} (flags unchanged) */
    unlk_a5(c);                                /* 186D0  unlk   A5      */
    return (int)c->z;                          /* 186D2  rts            */
}
