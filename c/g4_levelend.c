/*
 * g4_levelend.c -- the per-mode handlers that run after each level (table
 * 0xEF6, called by level_end 0xEDC) from Gauntlet 4 (Mega Drive, 1993-09),
 * translated from the 68000 instruction by instruction (register model, see
 * g4_m68k.h).
 *
 *   0x000F06  arcade_next     Arcade (and Record, via 0x1F22): next level number,
 *                             treasure-room countdown, treasure-room bonus screen
 *   0x00118C  arcade_speed    table lookup (0x11EC) for $ffe780 by level count
 *   0x0019EC  quest_next      Quest: 0x1B2E after a tower stage, 0x1A0E after the hub
 *   0x001A0E  quest_from_hub  which doorway was taken -> area and first stage;
 *                             the exit at y = 0x1F ends the game (passwords saved)
 *   0x001B2E  quest_from_stage stairs taken ($ffe85c = +1 / -1) -> next stage;
 *                             none -> back to the hub (0x8E)
 *   0x001C6E  quest_tally     end-of-stage "E.P." / "GOLD" screen and totals
 *   0x001ED6  battle_next     next Battle arena (0x81-0x8D, rotating offset)
 *   0x001F22  record_next     0x186D4, then arcade_next
 *
 * RAM variables (names provisional)
 *   $ffe760.w levels played      $ffe758.w level number   $ffe75a.w level offset
 *   $ffe762.w level to return to after a treasure room
 *   $ffe768.w levels until the next treasure room   $ffe799.b in a treasure room
 *   $ffe76a.w treasure-room seconds (table 0x105E)
 *   $ffe842.w 1 = Quest hub   $ffe844.w area (0 Earth, 1 Water, 2 Fire, 3 Wind, 4 Castle)
 *   $ffe846.w stage           $ffe85c.w stairs direction   $ffe85e.w stage bonus count
 *   player +$12E/+$12C stage E.P./gold, +$128/+$124 totals (capped at $FFFF)
 *
 * Each statement is annotated with the instruction it implements.
 */
#include "g4_m68k.h"

#define DBF(r)  (SETW((r), W(r) - 1), W(r) != 0xFFFFu)   /* dbf: decrement, true = loop again */

static inline void     push16(M68K *c, uint16_t v) { c->a[7] -= 2; wr16(c->a[7], v); }
static inline uint16_t pop16 (M68K *c)             { uint16_t v = rd16(c->a[7]); c->a[7] += 2; return v; }
static inline uint32_t g4_addl(M68K *c, uint32_t a, uint32_t b) { c->x = (a + b) < a; return a + b; }
#define ADDW(r, v)   SETW((r), g4_addw(c, W(r), (uint16_t)(v)))
#define SUBW(r, v)   SETW((r), g4_subw(c, W(r), (uint16_t)(v)))
#define MEM_ADDW(a, v) wr16((a), g4_addw(c, rd16(a), (uint16_t)(v)))
#define MULU(r, v)   ((r) = (uint32_t)W(r) * (uint16_t)(v))

/* ------------------------------------------------------------------ */
/* 0x000F06  arcade_next                                               */
/* ------------------------------------------------------------------ */
void g4_arcade_next(M68K *c)
{
    const uint32_t regs = 0xFF | MV_A(0) | MV_A(1);
    movem_push(c, regs);                                /* f06  movem.l {D0-D7 A0 A1},-(SP) */
    if (rd8(0xFFE799) != 0) goto L1068;                 /* f0a  tst.b ($ffe799).l / f10 bne.w $1068 */

    SETW(c->d[0], rd16(0xFFE760));                      /* f14  move.w ($ffe760).l,D0w */
    ADDW(c->d[0], 1);                                   /* f1a  addq.w #1,D0w */
    if ((int16_t)W(c->d[0]) > 0x3E7                     /* f1c  cmpi.w #$3e7,D0w / f20 ble.b $f2e */
        && rd16(0xFFE838) != 3)                         /* f22  cmpi.w #3,($ffe838).l / f2a beq.b $f2e */
        c->d[0] = 8;                                    /* f2c  moveq #8,D0 */
    wr16(0xFFE760, W(c->d[0]));                         /* f2e  move.w D0w,($ffe760).l */
    if (W(c->d[0]) == 8) {                              /* f34  cmpi.w #8,D0w / f38 bne.b $f72 */
        c->d[1] = 3;                                    /* f3a  moveq #3,D1 */
        JSR(c, 0x000F42, 0x0041B8);                     /* f3c  jsr $41b8 */
        ADDW(c->d[0], 4);                               /* f42  addq.w #4,D0w */
        wr16(0xFFE768, W(c->d[0]));                     /* f44  move.w D0w,($ffe768).l */
        wr16(0xFFE766, 0x76);                           /* f4a  move.w #$76,($ffe766).l */
        wr16(0xFFE758, rd16(0xFFE762));                 /* f52  move.w ($ffe762).l,($ffe758).l */
        if (rd16(0xFFE770) == 3) {                      /* f5c  cmpi.w #3,($ffe770).l / f64 bne.b $f70 */
            wr8(0xFFE78E, 0xFF);                        /* f66  st ($ffe78e).l */
            JSR(c, 0x000F70, 0x000890);                 /* f6c  bsr.w $890 */
        }
        goto LFC4;                                      /* f70  bra.b $fc4 */
    }
    SETW(c->d[0], rd16(0xFFE758));                      /* f72  move.w ($ffe758).l,D0w */
    ADDW(c->d[0], 1);                                   /* f78  addq.w #1,D0w */
    if ((int16_t)rd16(0xFFE760) > 8)                    /* f7a  cmpi.w #8,($ffe760).l / f82 ble.b $f8a */
        ADDW(c->d[0], rd16(0xFFE75A));                  /* f84  add.w ($ffe75a).l,D0w */
    if ((int16_t)W(c->d[0]) > 0x72) {                   /* f8a  cmpi.w #$72,D0w / f8e ble.b $fbe */
        SUBW(c->d[0], 0x72);                            /* f90  subi.w #$72,D0w */
        ADDW(c->d[0], 7);                               /* f94  addq.w #7,D0w */
        if (W(c->d[0]) == 8) {                          /* f96  cmpi.w #8,D0w / f9a bne.b $fbe */
            c->d[1] = 8;                                /* f9c  moveq #8,D1 */
            JSR(c, 0x000FA2, 0x004086);                 /* f9e  bsr.w $4086 */
            if (W(c->d[0]) != 1)                        /* fa2  cmpi.w #1,D0w / fa6 beq.b $faa */
                ADDW(c->d[1], 1);                       /* fa8  addq.w #1,D1w */
            SETW(c->d[0], W(c->d[1]));                  /* faa  move.w D1w,D0w */
            SETW(c->d[1], rd16(0xFFE75A));              /* fac  move.w ($ffe75a).l,D1w */
            ADDW(c->d[1], 1);                           /* fb2  addq.w #1,D1w */
            SETW(c->d[1], W(c->d[1]) & 7);              /* fb4  andi.w #7,D1w */
            wr16(0xFFE75A, W(c->d[1]));                 /* fb8  move.w D1w,($ffe75a).l */
        }
    }
    wr16(0xFFE758, W(c->d[0]));                         /* fbe  move.w D0w,($ffe758).l */

LFC4:
    if ((int16_t)rd16(0xFFE760) < 8) goto L1170;        /* fc4  cmpi.w #8,($ffe760).l / fcc blt.w $1170 */
    {   uint16_t old = rd16(0xFFE770);                  /* fd0  subq.w #1,($ffe770).l */
        wr16(0xFFE770, g4_subw(c, old, 1));
        if (old == 0) {                                 /* fd6  bcc.b $fe6  (carry = borrow) */
            wr8(0xFFE78E, 0xFF);                        /* fd8  st ($ffe78e).l */
            wr16(0xFFE770, 2);                          /* fde  move.w #2,($ffe770).l */
        }
    }
    SETW(c->d[0], rd16(0xFFE768));                      /* fe6  move.w ($ffe768).l,D0w */
    SUBW(c->d[0], 1);                                   /* fec  subq.w #1,D0w */
    wr16(0xFFE768, W(c->d[0]));                         /* fee  move.w D0w,($ffe768).l */
    if ((int16_t)W(c->d[0]) > 0) goto L1170;            /* ff4  bgt.w $1170 */

    /* treasure room next */
    JSR(c, 0x000FFE, 0x01F694);                         /* ff8  jsr $1f694 */
    c->d[1] = 3;                                        /* ffe  moveq #3,D1 */
    JSR(c, 0x001006, 0x0041B8);                         /* 1000 jsr $41b8 */
    ADDW(c->d[0], 3);                                   /* 1006 addq.w #3,D0w */
    wr16(0xFFE768, W(c->d[0]));                         /* 1008 move.w D0w,($ffe768).l */
    wr8(0xFFE799, 0xFF);                                /* 100e st ($ffe799).l */
    wr8(0xFFE78D, 0xFF);                                /* 1014 st ($ffe78d).l */
    JSR(c, 0x00101E, 0x004086);                         /* 101a bsr.w $4086 */
    ADDW(c->d[0], W(c->d[0]));                          /* 101e add.w D0w,D0w */
    wr16(0xFFE76A, rd16(0x00105E + sext16(W(c->d[0])))); /* 1020 move.w ($105e,PC,D0w),($ffe76a).l */
    wr16(0xFFE76C, 0);                                  /* 1028 move.w #0,($ffe76c).l */
    c->d[1] = 0x0C;                                     /* 1030 moveq #$c,D1 */
    JSR(c, 0x001038, 0x0041B8);                         /* 1032 jsr $41b8 */
    wr16(0xFFE76E, W(c->d[0]));                         /* 1038 move.w D0w,($ffe76e).l */
    SETW(c->d[1], rd16(0xFF0B60));                      /* 103e move.w ($ff0b60).l,D1w */
    if ((int16_t)W(c->d[1]) < 0) goto L1186;            /* 1044 bmi.w $1186 */
    c->a[2] = 0xFF0B50;                                 /* 1048 lea ($ff0b50).l,A2 */
    do {
        c->a[0] = rd32(c->a[2]); c->a[2] += 4;          /* 104e movea.l (A2)+,A0 */
        wr16(c->a[0] + 0xD2, 0);                        /* 1050 move.w #0,($d2,A0)  treasures collected */
    } while (DBF(c->d[1]));                             /* 1056 dbf D1w,$104e */
    goto L1186;                                         /* 105a bra.w $1186 */

L1068:  /* leaving a treasure room: bonus screen */
    JSR(c, 0x00106E, 0x01F694);                         /* 1068 jsr $1f694 */
    wr8(0xFFE799, 0);                                   /* 106e clr.b ($ffe799).l */
    wr8(0xFFE78D, 0);                                   /* 1074 clr.b ($ffe78d).l */
    wr8(0xFFE79A, 0xFF);                                /* 107a st ($ffe79a).l */
    JSR(c, 0x001086, 0x01F84A);                         /* 1080 jsr $1f84a */
    JSR(c, 0x00108A, 0x004086);                         /* 1086 bsr.w $4086 */
    SETW(c->d[5], W(c->d[0]));                          /* 108a move.w D0w,D5w */
    c->a[0] = 0xFF0040;                                 /* 108c lea ($ff0040).l,A0 */
    c->d[6] = 5;                                        /* 1092 moveq #5,D6 */
    c->d[7] = 3;                                        /* 1094 moveq #3,D7 */
    do {
        if (rd8(c->a[0] + 0x61) != 0) goto L113A;       /* 1096 tst.b ($61,A0) / 109a bne.w $113a */
        SETW(c->d[1], rd16(c->a[0] + 0xD6));            /* 109e move.w ($d6,A0),D1w */
        SETW(c->d[0], rd16(c->a[0] + 0x5A));            /* 10a2 move.w ($5a,A0),D0w */
        SETB(c->d[0], B(c->d[0]) | rd8(c->a[0] + 0x60));/* 10a6 or.b ($60,A0),D0b */
        if (B(c->d[0]) == 0) {                          /* 10aa beq.b $1128 */
            c->d[2] = 2;                                /* 1128 moveq #2,D2 */
            SETW(c->d[3], W(c->d[6]));                  /* 112a move.w D6w,D3w */
            ADDW(c->d[3], 1);                           /* 112c addq.w #1,D3w */
            c->a[1] = 0x0255D1;                         /* 112e lea ($255d1).l,A1  "NO BONUS !!" */
            JSR(c, 0x00113A, 0x01D0F2);                 /* 1134 jsr $1d0f2  draw_text */
            goto L113A;
        }
        c->d[2] = 5;                                    /* 10ac moveq #5,D2 */
        SETW(c->d[3], W(c->d[6]));                      /* 10ae move.w D6w,D3w */
        ADDW(c->d[5], W(c->d[5]));                      /* 10b0 add.w D5w,D5w */
        ADDW(c->d[5], W(c->d[5]));                      /* 10b2 add.w D5w,D5w */
        c->a[1] = 0x0254CE;                             /* 10b4 lea ($254ce).l,A1 */
        c->a[1] = rd32(c->a[1] + sext16(W(c->d[5])));   /* 10ba movea.l (0,A1,D5w),A1  "100 * COINS"... */
        c->x = (W(c->d[5]) >> 1) & 1;                   /* 10be lsr.w #2,D5w */
        SETW(c->d[5], W(c->d[5]) >> 2);
        JSR(c, 0x0010C6, 0x01D0F2);                     /* 10c0 jsr $1d0f2  draw_text */
        c->d[2] = 0x14;                                 /* 10c6 moveq #$14,D2 */
        c->d[4] = 5;                                    /* 10c8 moveq #5,D4 */
        SETW(c->d[0], W(c->d[5]));                      /* 10ca move.w D5w,D0w */
        MULU(c->d[0], 0x64);                            /* 10cc mulu.w #$64,D0 */
        MULU(c->d[0], rd16(c->a[0] + 0xD0));            /* 10d0 mulu.w ($d0,A0),D0 */
        c->d[0] &= 0xFFFF;                              /* 10d4 andi.l #$ffff,D0 */
        push16(c, W(c->d[0]));                          /* 10da move.w D0w,-(SP) */
        JSR(c, 0x0010E2, 0x01CF70);                     /* 10dc jsr $1cf70  draw number */
        c->d[2] = 7;                                    /* 10e2 moveq #7,D2 */
        ADDW(c->d[3], 1);                               /* 10e4 addq.w #1,D3w */
        c->a[1] = 0x02560D;                             /* 10e6 lea ($2560d).l,A1  "TREASURES *" */
        JSR(c, 0x0010F2, 0x01D0F2);                     /* 10ec jsr $1d0f2 */
        c->d[2] = 0x17;                                 /* 10f2 moveq #$17,D2 */
        c->d[4] = 2;                                    /* 10f4 moveq #2,D4 */
        c->d[0] = 0;                                    /* 10f6 moveq #0,D0 */
        SETW(c->d[0], rd16(c->a[0] + 0xD2));            /* 10f8 move.w ($d2,A0),D0w */
        JSR(c, 0x001102, 0x01CF70);                     /* 10fc jsr $1cf70 */
        c->d[2] = 0x0B;                                 /* 1102 moveq #$b,D2 */
        ADDW(c->d[3], 1);                               /* 1104 addq.w #1,D3w */
        c->a[1] = 0x025619;                             /* 1106 lea ($25619).l,A1  "BONUS =" */
        JSR(c, 0x001112, 0x01D0F2);                     /* 110c jsr $1d0f2 */
        c->d[2] = 0x13;                                 /* 1112 moveq #$13,D2 */
        SETW(c->d[0], pop16(c));                        /* 1114 move.w (SP)+,D0w */
        MULU(c->d[0], rd16(c->a[0] + 0xD2));            /* 1116 mulu.w ($d2,A0),D0 */
        c->d[4] = 6;                                    /* 111a moveq #6,D4 */
        JSR(c, 0x001122, 0x01CF70);                     /* 111c jsr $1cf70 */
        wr32(c->a[0] + 0xC6, g4_addl(c, rd32(c->a[0] + 0xC6), c->d[0]));  /* 1122 add.l D0,($c6,A0)  score */
                                                        /* 1126 bra.b $113a */
L113A:
        ADDW(c->d[6], 5);                               /* 113a addq.w #5,D6w */
        c->a[0] += 0x2C4;                               /* 113c lea ($2c4,A0),A0 */
    } while (DBF(c->d[7]));                             /* 1140 dbf D7w,$1096 */
    push16(c, 0xF0);                                    /* 1144 move.w #$f0,-(SP) */
    JSR(c, 0x00114E, 0x004150);                         /* 1148 jsr $4150  delay_frames */
    c->a[7] += 2;                                       /* 114e addq.l #2,SP */
    SETW(c->d[0], rd16(0xFFE766));                      /* 1150 move.w ($ffe766).l,D0w */
    ADDW(c->d[0], 1);                                   /* 1156 addq.w #1,D0w */
    if ((int16_t)W(c->d[0]) > 0x80)                     /* 1158 cmpi.w #$80,D0w / 115c ble.b $1162 */
        SETW(c->d[0], 0x76);                            /* 115e move.w #$76,D0w */
    wr16(0xFFE766, W(c->d[0]));                         /* 1162 move.w D0w,($ffe766).l */
    JSR(c, 0x00116E, 0x01CE4A);                         /* 1168 jsr $1ce4a */
    goto L1186;                                         /* 116e bra.b $1186 */

L1170:
    SETW(c->d[0], rd16(0xFFE77E));                      /* 1170 move.w ($ffe77e).l,D0w */
    if (rd8(0xFFE790) == 0)                             /* 1176 tst.b ($ffe790).l / 117c bne.b $1180 */
        JSR(c, 0x001180, 0x00118C);                     /* 117e bsr.b $118c  arcade_speed */
    wr16(0xFFE780, W(c->d[0]));                         /* 1180 move.w D0w,($ffe780).l */
L1186:
    movem_pop(c, regs);                                 /* 1186 movem.l (SP)+,{D0-D7 A0 A1} */
}                                                       /* 118a rts */

/* ------------------------------------------------------------------ */
/* 0x00118C  arcade_speed: D0.w = word table 0x11EC[min($ffe782,$7f)  */
/*   + $ffe77c*128]; counts $ffe782 up outside treasure rooms          */
/* ------------------------------------------------------------------ */
void g4_arcade_speed(M68K *c)
{
    const uint32_t regs = MV_D(2) | MV_D(3);
    movem_push(c, regs);                                /* 118c movem.l {D2 D3},-(SP) */
    SETW(c->d[1], rd16(0xFFE782));                      /* 1190 move.w ($ffe782).l,D1w */
    if (rd8(0xFFE82C) != 0                              /* 1196 tst.b ($ffe82c).l / 119c beq.b $11b8 */
        && (int16_t)W(c->d[1]) >= 0x96) {               /* 119e cmpi.w #$96,D1w / 11a2 blt.b $11b8 */
        SETW(c->d[2], W(c->d[1]));                      /* 11a4 move.w D1w,D2w */
        SUBW(c->d[2], 0x80);                            /* 11a6 subi.w #$80,D2w */
        divu_w(&c->d[2], 0x20);                         /* 11aa divu.w #$20,D2 */
        c->d[3] = 6;                                    /* 11ae moveq #6,D3 */
        SETW(c->d[3], g4_subw(c, W(c->d[3]), W(c->d[2])));  /* 11b0 sub.w D2w,D3w */
        wr8(0xFFF014, B(c->d[3]));                      /* 11b2 move.b D3b,($fff014).l */
    }
    if (rd8(0xFFE799) == 0)                             /* 11b8 tst.b ($ffe799).l / 11be bne.b $11c6 */
        MEM_ADDW(0xFFE782, 1);                          /* 11c0 addq.w #1,($ffe782).l */
    if ((int16_t)W(c->d[1]) >= 0x7F)                    /* 11c6 cmpi.w #$7f,D1w / 11ca blt.b $11d0 */
        SETW(c->d[1], 0x7F);                            /* 11cc move.w #$7f,D1w */
    ADDW(c->d[1], W(c->d[1]));                          /* 11d0 add.w D1w,D1w */
    SETW(c->d[2], rd16(0xFFE77C));                      /* 11d2 move.w ($ffe77c).l,D2w */
    c->x = (W(c->d[2]) >> 8) & 1;                       /* 11d8 lsl.w #8,D2w */
    SETW(c->d[2], W(c->d[2]) << 8);
    ADDW(c->d[1], W(c->d[2]));                          /* 11da add.w D2w,D1w */
    c->a[1] = 0x0011EC;                                 /* 11dc lea ($11ec).l,A1 */
    SETW(c->d[0], rd16(c->a[1] + sext16(W(c->d[1]))));  /* 11e2 move.w (0,A1,D1w),D0w */
    movem_pop(c, regs);                                 /* 11e6 movem.l (SP)+,{D2 D3} */
}                                                       /* 11ea rts */

/* ------------------------------------------------------------------ */
/* 0x0019EC  quest_next: table 0x1A06 by $ffe842 (0 stage, 1 hub)      */
/* ------------------------------------------------------------------ */
void g4_quest_next(M68K *c)
{
    movem_push(c, 0x3FFF);                              /* 19ec movem.l {D0-D7 A0-A5},-(SP) */
    SETW(c->d[0], rd16(0xFFE842));                      /* 19f0 move.w ($ffe842).l,D0w */
    ADDW(c->d[0], W(c->d[0]));                          /* 19f6 add.w D0w,D0w */
    ADDW(c->d[0], W(c->d[0]));                          /* 19f8 add.w D0w,D0w */
    c->a[0] = rd32(0x001A06 + sext16(W(c->d[0])));      /* 19fa movea.l ($1a06,PC,D0w),A0 */
    JSR(c, 0x001A00, c->a[0]);                          /* 19fe jsr (A0)  0x1B2E stage / 0x1A0E hub */
    movem_pop(c, 0x3FFF);                               /* 1a00 movem.l (SP)+,{D0-D7 A0-A5} */
}                                                       /* 1a04 rts */

/* ------------------------------------------------------------------ */
/* 0x001A0E  quest_from_hub                                            */
/* ------------------------------------------------------------------ */
void g4_quest_from_hub(M68K *c)
{
    c->d[4] = sext16(rd16(0xFFE84C));                   /* 1a0e movem.w ($ffe84c).l,{D4 D5}  exit cell x, y */
    c->d[5] = sext16(rd16(0xFFE84E));
    if (W(c->d[5]) == 0x1F) goto L1AA8;                 /* 1a16 cmpi.w #$1f,D5w / 1a1a beq.w $1aa8 */
    c->x = (W(c->d[4]) >> 3) & 1;                       /* 1a1e lsr.w #4,D4w */
    SETW(c->d[4], W(c->d[4]) >> 4);
    c->x = (W(c->d[5]) >> 2) & 1;                       /* 1a20 lsr.w #3,D5w */
    SETW(c->d[5], W(c->d[5]) >> 3);
    ADDW(c->d[5], W(c->d[5]));                          /* 1a22 add.w D5w,D5w */
    ADDW(c->d[4], W(c->d[5]));                          /* 1a24 add.w D5w,D4w */
    ADDW(c->d[4], W(c->d[4]));                          /* 1a26 add.w D4w,D4w */
    c->a[1] = 0x001B0A;                                 /* 1a28 lea ($1b0a).l,A1  doorway -> area */
    SETW(c->d[0], rd16(c->a[1] + sext16(W(c->d[4])))); /* 1a2e move.w (0,A1,D4w),D0w */
    wr16(0xFFE844, W(c->d[0]));                         /* 1a32 move.w D0w,($ffe844).l */
    ADDW(c->d[0], W(c->d[0]));                          /* 1a38 add.w D0w,D0w */
    c->a[1] = 0x001B1A;                                 /* 1a3a lea ($1b1a).l,A1  area -> first stage */
    wr16(0xFFE846, rd16(c->a[1] + sext16(W(c->d[0])))); /* 1a40 move.w (0,A1,D0w),($ffe846).l */
    wr16(0xFFE85C, 0);                                  /* 1a48 move.w #0,($ffe85c).l */
    wr16(0xFFE85E, 0);                                  /* 1a50 move.w #0,($ffe85e).l */
    JSR(c, 0x001A5E, 0x01310A);                         /* 1a58 jsr $1310a  level = $8f + area*10 + stage-1 */
    wr16(0xFFE842, 0);                                  /* 1a5e move.w #0,($ffe842).l */
    wr8(0xFFE874, 0);                                   /* 1a66 clr.b ($ffe874).l */
    wr8(0xFFE875, 0);                                   /* 1a6c clr.b ($ffe875).l */
    wr8(0xFFE876, 0);                                   /* 1a72 clr.b ($ffe876).l */
    wr8(0xFFE86D, 0);                                   /* 1a78 clr.b ($ffe86d).l */
    wr8(0xFFE872, 0);                                   /* 1a7e clr.b ($ffe872).l */
    JSR(c, 0x001A8A, 0x011E96);                         /* 1a84 jsr $11e96 */
    push16(c, 0x64);                                    /* 1a8a move.w #$64,-(SP) */
    JSR(c, 0x001A94, 0x004150);                         /* 1a8e jsr $4150  delay_frames */
    c->a[7] += 2;                                       /* 1a94 addq.l #2,SP */
    SETW(c->d[1], 6);                                   /* 1a96 move.w #6,D1w */
    ADDW(c->d[1], rd16(0xFFE844));                      /* 1a9a add.w ($ffe844).l,D1w */
    JSR(c, 0x001AA6, 0x01F664);                         /* 1aa0 jsr $1f664 */
    return;                                             /* 1aa6 bra.b $1b08 */

L1AA8:  /* the hub's exit row: end of the quest */
    push16(c, 0x78);                                    /* 1aa8 move.w #$78,-(SP) */
    JSR(c, 0x001AB2, 0x004150);                         /* 1aac jsr $4150 */
    c->a[7] += 2;                                       /* 1ab2 addq.l #2,SP */
    wr8(0xFFE792, 0xFF);                                /* 1ab4 st ($ffe792).l  game over */
    wr8(0xFFE871, 0xFF);                                /* 1aba st ($ffe871).l */
    c->a[0] = 0xFF0040;                                 /* 1ac0 lea ($ff0040).l,A0 */
    c->d[1] = 3;                                        /* 1ac6 moveq #3,D1 */
    do {
        if (rd8(c->a[0] + 0x61) == 0) {                 /* 1ac8 tst.b ($61,A0) / 1acc bne.b $1ad8 */
            JSR(c, 0x001AD4, 0x01292E);                 /* 1ace jsr $1292e  pw_generate */
            wr8(c->a[0] + 0x61, 0xFF);                  /* 1ad4 st ($61,A0) */
        }
        c->a[0] += 0x2C4;                               /* 1ad8 lea ($2c4,A0),A0 */
    } while (DBF(c->d[1]));                             /* 1adc dbf D1w,$1ac8 */
    if (rd8(0xFFE866) == 0) {                           /* 1ae0 tst.b ($ffe866).l / 1ae6 bne.b $1aee */
        JSR(c, 0x001AEC, 0x002E8E);                     /* 1ae8 bsr.w $2e8e  pw_screen */
        return;                                         /* 1aec bra.b $1b08 */
    }
    wr8(0xFFE871, 0);                                   /* 1aee clr.b ($ffe871).l */
    JSR(c, 0x001AFA, 0x0164E6);                         /* 1af4 jsr $164e6 */
    JSR(c, 0x001B00, 0x0040AA);                         /* 1afa jsr $40aa */
    wr16(0xFFE838, 0);                                  /* 1b00 move.w #0,($ffe838).l */
}                                                       /* 1b08 rts */

/* ------------------------------------------------------------------ */
/* 0x001B2E  quest_from_stage                                          */
/* ------------------------------------------------------------------ */
void g4_quest_from_stage(M68K *c)
{
    JSR(c, 0x001B32, 0x001C6E);                         /* 1b2e bsr.w $1c6e  quest_tally */
    JSR(c, 0x001B38, 0x01295A);                         /* 1b32 jsr $1295a  shortcode_generate */
    wr8(0xFFE874, 0);                                   /* 1b38 clr.b ($ffe874).l */
    wr8(0xFFE875, 0);                                   /* 1b3e clr.b ($ffe875).l */
    wr8(0xFFE876, 0);                                   /* 1b44 clr.b ($ffe876).l */
    SETW(c->d[0], rd16(0xFFE85C));                      /* 1b4a move.w ($ffe85c).l,D0w */
    if (W(c->d[0]) != 0) goto L1BD6;                    /* 1b50 bne.w $1bd6 */

    /* no stairs: back to the hub */
    wr16(0xFFE758, 0x8E);                               /* 1b54 move.w #$8e,($ffe758).l */
    wr16(0xFFE842, 1);                                  /* 1b5c move.w #1,($ffe842).l */
    SETW(c->d[1], rd16(0xFFE844));                      /* 1b64 move.w ($ffe844).l,D1w */
    ADDW(c->d[1], W(c->d[1]));                          /* 1b6a add.w D1w,D1w */
    ADDW(c->d[1], W(c->d[1]));                          /* 1b6c add.w D1w,D1w */
    c->a[1] = 0x001C52;                                 /* 1b6e lea ($1c52).l,A1  hub return position per area */
    c->d[4] = sext16(rd16(c->a[1] + sext16(W(c->d[1]))));      /* 1b74 movem.w (0,A1,D1w),{D4 D5} */
    c->d[5] = sext16(rd16(c->a[1] + sext16(W(c->d[1])) + 2));
    wr16(0xFFE848, W(c->d[4]));                         /* 1b7a movem.w {D4 D5},($ffe848).l */
    wr16(0xFFE84A, W(c->d[5]));
    JSR(c, 0x001B88, 0x01F694);                         /* 1b82 jsr $1f694 */
    push32(c, 0x02500C);                                /* 1b88 pea ($2500c).l */
    JSR(c, 0x001B94, 0x01CCFA);                         /* 1b8e jsr $1ccfa */
    c->a[7] += 4;                                       /* 1b94 addq.l #4,SP */
    c->a[0] = 0xFF0040;                                 /* 1b96 lea ($ff0040).l,A0 */
    c->d[1] = 3;                                        /* 1b9c moveq #3,D1 */
    do {
        if (rd8(c->a[0] + 0x167) != 0) {                /* 1b9e tst.b ($167,A0) / 1ba2 beq.b $1bc0 */
            wr8(c->a[0] + 0x61, 0);                     /* 1ba4 clr.b ($61,A0) */
            wr8(c->a[0] + 0x167, 0);                    /* 1ba8 clr.b ($167,A0) */
            if (rd8(0xFFE866) != 0) {                   /* 1bac tst.b ($ffe866).l / 1bb2 beq.b $1bc0 */
                wr8(c->a[0] + 0x16D, 0);                /* 1bb4 move.b #0,($16d,A0) */
                wr16(c->a[0] + 0xD0, 1);                /* 1bba move.w #1,($d0,A0) */
            }
        }
        if (rd8(c->a[0] + 0x61) == 0)                   /* 1bc0 tst.b ($61,A0) / 1bc4 bne.b $1bcc */
            wr32(c->a[0] + 0xB6, rd32(c->a[0] + 0x134));/* 1bc6 move.l ($134,A0),($b6,A0)  health = max */
        c->a[0] += 0x2C4;                               /* 1bcc lea ($2c4,A0),A0 */
    } while (DBF(c->d[1]));                             /* 1bd0 dbf D1w,$1b9e */
    goto L1C4A;                                         /* 1bd4 bra.b $1c4a */

L1BD6:  /* stairs: next stage */
    SETW(c->d[2], rd16(0xFFE846));                      /* 1bd6 move.w ($ffe846).l,D2w */
    MEM_ADDW(0xFFE846, W(c->d[0]));                     /* 1bdc add.w D0w,($ffe846).l */
    SETW(c->d[1], rd16(0xFFE844));                      /* 1be2 move.w ($ffe844).l,D1w */
    ADDW(c->d[1], W(c->d[1]));                          /* 1be8 add.w D1w,D1w */
    c->a[0] = 0x001B24;                                 /* 1bea lea ($1b24).l,A0  top stage per area */
    SETW(c->d[0], rd16(c->a[0] + sext16(W(c->d[1])))); /* 1bf0 move.w (0,A0,D1w),D0w */
    if (W(c->d[0]) == rd16(0xFFE846)) {                 /* 1bf4 cmp.w ($ffe846).l,D0w / 1bfa bne.b $1c34 */
        if (rd8(0xFFE866) != 0)                         /* 1bfc tst.b ($ffe866).l / 1c02 beq.b $1c0e */
            wr32(0xFFE84C, 0x00100012);                 /* 1c04 move.l #$100012,($ffe84c).l */
        wr8(0xFFE875, 0xFF);                            /* 1c0e st ($ffe875).l */
        wr8(0xFFE8E0, 0);                               /* 1c14 clr.b ($ffe8e0).l */
        JSR(c, 0x001C20, 0x01F694);                     /* 1c1a jsr $1f694 */
        SETW(c->d[1], rd16(0xFFE844));                  /* 1c20 move.w ($ffe844).l,D1w */
        if (!(rd8(0xFFE869) & (1u << (c->d[1] & 7))))   /* 1c26 btst.l D1,($ffe869).l / 1c2c bne.b $1c34 */
            wr8(0xFFE874, 0xFF);                        /* 1c2e st ($ffe874).l */
    }
    wr8(0xFFE870, W(c->d[0]) == W(c->d[2]) ? 0xFF : 0); /* 1c34 cmp.w D2w,D0w / 1c36 seq ($ffe870).l */
    if (W(c->d[0]) == W(c->d[2]))                       /* 1c3c bne.b $1c44 */
        JSR(c, 0x001C44, 0x01F694);                     /* 1c3e jsr $1f694 */
    JSR(c, 0x001C4A, 0x01310A);                         /* 1c44 jsr $1310a  level number */
L1C4A:
    JSR(c, 0x001C50, 0x01CE4A);                         /* 1c4a jsr $1ce4a */
}                                                       /* 1c50 rts */

/* ------------------------------------------------------------------ */
/* 0x001C6E  quest_tally: share and multiply each player's stage       */
/*   E.P. (+$12E) and gold (+$12C), add to the totals, show the screen */
/* ------------------------------------------------------------------ */
static void tally_line(M68K *c, uint32_t label, uint32_t stage_field, uint32_t total_field, int col6_first,
                       uint32_t r1, uint32_t r2, uint32_t r3, uint32_t r4, uint32_t r5,
                       uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r9, uint32_t r10)
{
    /* One "label  +N*M  +stage = total" line; r1..r10 are the real return addresses.
     * The two copies in the ROM differ only in where "moveq #6,D2" sits: before the
     * tst.w D6 in the GOLD line (col6_first), inside the branch in the E.P. line. */
    c->d[2] = 2;                                        /*      moveq #2,D2 */
    c->a[1] = label;                                    /*      lea (label).l,A1 */
    JSR(c, r1, 0x01D0F2);                               /*      jsr $1d0f2  draw_text */
    if (col6_first) c->d[2] = 6;                        /* 1e28 moveq #6,D2  (GOLD line) */
    if (W(c->d[6]) != 0) {                              /*      tst.w D6w / beq.b */
        if (!col6_first) c->d[2] = 6;                   /* 1da6 moveq #6,D2  (E.P. line) */
        c->d[0] = 0x2B;                                 /*      moveq #$2b,D0  '+' */
        JSR(c, r2, 0x01D054);                           /*      jsr $1d054  draw_char */
        ADDW(c->d[2], 1);                               /*      addq.w #1,D2w */
        if ((int16_t)W(c->d[6]) > 4) {                  /*      cmpi.w #4,D6w / ble.b */
            c->d[4] = 5;                                /*      moveq #5,D4 */
            SETW(c->d[0], W(c->d[5]));                  /*      move.w D5w,D0w */
            MULU(c->d[0], W(c->d[6]));                  /*      mulu.w D6w,D0 */
            JSR(c, r3, 0x01CF70);                       /*      jsr $1cf70  draw number */
        } else {
            c->d[4] = 3;                                /*      moveq #3,D4 */
            c->d[0] = 0;                                /*      moveq #0,D0 */
            SETW(c->d[0], W(c->d[5]));                  /*      move.w D5w,D0w */
            JSR(c, r4, 0x01CF70);                       /*      jsr $1cf70 */
            ADDW(c->d[2], 3);                           /*      addq.w #3,D2w */
            c->d[0] = 0x2A;                             /*      moveq #$2a,D0  '*' */
            JSR(c, r5, 0x01D054);                       /*      jsr $1d054 */
            ADDW(c->d[2], 1);                           /*      addq.w #1,D2w */
            SETW(c->d[0], W(c->d[6]));                  /*      move.w D6w,D0w */
            c->d[4] = 1;                                /*      moveq #1,D4 */
            JSR(c, r6, 0x01CF70);                       /*      jsr $1cf70 */
        }
    }
    c->d[2] = 0x0D;                                     /*      moveq #$d,D2 */
    c->d[0] = 0x2B;                                     /*      moveq #$2b,D0  '+' */
    JSR(c, r7, 0x01D054);                               /*      jsr $1d054 */
    ADDW(c->d[2], 1);                                   /*      addq.w #1,D2w */
    SETW(c->d[0], rd16(c->a[0] + stage_field));        /*      move.w (stage,A0),D0w */
    c->d[4] = 4;                                        /*      moveq #4,D4 */
    JSR(c, r8, 0x01CF70);                               /*      jsr $1cf70 */
    ADDW(c->d[2], 6);                                   /*      addq.w #6,D2w */
    c->d[0] = 0x3D;                                     /*      moveq #$3d,D0  '=' */
    JSR(c, r9, 0x01D054);                               /*      jsr $1d054 */
    ADDW(c->d[2], 2);                                   /*      addq.w #2,D2w */
    c->d[0] = rd32(c->a[0] + total_field);              /*      move.l (total,A0),D0 */
    c->d[4] = 5;                                        /*      moveq #5,D4 */
    JSR(c, r10, 0x01CF70);                              /*      jsr $1cf70 */
}

void g4_quest_tally(M68K *c)
{
    if (rd8(0xFFE86F) != 0) goto L1EBA;                 /* 1c6e tst.b ($ffe86f).l / 1c74 bne.w $1eba */
    if (rd8(0xFFE866) != 0) goto L1EBA;                 /* 1c78 tst.b ($ffe866).l / 1c7e bne.w $1eba */

    /* each player gives half of this stage's E.P. and gold to every player of another class */
    c->a[0] = 0xFF0040;                                 /* 1c82 lea ($ff0040).l,A0 */
    c->d[7] = 3;                                        /* 1c88 moveq #3,D7 */
    do {
        if (rd8(c->a[0] + 0x61) == 0 && rd8(c->a[0] + 0x167) == 0) {   /* 1c8a-1c94 tst.b $61/$167(A0) ; bne $1cd0 */
            SETW(c->d[2], rd16(c->a[0] + 0x12E));       /* 1c96 move.w ($12e,A0),D2w */
            SETW(c->d[3], rd16(c->a[0] + 0x12C));       /* 1c9a move.w ($12c,A0),D3w */
            c->x = W(c->d[2]) & 1; SETW(c->d[2], W(c->d[2]) >> 1);   /* 1c9e lsr.w #1,D2w */
            c->x = W(c->d[3]) & 1; SETW(c->d[3], W(c->d[3]) >> 1);   /* 1ca0 lsr.w #1,D3w */
            c->a[1] = 0xFF0040;                         /* 1ca2 lea ($ff0040).l,A1 */
            c->d[6] = 3;                                /* 1ca8 moveq #3,D6 */
            do {
                if (rd8(c->a[1] + 0x61) == 0 && rd8(c->a[1] + 0x167) == 0) {   /* 1caa-1cb4 */
                    SETW(c->d[1], rd16(c->a[0] + 0xB4));            /* 1cb6 move.w ($b4,A0),D1w */
                    if (W(c->d[1]) != rd16(c->a[1] + 0xB4)) {       /* 1cba cmp.w ($b4,A1),D1w / 1cbe beq.b $1cc8 */
                        MEM_ADDW(c->a[1] + 0x132, W(c->d[2]));      /* 1cc0 add.w D2w,($132,A1) */
                        MEM_ADDW(c->a[1] + 0x130, W(c->d[3]));      /* 1cc4 add.w D3w,($130,A1) */
                    }
                }
                c->a[1] += 0x2C4;                       /* 1cc8 lea ($2c4,A1),A1 */
            } while (DBF(c->d[6]));                     /* 1ccc dbf D6w,$1caa */
        }
        c->a[0] += 0x2C4;                               /* 1cd0 lea ($2c4,A0),A0 */
    } while (DBF(c->d[7]));                             /* 1cd4 dbf D7w,$1c8a */

    /* multiplier by player count, bonus per bonus count */
    c->a[1] = 0x001EC2;                                 /* 1cd8 lea ($1ec2).l,A1  1 2 3 4 5 */
    c->a[2] = 0x001ECC;                                 /* 1cde lea ($1ecc).l,A2  50 125 225 350 500 */
    SETW(c->d[1], rd16(0xFFE860));                      /* 1ce4 move.w ($ffe860).l,D1w */
    ADDW(c->d[1], W(c->d[1]));                          /* 1cea add.w D1w,D1w */
    SETW(c->d[5], rd16(c->a[1] + sext16(W(c->d[1])))); /* 1cec move.w (0,A1,D1w),D5w */
    SETW(c->d[6], rd16(c->a[2] + sext16(W(c->d[1])))); /* 1cf0 move.w (0,A2,D1w),D6w */
    SETW(c->d[0], rd16(0xFFE85E));                      /* 1cf4 move.w ($ffe85e).l,D0w */
    MULU(c->d[6], W(c->d[0]));                          /* 1cfa mulu.w D0w,D6 */
    c->a[0] = 0xFF0040;                                 /* 1cfc lea ($ff0040).l,A0 */
    c->d[7] = 3;                                        /* 1d02 moveq #3,D7 */
    do {
        if (rd8(c->a[0] + 0x61) == 0 && rd8(c->a[0] + 0x167) == 0) {   /* 1d04-1d0e */
            SETW(c->d[0], rd16(c->a[0] + 0x12E));       /* 1d10 move.w ($12e,A0),D0w */
            ADDW(c->d[0], rd16(c->a[0] + 0x132));       /* 1d14 add.w ($132,A0),D0w */
            MULU(c->d[0], W(c->d[5]));                  /* 1d18 mulu.w D5w,D0 */
            wr16(c->a[0] + 0x12E, W(c->d[0]));          /* 1d1a move.w D0w,($12e,A0) */
            c->d[0] = g4_addl(c, c->d[0], c->d[6]);     /* 1d1e add.l D6,D0 */
            c->d[0] = g4_addl(c, c->d[0], rd32(c->a[0] + 0x128));  /* 1d20 add.l ($128,A0),D0 */
            if ((int32_t)c->d[0] >= 0x10000)            /* 1d24 cmpi.l #$10000,D0 / 1d2a blt.b $1d32 */
                c->d[0] = 0xFFFF;                       /* 1d2c move.l #$ffff,D0 */
            wr32(c->a[0] + 0x128, c->d[0]);             /* 1d32 move.l D0,($128,A0)  E.P. total */
            SETW(c->d[0], rd16(c->a[0] + 0x12C));       /* 1d36 move.w ($12c,A0),D0w */
            ADDW(c->d[0], rd16(c->a[0] + 0x130));       /* 1d3a add.w ($130,A0),D0w */
            MULU(c->d[0], W(c->d[5]));                  /* 1d3e mulu.w D5w,D0 */
            wr16(c->a[0] + 0x12C, W(c->d[0]));          /* 1d40 move.w D0w,($12c,A0) */
            c->d[0] = g4_addl(c, c->d[0], c->d[6]);     /* 1d44 add.l D6,D0 */
            c->d[0] = g4_addl(c, c->d[0], rd32(c->a[0] + 0x124));  /* 1d46 add.l ($124,A0),D0 */
            if ((int32_t)c->d[0] >= 0x10000)            /* 1d4a cmpi.l #$10000,D0 / 1d50 blt.b $1d58 */
                c->d[0] = 0xFFFF;                       /* 1d52 move.l #$ffff,D0 */
            wr32(c->a[0] + 0x124, c->d[0]);             /* 1d58 move.l D0,($124,A0)  gold total */
        }
        c->a[0] += 0x2C4;                               /* 1d5c lea ($2c4,A0),A0 */
    } while (DBF(c->d[7]));                             /* 1d60 dbf D7w,$1d04 */

    /* the screen: two lines per player */
    SETW(c->d[5], rd16(c->a[2] + sext16(W(c->d[1])))); /* 1d64 move.w (0,A2,D1w),D5w */
    SETW(c->d[6], rd16(0xFFE85E));                      /* 1d68 move.w ($ffe85e).l,D6w */
    c->a[0] = 0xFF0040;                                 /* 1d6e lea ($ff0040).l,A0 */
    c->d[7] = 3;                                        /* 1d74 moveq #3,D7 */
    do {
        if (rd8(c->a[0] + 0x61) == 0 && rd8(c->a[0] + 0x167) == 0) {   /* 1d76-1d82 ; bne.w $1e9e */
            SETW(c->d[1], rd16(c->a[0] + 0xD6));        /* 1d86 move.w ($d6,A0),D1w */
            SETW(c->d[3], rd16(c->a[0] + 0xB4));        /* 1d8a move.w ($b4,A0),D3w */
            MULU(c->d[3], 5);                           /* 1d8e mulu.w #5,D3 */
            ADDW(c->d[3], 6);                           /* 1d92 addq.w #6,D3w */
            /* 1d94-1e18: "E.P." line */
            tally_line(c, 0x0259C9, 0x12E, 0x128, 0,
                       0x001DA2, 0x001DB0, 0x001DC4, 0x001DD2, 0x001DDC, 0x001DE8,
                       0x001DF2, 0x001E00, 0x001E0A, 0x001E18);
            ADDW(c->d[3], 1);                           /* 1e18 addq.w #1,D3w */
            /* 1e1a-1e9e: "GOLD" line */
            tally_line(c, 0x0259CE, 0x12C, 0x124, 1,
                       0x001E28, 0x001E36, 0x001E4A, 0x001E58, 0x001E62, 0x001E6E,
                       0x001E78, 0x001E86, 0x001E90, 0x001E9E);
        }
        c->a[0] += 0x2C4;                               /* 1e9e lea ($2c4,A0),A0 */
    } while (DBF(c->d[7]));                             /* 1ea2 dbf D7w,$1d76 */
    wr16(0xFFE85E, 0);                                  /* 1ea6 move.w #0,($ffe85e).l */
    push16(c, 0x12C);                                   /* 1eae move.w #$12c,-(SP) */
    JSR(c, 0x001EB8, 0x004150);                         /* 1eb2 jsr $4150  delay_frames (5 s) */
    c->a[7] += 2;                                       /* 1eb8 addq.l #2,SP */
L1EBA:
    wr8(0xFFE86F, 0);                                   /* 1eba clr.b ($ffe86f).l */
}                                                       /* 1ec0 rts */

/* ------------------------------------------------------------------ */
/* 0x001ED6  battle_next: next arena, 0x81-0x8D                        */
/* ------------------------------------------------------------------ */
void g4_battle_next(M68K *c)
{
    movem_push(c, MV_D(0) | MV_D(1));                   /* 1ed6 movem.l {D0 D1},-(SP) */
    SETW(c->d[0], rd16(0xFFE758));                      /* 1eda move.w ($ffe758).l,D0w */
    ADDW(c->d[0], 1);                                   /* 1ee0 addq.w #1,D0w */
    ADDW(c->d[0], rd16(0xFFE75A));                      /* 1ee2 add.w ($ffe75a).l,D0w */
    if ((int16_t)W(c->d[0]) > 0x8D) {                   /* 1ee8 cmpi.w #$8d,D0w / 1eec ble.b $1f0a */
        SUBW(c->d[0], 0x0D);                            /* 1eee subi.w #$d,D0w */
        if (W(c->d[0]) == 0x81) {                       /* 1ef2 cmpi.w #$81,D0w / 1ef6 bne.b $1f0a */
            SETW(c->d[1], rd16(0xFFE75A));              /* 1ef8 move.w ($ffe75a).l,D1w */
            ADDW(c->d[1], 1);                           /* 1efe addq.w #1,D1w */
            SETW(c->d[1], W(c->d[1]) & 3);              /* 1f00 andi.w #3,D1w */
            wr16(0xFFE75A, W(c->d[1]));                 /* 1f04 move.w D1w,($ffe75a).l */
        }
    }
    wr16(0xFFE758, W(c->d[0]));                         /* 1f0a move.w D0w,($ffe758).l */
    wr16(0xFFE962, W(c->d[0]));                         /* 1f10 move.w D0w,($ffe962).l */
    MEM_ADDW(0xFFE760, 1);                              /* 1f16 addq.w #1,($ffe760).l */
    movem_pop(c, MV_D(0) | MV_D(1));                    /* 1f1c movem.l (SP)+,{D0 D1} */
}                                                       /* 1f20 rts */

/* ------------------------------------------------------------------ */
/* 0x001F22  record_next                                               */
/* ------------------------------------------------------------------ */
void g4_record_next(M68K *c)
{
    JSR(c, 0x001F28, 0x0186D4);                         /* 1f22 jsr $186d4 */
    JSR(c, 0x001F2C, 0x000F06);                         /* 1f28 bsr.w $f06  arcade_next */
}                                                       /* 1f2c rts */
