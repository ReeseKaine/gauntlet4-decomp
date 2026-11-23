/*
 * g4_bits.c -- password bit-buffer helpers from Gauntlet 4 (1993-09),
 * translated from the 68000 instruction by instruction (register model).
 *
 *   0x012F1A  get_bits   read D1 bits (MSB first) from the buffer at A4,
 *                        starting at bit position D7; returns them in D0
 *   0x012EDE  put_bits   write the low D1 bits of D0 (MSB first) to the
 *                        buffer at A4, starting at bit position D7
 *   0x012F4A  ck_step    fold one bit (D2 = 0 or 1) at bit position D7 into
 *                        the 10-bit password checksum in D6
 *
 * Standard C only. Each statement is annotated with the instruction it
 * implements.
 */
#include "g4_m68k.h"

uint8_t *g4_mem;

/* ROM addresses of ck_step's tables (10 words each). */
#define CK_MOD_TABLE  0x012F76u   /* divisors:  5 5 5 2 5 5 2 3 3 3 */
#define CK_RES_TABLE  0x012F8Eu   /* residues:  0 1 2 0 3 4 1 0 1 2 */

/* ------------------------------------------------------------------ */
/* 0x012F4A  ck_step                                                   */
/*   in:  D2.w = bit value (0/1), D7.w = bit position, D6.w = checksum */
/*   out: D6.w updated; all other registers preserved                  */
/* ------------------------------------------------------------------ */
void g4_ck_step(M68K *c)
{
    /* 12F4A  movem.l {D2 D4 D5 A1 A2},-(SP)
     *        predecrement mode: lowest register ends at the lowest address */
    c->a[7] -= 20;
    wr32(c->a[7] +  0, c->d[2]);
    wr32(c->a[7] +  4, c->d[4]);
    wr32(c->a[7] +  8, c->d[5]);
    wr32(c->a[7] + 12, c->a[1]);
    wr32(c->a[7] + 16, c->a[2]);

    c->a[1] = CK_MOD_TABLE;                 /* 12F4E  lea    ($12f76).l,A1 */
    c->a[2] = CK_RES_TABLE;                 /* 12F54  lea    ($12f8e).l,A2 */
    c->d[5] = 9;                            /* 12F5A  moveq  #9,D5         */

    do {
        uint16_t divisor;
        uint32_t quot, rem;

        c->d[4] = 0;                        /* 12F5C  moveq  #0,D4         */
        SETW(c->d[4], W(c->d[7]));          /* 12F5E  move.w D7,D4         */

        /* 12F60  divu.w (A1)+,D4
         *   32-bit dividend / 16-bit divisor -> remainder:quotient.
         *   On quotient overflow the 68000 leaves D4 unchanged.
         *   (The table never holds 0, so the divide-by-zero trap can't occur.) */
        divisor = rd16(c->a[1]);
        c->a[1] += 2;
        quot = c->d[4] / divisor;
        rem  = c->d[4] % divisor;
        if (quot <= 0xFFFFu)
            c->d[4] = (rem << 16) | quot;

        c->d[4] = (c->d[4] << 16) | (c->d[4] >> 16);   /* 12F62  swap D4  */

        /* 12F64  cmp.w (A2)+,D4 / 12F66  bne.b $12f6a */
        {
            uint16_t residue = rd16(c->a[2]);
            c->a[2] += 2;
            if (W(c->d[4]) == residue)
                SETW(c->d[6], W(c->d[6]) ^ W(c->d[2]));  /* 12F68  eor.w D2,D6 */
        }

        c->x = W(c->d[2]) >> 15;                         /* 12F6A  add.w D2,D2  (X = carry) */
        SETW(c->d[2], W(c->d[2]) + W(c->d[2]));

        SETW(c->d[5], W(c->d[5]) - 1);                   /* 12F6C  dbf D5,$12f5c */
    } while (W(c->d[5]) != 0xFFFFu);

    /* 12F70  movem.l (SP)+,{D2 D4 D5 A1 A2} */
    c->d[2] = rd32(c->a[7] +  0);
    c->d[4] = rd32(c->a[7] +  4);
    c->d[5] = rd32(c->a[7] +  8);
    c->a[1] = rd32(c->a[7] + 12);
    c->a[2] = rd32(c->a[7] + 16);
    c->a[7] += 20;
}                                                        /* 12F74  rts */

/* ------------------------------------------------------------------ */
/* 0x012F1A  get_bits                                                  */
/*   in:  D1.w = bit count (1..32), D7.w = bit position, A4 = buffer,  */
/*        D6.w = running checksum                                      */
/*   out: D0 = bits read, MSB first; D7.w advanced by the count;       */
/*        D6.w updated by ck_step for every bit; D1-D4 preserved       */
/* ------------------------------------------------------------------ */
uint32_t g4_get_bits(M68K *c)
{
    /* 12F1A  movem.l {D1 D2 D3 D4},-(SP) */
    c->a[7] -= 16;
    wr32(c->a[7] +  0, c->d[1]);
    wr32(c->a[7] +  4, c->d[2]);
    wr32(c->a[7] +  8, c->d[3]);
    wr32(c->a[7] + 12, c->d[4]);

    c->d[0] = 0;                                         /* 12F1E  moveq  #0,D0      */
    c->x = (W(c->d[1]) == 0);                            /* 12F20  subq.w #1,D1      (X = borrow) */
    SETW(c->d[1], W(c->d[1]) - 1);

    do {
        uint32_t ea;

        SETW(c->d[3], W(c->d[7]));                       /* 12F22  move.w D7,D3      */
        SETW(c->d[4], W(c->d[7]));                       /* 12F24  move.w D7,D4      */
        c->x = (W(c->d[3]) >> 2) & 1;                    /* 12F26  lsr.w  #3,D3      byte index; X = last bit out */
        SETW(c->d[3], W(c->d[3]) >> 3);
        SETW(c->d[4], W(c->d[4]) & 7);                   /* 12F28  andi.w #7,D4      */
        SETW(c->d[4], W(c->d[4]) ^ 7);                   /* 12F2C  eori.w #7,D4      bit 7-n: MSB first */
        c->d[2] = 0;                                     /* 12F30  moveq  #0,D2      */

        /* 12F32  btst.b D4,(0,A4,D3.w)   index register is sign-extended .w
         * 12F36  beq.b  $12f3c                                              */
        ea = c->a[4] + (uint32_t)(int32_t)(int16_t)W(c->d[3]);
        if (rd8(ea) & (1u << (c->d[4] & 7))) {
            c->d[0] |= 1u << (c->d[1] & 31);             /* 12F38  bset.l D1,D0      */
            c->d[2] = 1;                                 /* 12F3A  moveq  #1,D2      */
        }

        /* 12F3C  bsr.b $12f4a   (return address pushed like the real call) */
        push32(c, 0x012F3Eu);
        g4_ck_step(c);
        (void)pop32(c);                                  /*        rts in ck_step */

        c->x = (W(c->d[7]) == 0xFFFFu);                  /* 12F3E  addq.w #1,D7      (X = carry) */
        SETW(c->d[7], W(c->d[7]) + 1);

        SETW(c->d[1], W(c->d[1]) - 1);                   /* 12F40  dbf    D1,$12f22  */
    } while (W(c->d[1]) != 0xFFFFu);

    /* 12F44  movem.l (SP)+,{D1 D2 D3 D4} */
    c->d[1] = rd32(c->a[7] +  0);
    c->d[2] = rd32(c->a[7] +  4);
    c->d[3] = rd32(c->a[7] +  8);
    c->d[4] = rd32(c->a[7] + 12);
    c->a[7] += 16;

    return c->d[0];                                      /* 12F48  rts               */
}

/* ------------------------------------------------------------------ */
/* 0x012EDE  put_bits                                                  */
/*   in:  D0 = value, D1.w = bit count (1..32), D7.w = bit position,   */
/*        A4 = buffer, D6.w = running checksum                         */
/*   out: the low D1 bits of D0 written MSB first at bit D7; D7.w      */
/*        advanced by the count; D6.w updated by ck_step for every     */
/*        bit; D0-D4 preserved. No return value (D0 is restored).      */
/* ------------------------------------------------------------------ */
void g4_put_bits(M68K *c)
{
    /* 12EDE  movem.l {D0 D1 D2 D3 D4},-(SP) */
    c->a[7] -= 20;
    wr32(c->a[7] +  0, c->d[0]);
    wr32(c->a[7] +  4, c->d[1]);
    wr32(c->a[7] +  8, c->d[2]);
    wr32(c->a[7] + 12, c->d[3]);
    wr32(c->a[7] + 16, c->d[4]);

    c->d[2] = 0x20;                                      /* 12EE2  moveq  #$20,D2    */

    /* 12EE4  sub.w D1,D2      D2.w = 32 - count  (X = borrow) */
    c->x = (W(c->d[2]) < W(c->d[1]));
    SETW(c->d[2], W(c->d[2]) - W(c->d[1]));

    /* 12EE6  lsl.l D2,D0      left-align the value: its top bit -> bit 31.
     *   Shift count is D2 mod 64. X = last bit shifted out; a count of 0
     *   leaves X unchanged; counts of 32 or more clear D0.
     *   (This X is dead: subq.w below overwrites it before anything reads it.
     *   It is kept so every instruction's effect is modelled.) */
    {
        unsigned count = c->d[2] & 63u;
        if (count != 0) {
            c->x = (count <= 32) ? (c->d[0] >> (32 - count)) & 1u : 0;
            c->d[0] = (count < 32) ? c->d[0] << count : 0;
        }
    }

    /* 12EE8  subq.w #1,D1   (X = borrow)
     * 12EEA  bmi.b  $12f14  -- count 0 (or >= $8001) writes nothing */
    c->x = (W(c->d[1]) == 0);
    SETW(c->d[1], W(c->d[1]) - 1);
    if (!(W(c->d[1]) & 0x8000u)) {
        do {
            uint32_t ea, out;

            c->d[2] = 0;                                 /* 12EEC  moveq  #0,D2      (X unaffected) */

            /* 12EEE  roxl.l #1,D0   top bit -> X, old X -> bit 0 */
            out = c->d[0] >> 31;
            c->d[0] = (c->d[0] << 1) | c->x;
            c->x = out;

            /* 12EF0  roxl.w #1,D2   X -> D2 bit 0: D2 = this bit (0/1) */
            out = W(c->d[2]) >> 15;
            SETW(c->d[2], (uint16_t)((W(c->d[2]) << 1) | c->x));
            c->x = out;

            /* 12EF2  bsr.b $12f4a   checksum the bit before storing it */
            push32(c, 0x012EF4u);
            g4_ck_step(c);
            (void)pop32(c);

            SETW(c->d[3], W(c->d[7]));                   /* 12EF4  move.w D7,D3      */
            SETW(c->d[4], W(c->d[7]));                   /* 12EF6  move.w D7,D4      */
            c->x = (W(c->d[3]) >> 2) & 1;                /* 12EF8  lsr.w  #3,D3      byte index; X = last bit out */
            SETW(c->d[3], W(c->d[3]) >> 3);
            SETW(c->d[4], W(c->d[4]) & 7);               /* 12EFA  andi.w #7,D4      */
            SETW(c->d[4], W(c->d[4]) ^ 7);               /* 12EFE  eori.w #7,D4      bit 7-n: MSB first */

            ea = c->a[4] + (uint32_t)(int32_t)(int16_t)W(c->d[3]);
            wr8(ea, (uint8_t)(rd8(ea) | (1u << (c->d[4] & 7))));        /* 12F02  bset.b D4,(0,A4,D3.w) */
            if ((uint8_t)c->d[2] == 0)                                   /* 12F06  tst.b  D2 / bne.b $12f0e */
                wr8(ea, (uint8_t)(rd8(ea) & ~(1u << (c->d[4] & 7))));   /* 12F0A  bclr.b D4,(0,A4,D3.w) */

            c->x = (W(c->d[7]) == 0xFFFFu);              /* 12F0E  addq.w #1,D7      (X = carry) */
            SETW(c->d[7], W(c->d[7]) + 1);

            SETW(c->d[1], W(c->d[1]) - 1);               /* 12F10  dbf    D1,$12eec  */
        } while (W(c->d[1]) != 0xFFFFu);
    }

    /* 12F14  movem.l (SP)+,{D0 D1 D2 D3 D4} */
    c->d[0] = rd32(c->a[7] +  0);
    c->d[1] = rd32(c->a[7] +  4);
    c->d[2] = rd32(c->a[7] +  8);
    c->d[3] = rd32(c->a[7] + 12);
    c->d[4] = rd32(c->a[7] + 16);
    c->a[7] += 20;
}                                                        /* 12F18  rts */
