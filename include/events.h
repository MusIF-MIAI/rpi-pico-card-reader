/*
 * events.h -- lock-free single-writer (core1) / single-reader (core0) event
 * ring. Every wire event is recorded with a microsecond timestamp; core0
 * pretty-prints in `trace` mode and keeps counters for `status`.
 *
 * This ring is the primary instrument of the passive bring-up phase: with
 * outputs tri-stated it captures the machine's real command stream during a
 * console LOAD attempt (resolves OPEN #1/#3 in ARCHITECTURE.md).
 */
#ifndef EVENTS_H
#define EVENTS_H

#include <stdint.h>

enum ev_kind {
    EV_RE_CMD = 1,      /* arg = RE byte (TU00N-latched)                     */
    EV_TU03,            /* card-feed strobe                                  */
    EV_PRESENT_START,   /* arg = card index                                  */
    EV_PRESENT_END,     /* arg = card index (FININ nibble pushed)            */
    EV_MODE,            /* arg = new tc_mode                                 */
    EV_AUTOFEED,        /* advance without TU03N                             */
    EV_FIDEN,           /* deck exhausted                                    */
    EV_ANOMALY,         /* arg = anomaly code                                */
};

struct event {
    uint32_t t_us;      /* time_us_32() at capture                           */
    uint8_t  kind;      /* enum ev_kind                                      */
    uint8_t  arg;
    uint16_t seq;
};

#define EV_RING_LEN 1024              /* power of two                        */

struct ev_ring {
    struct event     ev[EV_RING_LEN];
    volatile uint32_t wr;             /* written by core1 only               */
    volatile uint32_t rd;             /* written by core0 only               */
};

extern struct ev_ring g_events;

/* core1, IRQ-safe (single writer). Drops oldest on overflow. */
void ev_push(uint8_t kind, uint8_t arg);

/* core0. Returns 0 if empty. */
int ev_pop(struct event *out);

#endif /* EVENTS_H */
