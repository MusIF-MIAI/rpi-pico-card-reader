/*
 * monitor.c -- event ring (core1 writer / core0 reader) and trace printer.
 *
 * The ring is the primary instrument of the passive bring-up phase: with
 * outputs tri-stated it records the machine's real command stream (every
 * RE byte, every TU00N/TU03N) during a console LOAD attempt.
 */
#include <stdio.h>
#include "events.h"

#ifdef PICO_BUILD
#include "console.h"
#define out_printf con_printf
#else
#define out_printf printf
#endif

struct ev_ring g_events;
static int trace_on;

void monitor_set_trace(int on) { trace_on = on; }
int  monitor_trace(void)       { return trace_on; }

#ifdef PICO_BUILD
#include "pico/time.h"
static inline uint32_t now_us(void) { return time_us_32(); }
#else
static uint32_t fake_us;                    /* host tests */
static inline uint32_t now_us(void) { return fake_us += 10; }
#endif

void ev_push(uint8_t kind, uint8_t arg)
{
    uint32_t wr = g_events.wr;
    struct event *e = &g_events.ev[wr & (EV_RING_LEN - 1)];
    e->t_us = now_us();
    e->kind = kind;
    e->arg  = arg;
    e->seq  = (uint16_t)wr;
    __atomic_store_n(&g_events.wr, wr + 1, __ATOMIC_RELEASE);
}

int ev_pop(struct event *out)
{
    uint32_t wr = __atomic_load_n(&g_events.wr, __ATOMIC_ACQUIRE);
    uint32_t rd = g_events.rd;
    if (rd == wr)
        return 0;
    if (wr - rd > EV_RING_LEN)          /* overflow: skip to oldest valid */
        rd = wr - EV_RING_LEN;
    *out = g_events.ev[rd & (EV_RING_LEN - 1)];
    g_events.rd = rd + 1;
    return 1;
}

static const char *ev_name(uint8_t kind)
{
    switch (kind) {
    case EV_RE_CMD:        return "RE-CMD ";
    case EV_TU03:          return "TU03   ";
    case EV_PRESENT_START: return "PRESENT";
    case EV_PRESENT_END:   return "FININ  ";
    case EV_MODE:          return "MODE   ";
    case EV_AUTOFEED:      return "AUTOFD ";
    case EV_FIDEN:         return "FIDEN  ";
    case EV_ANOMALY:       return "ANOMALY";
    default:               return "?      ";
    }
}

#ifdef PICO_BUILD
#include "hardware/gpio.h"
#include "ge_proto.h"

#define PINS_SNAPSHOT_US (500u * 1000u)

/* Standing level of every GE->Pico input, printed each 500 ms of trace.
 * Values are logical (active-high; the wire is the inverse) -- except any
 * pin whose inover override is NORMAL, which would read as the wire. */
static void pins_snapshot(void)
{
    static uint32_t next_us;
    uint32_t t = time_us_32();
    if ((int32_t)(t - next_us) < 0)
        return;
    next_us = t + PINS_SNAPSHOT_US;

    uint8_t re = gpio_get(GP_RE0) ? 1u : 0u;
    for (unsigned i = 0; i < 7; i++)
        if (gpio_get(GP_RE1 + i))
            re |= (uint8_t)(1u << (i + 1));
    out_printf("[%10lu us] PINS    TU00=%d TU03=%d RE=0x%02x\r\n",
               (unsigned long)t, gpio_get(GP_TU00N) ? 1 : 0,
               gpio_get(GP_TU03N) ? 1 : 0, re);
}
#else
static void pins_snapshot(void) { }
#endif

/* core0 main loop. With trace off the ring is left alone, so `trace on`
 * starts by dumping the most recent EV_RING_LEN events -- free history. */
void monitor_drain(void)
{
    struct event e;
    if (!trace_on)
        return;
    while (ev_pop(&e))
        out_printf("[%10lu us] %s 0x%02x\r\n",
                   (unsigned long)e.t_us, ev_name(e.kind), e.arg);
    pins_snapshot();
}
