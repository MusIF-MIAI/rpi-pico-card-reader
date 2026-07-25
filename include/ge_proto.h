/*
 * ge_proto.h -- GE-120 connector-2 (card reader / COCA) protocol constants.
 *
 * Signal semantics and command codes are source-verified against the gemu
 * emulator (reader.c, cardreader.c, msl-states.c, tests/initial-load.c) and
 * the atlas backplane transcription. See docs/ARCHITECTURE.md sec. 4-5 and
 * docs/PINOUT.md for the full derivation.
 */
#ifndef GE_PROTO_H
#define GE_PROTO_H

#include <stdint.h>

/* ---- GPIO map (Pico 2 W; GP23/24/25/29 = CYW43, never used) ------------ */
/* Outputs, reader -> CPU. Active-low wires; GPIO_OVERRIDE_INVERT applied,
 * so firmware logic is active-high everywhere. */
#define GP_LU00        0   /* M1.01  data bit 0 (PIO0 out)                  */
/* ... GP1..GP7 = LU01N..LU07N, M1.02-04/06/07/09/10; only LU00-03 carry
 * data (the channel packs two low nibbles per byte), LU04-07 held inactive */
#define GP_FININ       8   /* L1.06  end-of-card, PIO0 out bit 8            */
#define GP_LU08        9   /* M1.15  char strobe, PIO0 side-set bit 0       */
#define GP_LUPOR      10   /* L1.07  ready, PIO0 side-set bit 1 (=!LU08)    */
#define GP_FIDEN      11   /* L1.03  end-of-sequence (software GPIO)        */
#define GP_POM01      12   /* M1.12  binary-mode indicator (software GPIO)  */
#define GP_LUREN      13   /* L1.09  error (software GPIO)                  */

/* Inputs, CPU -> reader. */
#define GP_TU00N      14   /* I1.15  command strobe ~1.2us (PIO1 trigger)   */
#define GP_RE0        15   /* I1.01  RE00N; GP15..GP22 = RE00..RE07,
                            * contiguous for PIO `in pins, 8`               */
#define GP_TU03N      26   /* M1.13  card-feed strobe (GPIO IRQ, core1)     */

/* Misc. */
#define GP_SCOPE_TRIG 27   /* bring-up: pulse per captured command          */
#define GP_SHIFT_OE   28   /* output level-shifter OE# (tri-state = passive)*/

/* Straps on the adapter, no GPIO: LESAB (L1.15) active, LUSEN (L1.04,
 * "LOSES") inactive, LENON (L1.13) inactive.                               */

/* ---- Reader command codes (RE byte, latched on TU00N) ------------------ */
/* gemu reader.c:6-20 + software/loader.txt.                                */
#define GE_CMD_READ_UNCHANGED  0x40  /* IPL + loader per-card read          */
#define GE_CMD_READ_NORMAL_1   0x21
#define GE_CMD_READ_NORMAL_2   0x01
#define GE_CMD_READ_MIXED_1    0x24
#define GE_CMD_READ_MIXED_2    0x04
#define GE_CMD_READ_BINARY     0x20  /* by-pass: latch COLBIN, POM01 high   */
#define GE_CMD_PUT_BINARY      0xa0
#define GE_CMD_EXAM            0x44  /* EPER: no transfer, status is wired  */
#define GE_CMD_RESET_ERROR     0x47  /* clear LUREN                         */
#define GE_CMD_CARD_REJECT     0x48
#define GE_CMD_NO_FUNCTION     0x0c

/* OPEN #1: the real loader's "set by-pass" may arrive as 0x40 under Z=0x80
 * (loader.s) rather than 0x20 (reader.c). Default policy: after the loader
 * card, present program cards as COLBIN regardless (post_loader_colbin),
 * exactly like gemu cardreader.c:325-348. Log every RE byte.               */

/* ---- Transcode modes (gemu transcode.h) --------------------------------- */
enum tc_mode {
    TC_NORMAL,   /* Hollerith->code table; never used for program loading   */
    TC_BINARY,   /* column & 0xFF                                           */
    TC_HEX,      /* loader card: nibble = sum of set digit rows; 2 cols/byte*/
    TC_COLBIN,   /* by-pass: 1 col -> 1 byte via B2R={9,8,7,6,3,2,1,0}      */
};

uint8_t transcode_column(uint16_t column, enum tc_mode mode);

/* ---- Presentation timing defaults (100 ns PIO ticks) -------------------- */
/* See ARCHITECTURE.md sec. 5. All runtime-tunable via console `set`.       */
#define T_SETUP_TICKS_DEF     5    /* S: 0.5 us data setup                  */
#define T_STROBE_TICKS_DEF   35    /* W: 3.5 us LU08N width (OPEN #2)       */
#define T_GAP_TICKS_DEF     115    /* G: 11.5 us gap (BI20 spacing ~15 us)  */
#define T_CMD_DELAY_US_DEF   20    /* D: command -> first strobe            */
#define T_FININ_TIMEOUT_US_DEF 1000 /* FININ auto-release (OPEN #4)         */

#endif /* GE_PROTO_H */
