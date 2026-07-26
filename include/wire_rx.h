/*
 * wire_rx.h -- GE->Pico capture (core1) + wire diagnostics.
 *
 * Every input strobe is observed at TWO independent points so a bench
 * failure names its own stage:
 *   TU00N: raw GPIO edge IRQ (tu00_edges)  vs  PIO capture (re_words)
 *   TU03N: raw GPIO edge IRQ (tu03_edges)  ->  feeder
 * plus where the capture SM is parked and the live PIO IRQ-enable mask.
 */
#ifndef WIRE_RX_H
#define WIRE_RX_H

#include <stdint.h>

void wire_rx_init(void);            /* MUST run on core1 (IRQ routing)      */

struct wire_rx_stats {
    volatile uint32_t tu00_edges;   /* GPIO IRQ, GP27 -- independent of PIO */
    volatile uint32_t tu03_edges;   /* GPIO IRQ, GP26                       */
    volatile uint32_t pio_irqs;     /* PIO1_IRQ_0 handler entries           */
    volatile uint32_t re_words;     /* words drained from the RX FIFO       */
};
const struct wire_rx_stats *wire_rx_stats(void);

unsigned    wire_rx_fifo_level(void);  /* 4 = full: SM captures, IRQ dead   */
const char *wire_rx_sm_where(void);    /* which capture instruction is next */
uint32_t    wire_rx_irq_mask(void);    /* pio1 INTE0 (0 = source disabled!) */

#endif /* WIRE_RX_H */
