/*
 * g4_main.c -- reset, main loop, level loop and new-game setup from
 * Gauntlet 4 (Mega Drive, 1993-09), translated from the 68000 instruction
 * by instruction (register model, see g4_m68k.h).
 *
 * Call graph
 *   g4_entry (0x400)          hardware init, then forever:
 *     title / attract          0x22504, 0x236A4 (until Start: Z=1)
 *     g4_game_setup (0xB5A)    new game; per-mode setup via table 0xC16
 *     loop:
 *       g4_play_level (0x8C2)  one level, frame by frame
 *       g4_level_end  (0xEDC)  per-mode handler via table 0xEF6
 *     until $ffe792 (game over); then high-score bookkeeping
 *
 * RAM variables used here (names provisional)
 *   $ffe838.w  game mode: 0 Arcade, 1 Quest, 2 Battle, 3 Record
 *   $ffe758.w  level number          $ffe75c.l  level data pointer (set by 0x40D8)
 *   $ffe792.b  game over             $ffe794.b  replay level (play_level restarts)
 *   $ffe7ea.w  intro countdown       $ffe76a.w  level timer (seconds)
 *   $ff0b50    4 player-struct pointers (active players)
 *   $ff0b60.w  active players - 1 (-1 = none left)
 *   $ff0040    4 player structs, 0x2C4 bytes each
 *   $ffeb5c.b  cold boot (set at reset when $a1000c reads 0)
 *   $ffe9bc    high-score tables: 4 x 10 x {3 initials, 0, score.l}
 *
 * Every JSR/BSR goes through JSR() (g4_dispatch.c), with its real return
 * address, so routines not yet translated run wherever the host sends them.
 * Each statement is annotated with the instruction it implements.
 */
#include "g4_m68k.h"

#define DBF(r)  (SETW((r), W(r) - 1), W(r) != 0xFFFFu)   /* dbf: decrement, true = loop again */

/* ------------------------------------------------------------------ */
/* 0x000400  entry (reset vector)                                      */
/*   Standard Mega Drive start-up (VDP registers, Z80 stub, RAM, CRAM, */
/*   VSRAM and PSG cleared), then the main loop. Never returns.        */
/* ------------------------------------------------------------------ */
void g4_entry(M68K *c)
{
    int i;
    uint32_t base;

    /* 400  tst.l ($a10008).l / 406 bne.b $40e / 408 tst.w ($a1000c).l / 40e bne.b $48c
     *      Either expansion/control port reads non-zero: soft reset, skip the init. */
    if (rd32(0xA10008) != 0) goto L48C;
    if (rd16(0xA1000C) != 0) goto L48C;

    c->a[5] = 0x00048E;                                 /* 410  lea ($7c,PC),A5 -> init table */

    /* 414  movem.w (A5)+,{D5 D6 D7}  (sign-extended into the whole register) */
    c->d[5] = sext16(rd16(c->a[5]));     c->a[5] += 2;  /* $8000: VDP register 0 command */
    c->d[6] = sext16(rd16(c->a[5]));     c->a[5] += 2;  /* $3fff: RAM clear count       */
    c->d[7] = sext16(rd16(c->a[5]));     c->a[5] += 2;  /* $0100: register step / Z80  */
    /* 418  movem.l (A5)+,{A0 A1 A2 A3 A4} */
    for (i = 0; i < 5; i++) { c->a[i] = rd32(c->a[5]); c->a[5] += 4; }
    /* A0 $a00000 Z80 RAM, A1 $a11100 Z80 busreq, A2 $a11200 Z80 reset,
       A3 $c00000 VDP data, A4 $c00004 VDP control */

    SETB(c->d[0], rd8(c->a[1] - 0x10FF));               /* 41c  move.b (-$10ff,A1),D0b  ($a10001 version) */
    SETB(c->d[0], B(c->d[0]) & 0x0F);                   /* 420  andi.b #$f,D0b */
    if (B(c->d[0]) != 0)                                /* 424  beq.b $42e */
        wr32(c->a[1] + 0x2F00, 0x53454741u);            /* 426  move.l #'SEGA',($2f00,A1)  TMSS */

    SETW(c->d[0], rd16(c->a[4]));                       /* 42e  move.w (A4),D0w  (VDP status) */
    c->d[0] = 0;                                        /* 430  moveq #0,D0 */
    c->a[6] = c->d[0];                                  /* 432  movea.l D0,A6 */
    c->usp  = c->a[6];                                  /* 434  move A6,USP */

    c->d[1] = 0x17;                                     /* 436  moveq #$17,D1 */
    do {                                                /*      24 VDP registers */
        SETB(c->d[5], rd8(c->a[5])); c->a[5] += 1;      /* 438  move.b (A5)+,D5b */
        wr16(c->a[4], W(c->d[5]));                      /* 43a  move.w D5w,(A4) */
        SETW(c->d[5], g4_addw(c, W(c->d[5]), W(c->d[7])));  /* 43c  add.w D7w,D5w */
    } while (DBF(c->d[1]));                             /* 43e  dbf D1w,$438 */

    wr32(c->a[4], rd32(c->a[5])); c->a[5] += 4;         /* 442  move.l (A5)+,(A4)  VRAM fill command */
    wr16(c->a[3], W(c->d[0]));                          /* 444  move.w D0w,(A3) */
    wr16(c->a[1], W(c->d[7]));                          /* 446  move.w D7w,(A1)  Z80 bus request */
    wr16(c->a[2], W(c->d[7]));                          /* 448  move.w D7w,(A2)  Z80 reset off */
    while (rd8(c->a[1]) & (1u << (c->d[0] & 7)))        /* 44a  btst.b D0,(A1) / 44c bne.b $44a */
        ;                                               /*      wait for the Z80 bus */

    c->d[2] = 0x25;                                     /* 44e  moveq #$25,D2 */
    do {                                                /*      38-byte Z80 stub program */
        wr8(c->a[0], rd8(c->a[5])); c->a[5] += 1; c->a[0] += 1;   /* 450  move.b (A5)+,(A0)+ */
    } while (DBF(c->d[2]));                             /* 452  dbf D2w,$450 */

    wr16(c->a[2], W(c->d[0]));                          /* 456  move.w D0w,(A2)  Z80 reset on */
    wr16(c->a[1], W(c->d[0]));                          /* 458  move.w D0w,(A1)  release bus */
    wr16(c->a[2], W(c->d[7]));                          /* 45a  move.w D7w,(A2)  Z80 reset off */

    do {                                                /*      clear 64 KB work RAM, top down */
        c->a[6] -= 4; wr32(c->a[6], c->d[0]);           /* 45c  move.l D0,-(A6)  (A6 wraps from 0) */
    } while (DBF(c->d[6]));                             /* 45e  dbf D6w,$45c */

    wr32(c->a[4], rd32(c->a[5])); c->a[5] += 4;         /* 462  move.l (A5)+,(A4) */
    wr32(c->a[4], rd32(c->a[5])); c->a[5] += 4;         /* 464  move.l (A5)+,(A4)  CRAM write */
    c->d[3] = 0x1F;                                     /* 466  moveq #$1f,D3 */
    do {
        wr32(c->a[3], c->d[0]);                         /* 468  move.l D0,(A3) */
    } while (DBF(c->d[3]));                             /* 46a  dbf D3w,$468 */

    wr32(c->a[4], rd32(c->a[5])); c->a[5] += 4;         /* 46e  move.l (A5)+,(A4)  VSRAM write */
    c->d[4] = 0x13;                                     /* 470  moveq #$13,D4 */
    do {
        wr32(c->a[3], c->d[0]);                         /* 472  move.l D0,(A3) */
    } while (DBF(c->d[4]));                             /* 474  dbf D4w,$472 */

    c->d[5] = 3;                                        /* 478  moveq #3,D5 */
    do {                                                /*      silence the 4 PSG channels */
        wr8(c->a[3] + 0x11, rd8(c->a[5])); c->a[5] += 1;    /* 47a  move.b (A5)+,($11,A3) */
    } while (DBF(c->d[5]));                             /* 47e  dbf D5w,$47a */

    wr16(c->a[2], W(c->d[0]));                          /* 482  move.w D0w,(A2)  Z80 reset on */

    /* 484  movem.l (A6),{D0-D7 A0-A6}  (A6 = $ffff0000: cleared RAM -> all zero) */
    base = c->a[6];
    for (i = 0; i < 15; i++) *mv_reg(c, i) = rd32(base + 4u * (uint32_t)i);

    g4_move_sr(c, 0x2700);                              /* 488  move #$2700,SR  (CCR cleared too) */

L48C:                                                   /* 48c  bra.b $4fa */
    (void)rd16(0xC00004);                               /* 4fa  tst.w ($c00004).l */
    wr8(0xFFEB5C, rd16(0xA1000C) == 0 ? 0xFF : 0x00);   /* 500  tst.w ($a1000c).l / 506 seq ($ffeb5c).l */
    JSR(c, 0x000512, 0x01DB14);                         /* 50c  jsr $1db14 */
    JSR(c, 0x000514, 0x000584);                         /* 512  bsr.b $584  game_init */

    for (;;) {
        do {                                            /*      title / attract until Start */
            wr8(0xFFE874, 0);                           /* 514  clr.b ($ffe874).l */
            JSR(c, 0x000520, 0x022504);                 /* 51a  jsr $22504 */
            JSR(c, 0x000526, 0x0236A4);                 /* 520  jsr $236a4 */
        } while (!c->z);                                /* 526  bne.b $514 */

        JSR(c, 0x00052C, 0x000B5A);                     /* 528  bsr.w $b5a  game_setup */
        wr8(0xFFE798, 0xFF);                            /* 52c  st ($ffe798).l */
        for (;;) {
            JSR(c, 0x000536, 0x0008C2);                 /* 532  bsr.w $8c2  play_level */
            if (rd8(0xFFE792) != 0) break;              /* 536  tst.b ($ffe792).l / 53c bne.b $54c */
            JSR(c, 0x000542, 0x000EDC);                 /* 53e  bsr.w $edc  level_end */
            if (rd8(0xFFE792) != 0) break;              /* 542  tst.b ($ffe792).l / 548 bne.b $54c */
        }                                               /* 54a  bra.b $532 */

        wr8(0xFFE798, 0);                               /* 54c  clr.b ($ffe798).l */
        wr8(0xFFE78E, 0);                               /* 552  clr.b ($ffe78e).l */
        if ((int16_t)rd16(0xFFE758) >= 8                /* 558  cmpi.w #8,($ffe758).l / 560 blt.b $57e */
            && rd16(0xFFE838) == 0                      /* 562  cmpi.w #0,($ffe838).l / 56a bne.b $57e */
            && rd8(0xFFE866) == 0)                      /* 56c  tst.b ($ffe866).l / 572 bne.b $57e */
            wr16(0xFFE762, rd16(0xFFE758));             /* 574  move.w ($ffe758).l,($ffe762).l */
    }                                                   /* 57e  bra.b $514 */
}

/* ------------------------------------------------------------------ */
/* 0x000584  game_init: power-on defaults                              */
/* ------------------------------------------------------------------ */
void g4_game_init(M68K *c)
{
    static const uint32_t clr_b[] = {
        0xFFEB5E, 0xFFE824, 0xFFE821, 0xFFE81F, 0xFFE793, 0xFFE820, 0xFFE81D, 0xFFE796,
        0xFFE798, 0xFFE795, 0xFFE81E, 0xFFE874, 0xFFE7BB, 0xFFE823, 0xFFEB5A };
    int i;

    movem_push(c, MV_D(0) | MV_D(1) | MV_A(0) | MV_A(1));   /* 584  movem.l {D0 D1 A0 A1},-(SP) */
    JSR(c, 0x00058E, 0x01D64A);                         /* 588  jsr $1d64a */
    c->a[0] = 0xFF0040;                                 /* 58e  lea ($ff0040).l,A0 */
    c->d[0] = 3;                                        /* 594  moveq #3,D0 */
    do {
        JSR(c, 0x00059C, 0x00829C);                     /* 596  jsr $829c  player_reset */
        c->a[0] += 0x2C4;                               /* 59c  lea ($2c4,A0),A0 */
    } while (DBF(c->d[0]));                             /* 5a0  dbf D0w,$596 */

    wr16(0xFFE838, 0);                                  /* 5a4  move.w #0,($ffe838).l   mode = Arcade */
    wr16(0xFFE836, 7);                                  /* 5ac  move.w #7,($ffe836).l */
    wr16(0xFFE73A, 2);                                  /* 5b4  move.w #2,($ffe73a).l */
    wr16(0xFFE9BA, 0);                                  /* 5bc  move.w #0,($ffe9ba).l */
    wr16(0xFFEB6A, 0);                                  /* 5c4  move.w #0,($ffeb6a).l */
    wr16(0xFFEB74, 0);                                  /* 5cc  move.w #0,($ffeb74).l */
    wr16(0xFFEB76, 0);                                  /* 5d4  move.w #0,($ffeb76).l */
    wr16(0xFF0B60, 0xFFFF);                             /* 5dc  move.w #-1,($ff0b60).l  no players */
    wr16(0xFFAF14, 0xFFFF);                             /* 5e4  move.w #-1,($ffaf14).l */
    for (i = 0; i < 15; i++) wr8(clr_b[i], 0);          /* 5ec-640  clr.b x15 */
    wr8(0xFFEB59, 0xFF);                                /* 646  st ($ffeb59).l */
    wr8(0xFFE79B, 0xFF);                                /* 64c  st ($ffe79b).l */
    wr8(0xFFE823, 0);                                   /* 652  clr.b ($ffe823).l */
    wr8(0xFFE82A, 0);                                   /* 658  clr.b ($ffe82a).l */
    wr8(0xFFE871, 0);                                   /* 65e  clr.b ($ffe871).l */
    wr8(0xFFE8E2, 0);                                   /* 664  clr.b ($ffe8e2).l */
    wr8(0xFFE8E3, 0);                                   /* 66a  clr.b ($ffe8e3).l */
    JSR(c, 0x000676, 0x0007EA);                         /* 670  jsr $7ea */

    if (rd8(0xFFEB5C) != 0) {                           /* 676  tst.b ($ffeb5c).l / 67c beq.b $69a */
        JSR(c, 0x000684, 0x01DD3C);                     /* 67e  jsr $1dd3c */
        JSR(c, 0x000686, 0x0006CA);                     /* 684  bsr.b $6ca */
        JSR(c, 0x00068A, 0x000EA2);                     /* 686  bsr.w $ea2  hiscore_init */
        wr16(0xFFE7E4, 0x34EB);                         /* 68a  move.w #$34eb,($ffe7e4).l */
        c->d[1] = 2;                                    /* 692  moveq #2,D1 */
        JSR(c, 0x00069A, 0x01F574);                     /* 694  jsr $1f574 */
    }

    push32(c, 0x0006BE);                                /* 69a  pea ($6be).l */
    JSR(c, 0x0006A6, 0x01CCFA);                         /* 6a0  jsr $1ccfa */
    c->a[7] += 4;                                       /* 6a6  addq.l #4,SP */
    c->a[0] = 0xC00000;                                 /* 6a8  lea ($c00000).l,A0 */
    wr16(c->a[0] + 4, 0x8164);                          /* 6ae  move.w #$8164,(4,A0)  VDP reg 1: display on, VBlank IRQ */
    g4_move_sr(c, 0x2300);                              /* 6b4  move #$2300,SR  interrupts on, CCR cleared */
    movem_pop(c, MV_D(0) | MV_D(1) | MV_A(0) | MV_A(1));    /* 6b8  movem.l (SP)+,{D0 D1 A0 A1} */
}                                                       /* 6bc  rts */

/* ------------------------------------------------------------------ */
/* 0x0008C2  play_level                                                */
/*   Sets the level up, then runs it one frame per iteration until the */
/*   players are gone, the timer runs out (Battle) or the game is      */
/*   quit. If $ffe794 is set afterwards the level is played again.     */
/* ------------------------------------------------------------------ */
void g4_play_level(M68K *c)
{
    movem_push(c, 0x7FFF);                              /* 8c2  movem.l {D0-D7 A0-A6},-(SP) */
L8C6:
    JSR(c, 0x0008CC, 0x01E99A);                         /* 8c6  jsr $1e99a */
    JSR(c, 0x0008D2, 0x01EA08);                         /* 8cc  jsr $1ea08 */
    JSR(c, 0x0008D8, 0x01E64A);                         /* 8d2  jsr $1e64a */
    JSR(c, 0x0008DE, 0x01E406);                         /* 8d8  jsr $1e406 */
    JSR(c, 0x0008E4, 0x01D52C);                         /* 8de  jsr $1d52c */
    JSR(c, 0x0008E8, 0x001F2E);                         /* 8e4  bsr.w $1f2e */
    JSR(c, 0x0008EC, 0x002116);                         /* 8e8  bsr.w $2116 */
    JSR(c, 0x0008F2, 0x00F41C);                         /* 8ec  jsr $f41c */
    JSR(c, 0x0008F8, 0x003D9A);                         /* 8f2  jsr $3d9a  camera */
    JSR(c, 0x0008FE, 0x01D6D8);                         /* 8f8  jsr $1d6d8 */
    JSR(c, 0x000904, 0x020496);                         /* 8fe  jsr $20496  wait_vblank */
    JSR(c, 0x00090A, 0x01CE30);                         /* 904  jsr $1ce30 */
    JSR(c, 0x00090E, 0x000A58);                         /* 90a  bsr.w $a58 */
    wr8 (0xFFE81D, 0xFF);                               /* 90e  st ($ffe81d).l */
    wr16(0xFFE7EC, 0);                                  /* 914  clr.w ($ffe7ec).l */
    wr16(0xFFE7EE, 0);                                  /* 91a  clr.w ($ffe7ee).l */
    wr16(0xFFEB64, 0);                                  /* 920  clr.w ($ffeb64).l */
    wr8 (0xFFE794, 0);                                  /* 926  clr.b ($ffe794).l */
    JSR(c, 0x000932, 0x00431E);                         /* 92c  jsr $431e */
    JSR(c, 0x000938, 0x020496);                         /* 932  jsr $20496  wait_vblank */
    JSR(c, 0x00093E, 0x020496);                         /* 938  jsr $20496  wait_vblank */
    JSR(c, 0x000944, 0x01CE16);                         /* 93e  jsr $1ce16 */
    JSR(c, 0x000948, 0x00288E);                         /* 944  bsr.w $288e */
    JSR(c, 0x00094C, 0x0029D2);                         /* 948  bsr.w $29d2 */

L94C:                                                   /*      ---- per-frame loop ---- */
    JSR(c, 0x000952, 0x003D9A);                         /* 94c  jsr $3d9a  camera */
    wr8(0xFFE81C, 0);                                   /* 952  clr.b ($ffe81c).l */
    if (rd16(0xFFE7EA) != 0) {                          /* 958  tst.w ($ffe7ea).l / 95e beq.b $97c */
        uint16_t t;
        JSR(c, 0x000964, 0x000ADE);                     /* 960  bsr.w $ade  start_skip */
        t = g4_subw(c, rd16(0xFFE7EA), 1);              /* 964  subq.w #1,($ffe7ea).l */
        wr16(0xFFE7EA, t);
        if (t != 0) goto L9E8;                          /* 96a  bne.b $9e8 */
        JSR(c, 0x000972, 0x020496);                     /* 96c  jsr $20496  wait_vblank */
        JSR(c, 0x000978, 0x01CE16);                     /* 972  jsr $1ce16 */
        JSR(c, 0x00097C, 0x000B1E);                     /* 978  bsr.w $b1e */
    }
    JSR(c, 0x000980, 0x00393E);                         /* 97c  bsr.w $393e  level timer */
    {
        int16_t n = (int16_t)rd16(0xFF0B60);            /* 980  tst.w ($ff0b60).l */
        if (n < 0) goto L9F2;                           /* 986  bmi.b $9f2  no players left */
        if (n == 0 && rd16(0xFFE838) == 2) {            /* 988  bne.b $9b8 / 98a cmpi.w #2,($ffe838).l / 992 bne.b $9b8 */
            if (rd16(0xFFE76A) == 0) goto L9F2;         /* 994  tst.w ($ffe76a).l / 99a beq.b $9f2 */
            if (rd8(0xFFE78D) != 0) {                   /* 99c  tst.b ($ffe78d).l / 9a2 beq.b $9b2 */
                wr16(0xFFE76A, 0x50);                   /* 9a4  move.w #$50,($ffe76a).l */
                wr8(0xFFE78D, 0);                       /* 9ac  clr.b ($ffe78d).l */
            }
            wr16(0xFFE76A, g4_subw(c, rd16(0xFFE76A), 1));  /* 9b2  subq.w #1,($ffe76a).l */
        }
    }
    JSR(c, 0x0009BC, 0x003B68);                         /* 9b8  bsr.w $3b68 */
    JSR(c, 0x0009C2, 0x01D6D8);                         /* 9bc  jsr $1d6d8 */
    JSR(c, 0x0009C8, 0x00431E);                         /* 9c2  jsr $431e */
    JSR(c, 0x0009CE, 0x00A82C);                         /* 9c8  jsr $a82c */
    if (rd8(0xFFE81E) != 0) {                           /* 9ce  tst.b ($ffe81e).l / 9d4 beq.b $9de */
        JSR(c, 0x0009DC, 0x01FB80);                     /* 9d6  jsr $1fb80 */
        if (c->z) goto LA1E;                            /* 9dc  beq.b $a1e  (result in Z) */
    }
    if (rd8(0xFFE81C) != 0) goto L94C;                  /* 9de  tst.b ($ffe81c).l / 9e4 bne.w $94c */
L9E8:
    JSR(c, 0x0009EE, 0x020496);                         /* 9e8  jsr $20496  wait_vblank */
    goto L94C;                                          /* 9ee  bra.w $94c */

L9F2:                                                   /*      ---- level over ---- */
    SETB(c->d[0], rd8(0xFFE866));                       /* 9f2  move.b ($ffe866).l,D0b */
    SETB(c->d[0], B(c->d[0]) & rd8(0xFFE876));          /* 9f8  and.b ($ffe876).l,D0b */
    if (B(c->d[0]) != 0) {                              /* 9fe  beq.b $a0e */
        SETW(c->d[0], 0xB4);                            /* a00  move.w #$b4,D0w */
        do {
            JSR(c, 0x000A0A, 0x020496);                 /* a04  jsr $20496  wait_vblank (3 s) */
        } while (DBF(c->d[0]));                         /* a0a  dbf D0w,$a04 */
    }
    JSR(c, 0x000A12, 0x002AA4);                         /* a0e  bsr.w $2aa4  gameover_dispatch */
    if (rd8(0xFFE794) != 0) goto L8C6;                  /* a12  tst.b ($ffe794).l / a18 bne.w $8c6 */
    goto LA52;                                          /* a1c  bra.b $a52 */

LA1E:                                                   /*      ---- quit ---- */
    wr8(0xFFE792, 0xFF);                                /* a1e  st ($ffe792).l */
    wr8(0xFFE821, 0);                                   /* a24  clr.b ($ffe821).l */
    wr8(0xFFE799, 0);                                   /* a2a  clr.b ($ffe799).l */
    wr8(0xFFE793, 0);                                   /* a30  clr.b ($ffe793).l */
    wr8(0xFFE81D, 0);                                   /* a36  clr.b ($ffe81d).l */
    c->a[0] = 0xFF0040;                                 /* a3c  lea ($ff0040).l,A0 */
    c->d[0] = 3;                                        /* a42  moveq #3,D0 */
    do {
        JSR(c, 0x000A4A, 0x00829C);                     /* a44  jsr $829c  player_reset */
        c->a[0] += 0x2C4;                               /* a4a  lea ($2c4,A0),A0 */
    } while (DBF(c->d[0]));                             /* a4e  dbf D0w,$a44 */
LA52:
    movem_pop(c, 0x7FFF);                               /* a52  movem.l (SP)+,{D0-D7 A0-A6} */
}                                                       /* a56  rts */

/* ------------------------------------------------------------------ */
/* 0x000A58  per-mode panels at level start (provisional name)         */
/* ------------------------------------------------------------------ */
void g4_level_hud_a58(M68K *c)
{
    JSR(c, 0x000A5E, 0x01EA5A);                         /* a58  jsr $1ea5a */
    JSR(c, 0x000A64, 0x01E35E);                         /* a5e  jsr $1e35e */
    if (rd8(0xFFE799) != 0 || rd16(0xFFE838) == 2) {    /* a64  tst.b ($ffe799).l / a6a bne.b $a78
                                                           a6c  cmpi.w #2,($ffe838).l / a74 beq.b $a78 / a76 bra.b $a84 */
        JSR(c, 0x000A7E, 0x01E406);                     /* a78  jsr $1e406 */
        JSR(c, 0x000A84, 0x01E342);                     /* a7e  jsr $1e342 */
    }
    if (rd16(0xFFE838) != 1) return;                    /* a84  cmpi.w #1,($ffe838).l / a8c bne.b $adc */
    if (rd16(0xFFE842) == 1) {                          /* a8e  cmpi.w #1,($ffe842).l / a96 bne.b $aac */
        JSR(c, 0x000A9E, 0x01E420);                     /* a98  jsr $1e420 */
        JSR(c, 0x000AA4, 0x01E5AC);                     /* a9e  jsr $1e5ac */
        JSR(c, 0x000AAA, 0x01E6AC);                     /* aa4  jsr $1e6ac */
        return;                                         /* aaa  bra.b $adc */
    }
    if (rd8(0xFFE874) != 0) {                           /* aac  tst.b ($ffe874).l / ab2 beq.b $ac8 */
        JSR(c, 0x000ABA, 0x01E6C8);                     /* ab4  jsr $1e6c8 */
        JSR(c, 0x000AC0, 0x01EB1E);                     /* aba  jsr $1eb1e */
        JSR(c, 0x000AC6, 0x01E87A);                     /* ac0  jsr $1e87a */
        return;                                         /* ac6  bra.b $adc */
    }
    if (rd16(0xFFE846) == 0x0B)                         /* ac8  cmpi.w #$b,($ffe846).l / ad0 bne.b $adc */
        JSR(c, 0x000AD8, 0x01E6AC);                     /* ad2  jsr $1e6ac / ad8 bra.w $adc */
}                                                       /* adc  rts */

/* ------------------------------------------------------------------ */
/* 0x000ADE  start_skip: a player newly pressing Start (bit 7 of the   */
/*   pad bytes +$22 now / +$23 previous, active low) sets the intro    */
/*   countdown to 1 so it ends this frame.                             */
/* ------------------------------------------------------------------ */
void g4_start_skip(M68K *c)
{
    movem_push(c, MV_D(0) | MV_D(1) | MV_D(2) | MV_A(0) | MV_A(1));  /* ade  movem.l {D0 D1 D2 A0 A1},-(SP) */
    SETW(c->d[2], rd16(0xFF0B60));                      /* ae2  move.w ($ff0b60).l,D2w */
    c->a[1] = 0xFF0B50;                                 /* ae8  lea ($ff0b50).l,A1 */
    do {
        c->a[0] = rd32(c->a[1]); c->a[1] += 4;          /* aee  movea.l (A1)+,A0 */
        SETB(c->d[0], rd8(c->a[0] + 0x22));             /* af0  move.b ($22,A0),D0b */
        SETB(c->d[1], rd8(c->a[0] + 0x23));             /* af4  move.b ($23,A0),D1b */
        SETB(c->d[0], ~B(c->d[0]));                     /* af8  not.b D0b */
        SETB(c->d[1], ~B(c->d[1]));                     /* afa  not.b D1b */
        SETB(c->d[1], B(c->d[1]) ^ B(c->d[0]));         /* afc  eor.b D0b,D1b */
        SETB(c->d[1], B(c->d[1]) & B(c->d[0]));         /* afe  and.b D0b,D1b */
        SETB(c->d[1], B(c->d[1]) & 0x80);               /* b00  andi.b #-$80,D1b */
        if (B(c->d[1]) != 0) {                          /* b04  beq.b $b14 */
            wr16(0xFFE7EA, 1);                          /* b06  move.w #1,($ffe7ea).l */
            wr8(0xFFE81E, 0);                           /* b0e  clr.b ($ffe81e).l */
        }
    } while (DBF(c->d[2]));                             /* b14  dbf D2w,$aee */
    movem_pop(c, MV_D(0) | MV_D(1) | MV_D(2) | MV_A(0) | MV_A(1));   /* b18  movem.l (SP)+,{D0 D1 D2 A0 A1} */
}                                                       /* b1c  rts */

/* ------------------------------------------------------------------ */
/* 0x000B1E  panel redraw when the intro countdown ends (provisional)  */
/* ------------------------------------------------------------------ */
void g4_level_hud_b1e(M68K *c)
{
    if (rd8(0xFFE791) == 0) return;                     /* b1e  tst.b ($ffe791).l / b24 beq.b $b58 */
    if (rd8(0xFFE799) != 0) return;                     /* b26  tst.b ($ffe799).l / b2c bne.b $b58 */
    SETW(c->d[1], rd16(0xFFE838));                      /* b2e  move.w ($ffe838).l,D1w */
    if (W(c->d[1]) == 2) return;                        /* b34  cmpi.w #2,D1w / b38 beq.b $b58 */
    if (W(c->d[1]) == 1) {                              /* b3a  cmpi.w #1,D1w / b3e bne.b $b52 */
        if (rd16(0xFFE842) != 0) return;                /* b40  cmpi.w #0,($ffe842).l / b48 bne.b $b58 */
        if (rd8(0xFFE875) != 0) return;                 /* b4a  tst.b ($ffe875).l / b50 bne.b $b58 */
    }
    JSR(c, 0x000B58, 0x01E35E);                         /* b52  jsr $1e35e */
}                                                       /* b58  rts */

/* ------------------------------------------------------------------ */
/* 0x000B5A  game_setup: state common to all modes, then the mode's    */
/*   own setup through the table at 0xC16                              */
/* ------------------------------------------------------------------ */
void g4_game_setup(M68K *c)
{
    static const uint32_t clr_b[] = {
        0xFFE792, 0xFFE81D, 0xFFE799, 0xFFE78D, 0xFFE79A, 0xFFE794, 0xFFE797,
        0xFFE874, 0xFFE875, 0xFFE876, 0xFFE870, 0xFFE950 };
    int i;

    movem_push(c, 0x01FF);                              /* b5a  movem.l {D0-D7 A0},-(SP) */
    SETW(c->d[0], rd16(0xFFE776));                      /* b5e  move.w ($ffe776).l,D0w */
    SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* b64  add.w D0w,D0w */
    SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* b66  add.w D0w,D0w */
    c->a[0] = 0x02667E;                                 /* b68  lea ($2667e).l,A0 */
    wr32(0xFFE778, rd32(c->a[0] + sext16(W(c->d[0])))); /* b6e  move.l (0,A0,D0w),($ffe778).l */
    wr16(0xFFE810, 0x3C);                               /* b76  move.w #$3c,($ffe810).l */
    wr16(0xFFAF14, 0xFFFF);                             /* b7e  move.w #-1,($ffaf14).l */
    wr16(0xFF0B60, 0xFFFF);                             /* b86  move.w #-1,($ff0b60).l */
    wr16(0xFFE7EA, 0);                                  /* b8e  move.w #0,($ffe7ea).l */
    for (i = 0; i < 12; i++) wr8(clr_b[i], 0);          /* b96-bd8  clr.b x12 */
    wr8(0xFFF014, 6);                                   /* bde  move.b #6,($fff014).l */
    c->a[0] = 0xFFA362;                                 /* be6  lea ($ffa362).l,A0 */
    wr8(c->a[0] + 0x4C, 0xFF);                          /* bec  st ($4c,A0) */
    wr16(c->a[0], 0);                                   /* bf0  clr.w (0,A0) */
    SETW(c->d[0], rd16(0xFFE838));                      /* bf4  move.w ($ffe838).l,D0w */
    SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* bfa  add.w D0w,D0w */
    SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* bfc  add.w D0w,D0w */
    c->a[0] = rd32(0x000C16 + sext16(W(c->d[0])));      /* bfe  movea.l ($c16,PC,D0w),A0 */
    JSR(c, 0x000C04, c->a[0]);                          /* c02  jsr (A0)  setup_arcade/quest/battle/record */
    JSR(c, 0x000C0A, 0x020496);                         /* c04  jsr $20496  wait_vblank */
    JSR(c, 0x000C10, 0x01F6D0);                         /* c0a  jsr $1f6d0 */
    movem_pop(c, 0x01FF);                               /* c10  movem.l (SP)+,{D0-D7 A0} */
}                                                       /* c14  rts */

/* ------------------------------------------------------------------ */
/* 0x000C26  setup_arcade                                              */
/* ------------------------------------------------------------------ */
void g4_setup_arcade(M68K *c)
{
    movem_push(c, MV_D(0));                             /* c26  movem.l {D0},-(SP) */
    SETW(c->d[0], rd16(0xFFE77C));                      /* c2a  move.w ($ffe77c).l,D0w */
    c->x = (W(c->d[0]) >> 13) & 1;                      /* c30  lsl.w #3,D0w  (X = last bit out) */
    SETW(c->d[0], W(c->d[0]) << 3);
    if (rd8(0xFFE790) != 0)                             /* c32  tst.b ($ffe790).l / c38 beq.b $c3c */
        SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* c3a  add.w D0w,D0w */
    wr16(0xFFE77E, W(c->d[0]));                         /* c3c  move.w D0w,($ffe77e).l */
    wr16(0xFFE780, W(c->d[0]));                         /* c42  move.w D0w,($ffe780).l */
    wr8(0xFFE79B, 0);                                   /* c48  clr.b ($ffe79b).l */
    c->d[0] = 0;                                        /* c4e  moveq #0,D0 */
    JSR(c, 0x000C54, 0x000E1C);                         /* c50  bsr.w $e1c  set_flags_e79c */
    wr8(0xFFE7BB, 0);                                   /* c54  clr.b ($ffe7bb).l */
    wr16(0xFFE73A, 2);                                  /* c5a  move.w #2,($ffe73a).l */
    wr16(0xFFE760, 1);                                  /* c62  move.w #1,($ffe760).l */
    wr16(0xFFE782, rd16(0xFFE760));                     /* c6a  move.w ($ffe760).l,($ffe782).l */
    wr16(0xFFE758, 1);                                  /* c74  move.w #1,($ffe758).l  level 1 */
    JSR(c, 0x000C80, 0x0040D8);                         /* c7c  bsr.w $40d8  level data pointer */
    wr8(0xFFE866, 0);                                   /* c80  clr.b ($ffe866).l */
    wr8(0xFFE78E, 0);                                   /* c86  clr.b ($ffe78e).l */
    movem_pop(c, MV_D(0));                              /* c8c  movem.l (SP)+,{D0} */
}                                                       /* c90  rts */

/* ------------------------------------------------------------------ */
/* 0x000C92  setup_quest                                               */
/* ------------------------------------------------------------------ */
void g4_setup_quest(M68K *c)
{
    movem_push(c, 0x7FFF);                              /* c92  movem.l {D0-D7 A0-A6},-(SP) */
    wr8(0xFFE79B, 0xFF);                                /* c96  st ($ffe79b).l */
    c->d[0] = 0xFFFFFFFFu;                              /* c9c  moveq #-1,D0 */
    JSR(c, 0x000CA2, 0x000E1C);                         /* c9e  bsr.w $e1c  set_flags_e79c */
    wr8 (0xFFE7BB, 0xFF);                               /* ca2  st ($ffe7bb).l */
    wr16(0xFFE73A, 2);                                  /* ca8  move.w #2,($ffe73a).l */
    wr8 (0xFFE871, 0);                                  /* cb0  move.b #0,($ffe871).l */
    wr16(0xFFE760, 1);                                  /* cb8  move.w #1,($ffe760).l */
    wr16(0xFFE846, 1);                                  /* cc0  move.w #1,($ffe846).l */
    wr16(0xFFE860, 0);                                  /* cc8  move.w #0,($ffe860).l */
    wr16(0xFFE862, 0);                                  /* cd0  move.w #0,($ffe862).l */
    wr16(0xFFE844, 0);                                  /* cd8  move.w #0,($ffe844).l */
    wr8 (0xFFE869, 0);                                  /* ce0  move.b #0,($ffe869).l */
    wr8 (0xFFE86F, 0);                                  /* ce8  move.b #0,($ffe86f).l */
    wr8 (0xFFE872, 0);                                  /* cf0  move.b #0,($ffe872).l */
    wr8 (0xFFE866, 0);                                  /* cf8  move.b #0,($ffe866).l */
    wr8 (0xFFE78E, 0);                                  /* d00  clr.b ($ffe78e).l */
    c->a[1] = 0x001C66;                                 /* d06  lea ($1c66).l,A1 */
    if (rd8(0xFFE86B) != 0)                             /* d0c  tst.b ($ffe86b).l / d12 beq.b $d1a */
        c->a[1] = 0x001C6A;                             /* d14  lea ($1c6a).l,A1 */
    wr32(0xFFE848, rd32(c->a[1]));                      /* d1a  move.l (A1),($ffe848).l */
    wr16(0xFFE842, 1);                                  /* d20  move.w #1,($ffe842).l */
    wr16(0xFFE758, 0x8E);                               /* d28  move.w #$8e,($ffe758).l  first Quest level */
    JSR(c, 0x000D34, 0x0040D8);                         /* d30  bsr.w $40d8  level data pointer */
    movem_pop(c, 0x7FFF);                               /* d34  movem.l (SP)+,{D0-D7 A0-A6} */
}                                                       /* d38  rts */

/* ------------------------------------------------------------------ */
/* 0x000D3A  setup_battle                                              */
/* ------------------------------------------------------------------ */
void g4_setup_battle(M68K *c)
{
    movem_push(c, MV_D(0) | MV_D(1));                   /* d3a  movem.l {D0 D1},-(SP) */
    SETW(c->d[0], rd16(0xFFE77C));                      /* d3e  move.w ($ffe77c).l,D0w */
    c->x = (W(c->d[0]) >> 13) & 1;                      /* d44  lsl.w #3,D0w */
    SETW(c->d[0], W(c->d[0]) << 3);
    if (rd8(0xFFE790) != 0)                             /* d46  tst.b ($ffe790).l / d4c beq.b $d50 */
        SETW(c->d[0], g4_addw(c, W(c->d[0]), W(c->d[0])));  /* d4e  add.w D0w,D0w */
    wr16(0xFFE77E, W(c->d[0]));                         /* d50  move.w D0w,($ffe77e).l */
    wr16(0xFFE780, W(c->d[0]));                         /* d56  move.w D0w,($ffe780).l */
    wr8(0xFFE79B, 0);                                   /* d5c  clr.b ($ffe79b).l */
    JSR(c, 0x000D66, 0x000E1C);                         /* d62  bsr.w $e1c  (D0.b as computed above) */
    wr8(0xFFE7BB, B(c->d[0]));                          /* d66  move.b D0b,($ffe7bb).l */
    wr16(0xFFE73A, 2);                                  /* d6c  move.w #2,($ffe73a).l */
    wr16(0xFFE760, 1);                                  /* d74  move.w #1,($ffe760).l */
    wr16(0xFFE95A, 1);                                  /* d7c  move.w #1,($ffe95a).l */
    if (rd16(0xFFE962) == 0) {                          /* d84  tst.w ($ffe962).l / d8a bne.b $da0 */
        SETW(c->d[1], 0x0C);                            /* d8c  move.w #$c,D1w */
        JSR(c, 0x000D96, 0x0041B8);                     /* d90  jsr $41b8  (random 0..D1?) */
        SETW(c->d[0], g4_addw(c, W(c->d[0]), 0x81));    /* d96  addi.w #$81,D0w */
        wr16(0xFFE962, W(c->d[0]));                     /* d9a  move.w D0w,($ffe962).l */
    }
    wr16(0xFFE758, rd16(0xFFE962));                     /* da0  move.w ($ffe962).l,($ffe758).l */
    JSR(c, 0x000DAE, 0x0040D8);                         /* daa  bsr.w $40d8  level data pointer */
    wr8(0xFFE866, 0);                                   /* dae  clr.b ($ffe866).l */
    wr8(0xFFE78E, 0);                                   /* db4  clr.b ($ffe78e).l */
    c->a[0] = 0xFF0040;                                 /* dba  lea ($ff0040).l,A0 */
    c->d[1] = 3;                                        /* dc0  moveq #3,D1 */
    do {
        wr8(c->a[0] + 0xEC, 0xFF);                      /* dc2  st ($ec,A0) */
        wr8(c->a[0] + 0xED, 0xFF);                      /* dc6  st ($ed,A0) */
        c->a[0] += 0x2C4;                               /* dca  lea ($2c4,A0),A0 */
    } while (DBF(c->d[1]));                             /* dce  dbf D1w,$dc2 */
    movem_pop(c, MV_D(0) | MV_D(1));                    /* dd2  movem.l (SP)+,{D0 D1} */
}                                                       /* dd6  rts */

/* ------------------------------------------------------------------ */
/* 0x000DD8  setup_record                                              */
/* ------------------------------------------------------------------ */
void g4_setup_record(M68K *c)
{
    movem_push(c, MV_D(0) | MV_D(1));                   /* dd8  movem.l {D0 D1},-(SP) */
    c->d[0] = 0;                                        /* ddc  moveq #0,D0 */
    JSR(c, 0x000DE0, 0x000E1C);                         /* dde  bsr.b $e1c  set_flags_e79c */
    wr8(0xFFE7BB, 0);                                   /* de0  clr.b ($ffe7bb).l */
    wr8(0xFFE866, 0);                                   /* de6  clr.b ($ffe866).l */
    wr16(0xFFE73A, 2);                                  /* dec  move.w #2,($ffe73a).l */
    JSR(c, 0x000DF8, 0x0040D8);                         /* df4  bsr.w $40d8  level data pointer */
    if (rd16(0xFFE760) != 1) {                          /* df8  cmpi.w #1,($ffe760).l / e00 beq.b $e16 */
        c->d[1] = 5;                                    /* e02  moveq #5,D1 */
        JSR(c, 0x000E0A, 0x0041B8);                     /* e04  jsr $41b8 */
        SETW(c->d[0], g4_addw(c, W(c->d[0]), 6));       /* e0a  addi.w #6,D0w */
        SETW(c->d[1], W(c->d[0]));                      /* e0e  move.w D0w,D1w */
        JSR(c, 0x000E16, 0x01F664);                     /* e10  jsr $1f664 */
    }
    movem_pop(c, MV_D(0) | MV_D(1));                    /* e16  movem.l (SP)+,{D0 D1} */
}                                                       /* e1a  rts */

/* ------------------------------------------------------------------ */
/* 0x000E1C  set_flags_e79c: D0.b into $ffe79c-$ffe7b2 except $ffe7ab  */
/* ------------------------------------------------------------------ */
void g4_set_flags_e79c(M68K *c)
{
    uint32_t a;
    for (a = 0xFFE79C; a <= 0xFFE7B2; a++)              /* e1c-e9a  move.b D0b,($ffe79c..$ffe7b2).l x22 */
        if (a != 0xFFE7AB) wr8(a, B(c->d[0]));
}                                                       /* ea0  rts */

/* ------------------------------------------------------------------ */
/* 0x000EA2  hiscore_init: 4 tables x 10 entries of {3 initials, 0,    */
/*   score.l}; initials from 0x25B3B, scores 8000 down by 400          */
/* ------------------------------------------------------------------ */
void g4_hiscore_init(M68K *c)
{
    movem_push(c, MV_D(1) | MV_D(2) | MV_D(3) | MV_A(1) | MV_A(2));  /* ea2  movem.l {D1 D2 D3 A1 A2},-(SP) */
    c->a[1] = 0x025B3B;                                 /* ea6  lea ($25b3b).l,A1 */
    c->a[2] = 0xFFE9BC;                                 /* eac  lea ($ffe9bc).l,A2 */
    c->d[3] = 3;                                        /* eb2  moveq #3,D3 */
    do {
        c->d[1] = 0x1F40;                               /* eb4  move.l #$1f40,D1 */
        c->d[2] = 9;                                    /* eba  moveq #9,D2 */
        do {
            wr8(c->a[2]++, rd8(c->a[1]++));             /* ebc  move.b (A1)+,(A2)+ */
            wr8(c->a[2]++, rd8(c->a[1]++));             /* ebe  move.b (A1)+,(A2)+ */
            wr8(c->a[2]++, rd8(c->a[1]++));             /* ec0  move.b (A1)+,(A2)+ */
            wr8(c->a[2]++, 0);                          /* ec2  move.b #0,(A2)+ */
            wr32(c->a[2], c->d[1]); c->a[2] += 4;       /* ec6  move.l D1,(A2)+ */
            c->d[1] = g4_subl(c, c->d[1], 0x190);       /* ec8  subi.l #$190,D1 */
        } while (DBF(c->d[2]));                         /* ece  dbf D2w,$ebc */
    } while (DBF(c->d[3]));                             /* ed2  dbf D3w,$eb4 */
    movem_pop(c, MV_D(1) | MV_D(2) | MV_D(3) | MV_A(1) | MV_A(2));   /* ed6  movem.l (SP)+,{D1 D2 D3 A1 A2} */
}                                                       /* eda  rts */

/* ------------------------------------------------------------------ */
/* 0x000EDC  level_end: per-mode handler through the table at 0xEF6    */
/*   (0xF06 Arcade, 0x19EC Quest, 0x1ED6 Battle, 0x1F22 Record)        */
/* ------------------------------------------------------------------ */
void g4_level_end(M68K *c)
{
    movem_push(c, MV_D(1) | MV_A(5));                   /* edc  movem.l {D1 A5},-(SP) */
    SETW(c->d[1], rd16(0xFFE838));                      /* ee0  move.w ($ffe838).l,D1w */
    SETW(c->d[1], g4_addw(c, W(c->d[1]), W(c->d[1])));  /* ee6  add.w D1w,D1w */
    SETW(c->d[1], g4_addw(c, W(c->d[1]), W(c->d[1])));  /* ee8  add.w D1w,D1w */
    c->a[5] = rd32(0x000EF6 + sext16(W(c->d[1])));      /* eea  movea.l ($ef6,PC,D1w),A5 */
    JSR(c, 0x000EF0, c->a[5]);                          /* eee  jsr (A5) */
    movem_pop(c, MV_D(1) | MV_A(5));                    /* ef0  movem.l (SP)+,{D1 A5} */
}                                                       /* ef4  rts */
