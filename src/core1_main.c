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
#include "ipc.h"
#include "wire_tx.h"

void wire_rx_init(void);     /* wire_rx.c: PIO1 RE capture + TU03N IRQ       */
void status_pins_init(void); /* status_pins.c: FIDEN/POM01/LUREN             */

void __not_in_flash_func(core1_entry)(void)
{
    /* Let core0's flash_safe_execute (MSC/config writes) pause this core;
     * only ever exercised while DISARMED, when core1 is idle in SRAM. */
    multicore_lockout_victim_init();

    status_pins_init();
    wire_rx_init();
    wire_tx_init();
    feeder_init();

    /* All wire IRQs (PIO1_IRQ_0, IO_IRQ_BANK0, PIO0_IRQ_0) are claimed
     * inside wire_rx_init/wire_tx_init, on this core, priorities per
     * ARCHITECTURE.md sec. 7. */

    while (true) {
        /* Op queue from core0 (non-blocking; ipc_send ends with __sev). */
        uint32_t w;
        while (ipc_try_recv(&w))
            feeder_on_ipc(w);
        if (!feeder_poll())     /* nonzero = timeout pending, keep polling */
            __wfe();
    }
}
