/*
 * wire_rx.c -- capture of GE-generated signals: RE byte on TU00N (PIO1),
 * TU03N card feed (GPIO IRQ), both dispatched on core1.
 *
 * wire_rx_init() MUST run on core1: all IRQs are registered from here so
 * they land on core1's NVIC.
 *
 * Diagnostics: TU00N is ALSO sensed by a plain GPIO edge IRQ, independent
 * of the PIO path. If the scope shows fronts, tu00_edges must count; if it
 * counts while re_words does not, the fault is inside the PIO capture
 * (pad->PIO routing, SM, or FIFO/IRQ), not on the wire. See wire_rx.h.
 */
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "events.h"
#include "ge_proto.h"
#include "feeder.h"
#include "wire_rx.h"
#include "re_capture.pio.h"

static PIO  rx_pio = pio1;
static uint rx_sm;
static uint rx_offset;

static struct wire_rx_stats rxs;

const struct wire_rx_stats *wire_rx_stats(void) { return &rxs; }

unsigned wire_rx_fifo_level(void)
{
    return pio_sm_get_rx_fifo_level(rx_pio, rx_sm);
}

uint32_t wire_rx_irq_mask(void)
{
    return rx_pio->inte0;
}

/* Which instruction the capture SM will execute next -- names the stage it
 * is stuck at. "wait-tu00" forever while the scope shows fronts = the
 * strobe never reaches the PIO; "push" + rxfifo 4 = IRQ not draining. */
const char *wire_rx_sm_where(void)
{
    static const char *const where[] = {
        "wait-tu00",   /* wait 1 gpio TU00N */
        "in",          /* in pins, 13       */
        "push",        /* push block        */
        "wait-end",    /* wait 0 gpio TU00N */
    };
    uint pc = pio_sm_get_pc(rx_pio, rx_sm);
    if (pc - rx_offset < 4)
        return where[pc - rx_offset];
    return "?";
}

static void __not_in_flash_func(pio1_irq0_handler)(void)
{
    rxs.pio_irqs++;
    while (!pio_sm_is_rx_fifo_empty(rx_pio, rx_sm)) {
        uint32_t w = pio_sm_get(rx_pio, rx_sm);
        rxs.re_words++;
        /* 13-pin window GP16..GP28 (shift-left): bits 0-6 = RE01-07,
         * bit 12 = RE00; bits 7-11 (CYW43 pins, TU03N, TU00N) are junk. */
        feeder_on_re_cmd((uint8_t)(((w >> 12) & 1u) | ((w << 1) & 0xFEu)));
    }
}

static void __not_in_flash_func(gpio_irq_handler)(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio == GP_TU03N) {
        rxs.tu03_edges++;
        feeder_on_tu03();
    } else if (gpio == GP_TU00N) {
        /* Diagnostic shadow of the PIO path: raw strobe edge seen. */
        rxs.tu00_edges++;
        ev_push(EV_TU00_EDGE, 0);
    }
}

void wire_rx_init(void)
{
    rx_offset = pio_add_program(rx_pio, &re_capture_program);
    rx_sm = pio_claim_unused_sm(rx_pio, true);
    re_capture_program_init(rx_pio, rx_sm, rx_offset);

    /* TU03N: edge IRQ, routed to this core (core1). */
    gpio_init(GP_TU03N);
    gpio_set_dir(GP_TU03N, GPIO_IN);
    gpio_set_inover(GP_TU03N, GPIO_OVERRIDE_INVERT);

    /* RE capture: RXNEMPTY is level-triggered, so any word already sitting
     * in the FIFO is drained the moment the IRQ is enabled. Priorities per
     * ARCHITECTURE.md sec. 7: RE capture above the card-feed edge. */
    pio_set_irq0_source_enabled(rx_pio,
                                pis_sm0_rx_fifo_not_empty + rx_sm, true);
    irq_set_exclusive_handler(PIO1_IRQ_0, pio1_irq0_handler);
    irq_set_priority(PIO1_IRQ_0, 0x00);
    irq_set_enabled(PIO1_IRQ_0, true);

    /* Logical rising edge = wire falling = strobe asserted (inover invert).
     * TU00N's GPIO IRQ is diagnostic-only (the RE byte comes via PIO). */
    gpio_set_irq_enabled_with_callback(GP_TU03N, GPIO_IRQ_EDGE_RISE,
                                       true, gpio_irq_handler);
    gpio_set_irq_enabled(GP_TU00N, GPIO_IRQ_EDGE_RISE, true);
    irq_set_priority(IO_IRQ_BANK0, 0x40);
}
