/*
 * wire_rx.c -- capture of GE-generated signals: RE byte on TU00N (PIO1),
 * TU03N card feed (GPIO IRQ), both dispatched on core1.
 *
 * wire_rx_init() MUST run on core1: both IRQs are registered from here so
 * they land on core1's NVIC.
 */
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "ge_proto.h"
#include "feeder.h"
#include "re_capture.pio.h"

static PIO  rx_pio = pio1;
static uint rx_sm;

static void __not_in_flash_func(pio1_irq0_handler)(void)
{
    while (!pio_sm_is_rx_fifo_empty(rx_pio, rx_sm)) {
        uint32_t w = pio_sm_get(rx_pio, rx_sm);
        /* 13-pin window GP16..GP28 (shift-left): bits 0-6 = RE01-07,
         * bit 12 = RE00; bits 7-11 (CYW43 pins, TU03N, TU00N) are junk. */
        feeder_on_re_cmd((uint8_t)(((w >> 12) & 1u) | ((w << 1) & 0xFEu)));
    }
}

static void __not_in_flash_func(gpio_irq_handler)(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio == GP_TU03N)
        feeder_on_tu03();
}

void wire_rx_init(void)
{
    uint offset = pio_add_program(rx_pio, &re_capture_program);
    rx_sm = pio_claim_unused_sm(rx_pio, true);
    re_capture_program_init(rx_pio, rx_sm, offset);

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

    /* Logical rising edge = wire falling = TU03N asserted (inover invert). */
    gpio_set_irq_enabled_with_callback(GP_TU03N, GPIO_IRQ_EDGE_RISE,
                                       true, gpio_irq_handler);
    irq_set_priority(IO_IRQ_BANK0, 0x40);
}
