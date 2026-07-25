/*
 * core1_main.c -- core1 entry: claim wire IRQs, run the realtime loop.
 *
 * core1 owns everything that touches the GE: PIO0 presenter, PIO1 RE
 * capture, TU03N GPIO IRQ, status pins. It never calls flash-resident code
 * after init (MSC flash writes stall XIP).
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/irq.h"

#include "feeder.h"
#include "ge_proto.h"

void wire_tx_init(void);     /* wire_tx.c: PIO0 presenter                    */
void wire_rx_init(void);     /* wire_rx.c: PIO1 RE capture + TU03N IRQ       */
void status_pins_init(void); /* status_pins.c: FIDEN/POM01/LUREN             */

void __not_in_flash_func(core1_entry)(void)
{
    status_pins_init();
    wire_rx_init();
    wire_tx_init();
    feeder_init();

    /* TODO(step 4): irq_set_exclusive_handler + irq_set_enabled here (on
     * THIS core) for PIO1_IRQ_0, PIO0_IRQ_0, IO_IRQ_BANK0; priorities per
     * ARCHITECTURE.md sec. 7. */

    while (true) {
        /* Mailbox from core0 (non-blocking). */
        while (multicore_fifo_rvalid())
            feeder_on_ipc(multicore_fifo_pop_blocking());
        feeder_poll();
        __wfe();
    }
}
