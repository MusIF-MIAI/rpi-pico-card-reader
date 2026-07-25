/*
 * wire_tx.c -- PIO0 presenter: LU00-07 + FININ data, LU08N/LUPOR side-set.
 *
 * See pio/presenter.pio for the waveform and word format.
 * STATUS: stub (step 3/4 of the implementation order).
 */
#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "ge_proto.h"
#include "presenter.pio.h"

static PIO  tx_pio = pio0;
static uint tx_sm;

void wire_tx_init(void)
{
    uint offset = pio_add_program(tx_pio, &presenter_program);
    tx_sm = pio_claim_unused_sm(tx_pio, true);
    presenter_program_init(tx_pio, tx_sm, offset);

    /* Output level shifters stay tri-stated (GP_SHIFT_OE high) until the
     * console leaves passive mode -- first sessions on the machine only
     * listen. */
    gpio_init(GP_SHIFT_OE);
    gpio_set_dir(GP_SHIFT_OE, GPIO_OUT);
    gpio_put(GP_SHIFT_OE, 1);   /* OE# high = disabled */

    /* TODO(step 4): TXNFULL IRQ -> feeder_on_txfeed; enable SM + drive
     * LUPOR ready level on ARM; abort/drain on DISARM/reject. */
}

void wire_tx_push(uint8_t nibble, bool fini, uint16_t w_ticks, uint16_t g_ticks)
{
    pio_sm_put_blocking(tx_pio, tx_sm,
                        presenter_word(nibble, fini, w_ticks, g_ticks));
}
