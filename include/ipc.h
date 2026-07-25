/*
 * ipc.h -- core0 <-> core1 protocol.
 *
 * core0 -> core1: 32-bit opcodes over the SIO multicore FIFO (low 8 bits op,
 * high 24 bits argument). core1 never blocks on core0: it publishes into the
 * volatile status struct and the lock-free event ring; core0 polls.
 */
#ifndef IPC_H
#define IPC_H

#include <stdint.h>
#include "deckimg.h"

/* FIFO word layout: op in bits 7:0, small arg in bits 15:8 (deck slot,
 * on/off, param id), 16-bit value in bits 31:16 (SET_PARAM). */
#define IPC_WORD(op, arg, val) \
    ((uint32_t)(op) | ((uint32_t)(arg) << 8) | ((uint32_t)(val) << 16))
#define IPC_OP(w)   ((uint8_t)(w))
#define IPC_ARG(w)  ((uint8_t)((w) >> 8))
#define IPC_VAL(w)  ((uint16_t)((w) >> 16))

enum ipc_op {
    IPC_ARM = 1,        /* arg: deck slot (0/1) in g_deck[]                  */
    IPC_DISARM,
    IPC_REWIND,
    IPC_EJECT,          /* skip current card                                 */
    IPC_SET_PARAM,      /* arg: enum ipc_param; val: new value               */
    IPC_INJECT_ERROR,   /* assert LUREN until GE sends 0x47                  */
};

enum ipc_param {
    PARAM_W_TICKS = 1,  /* LU08N width, 100 ns units                         */
    PARAM_G_TICKS,      /* inter-strobe gap                                  */
    PARAM_S_TICKS,      /* data setup                                        */
    PARAM_D_US,         /* command -> first strobe delay                     */
    PARAM_FININ_TO_US,  /* FININ auto-release timeout                        */
    PARAM_POLICY,       /* bit0: post_loader_colbin, bit1: auto_rewind, ...  */
};

enum feeder_state {
    FS_DISARMED = 0,
    FS_ARMED_WAIT,      /* deck ready, waiting for a read command            */
    FS_PRESENTING,      /* strobing nibbles                                  */
    FS_CARD_DONE,       /* FININ presented; waiting TU03N / next command     */
    FS_DONE,            /* deck exhausted; FIDEN high                        */
    FS_ERROR,           /* LUREN high; cleared by GE cmd 0x47 or console     */
};

/* Written only by core1 (single writer), read by core0. */
struct feeder_status {
    volatile uint8_t  state;         /* enum feeder_state                    */
    volatile uint8_t  mode;          /* enum tc_mode currently latched       */
    volatile uint16_t card;          /* current card index                   */
    volatile uint8_t  col;           /* current column                       */
    volatile uint8_t  half;          /* 0 = hi nibble next, 1 = lo           */
    volatile uint32_t n_cmds;        /* TU00N strobes seen                   */
    volatile uint32_t n_feeds;       /* TU03N pulses seen                    */
    volatile uint32_t n_autofeeds;   /* advances taken without TU03N         */
    volatile uint32_t n_nibbles;     /* presentations pushed to PIO          */
    volatile uint32_t n_unknown_cmd; /* RE bytes outside the matrix          */
};

extern struct feeder_status g_feeder_status;
extern struct deck_img      g_deck[2];      /* double buffer, SRAM           */

#endif /* IPC_H */
