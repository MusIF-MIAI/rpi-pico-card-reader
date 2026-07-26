/*
 * wire_tx.c -- PIO0 presenter: LU00-07 + FININ data, LU08N/LUPOB side-set.
 *
 * See pio/presenter.pio for the waveform and word format. The SM is enabled
 * only while ARMED: its idle side-set holds LUPOB at ready, so arm/disarm is
 * also the ready-line control. wire_tx_init() MUST run on core1 (the TXNFULL
 * IRQ is registered from here so it lands on core1's NVIC).
 */
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/irq.h"

#include "ge_proto.h"
#include "feeder.h"
#include "wire_tx.h"
#include "presenter.pio.h"

static PIO  tx_pio = pio0;
static uint tx_sm;
static uint tx_offset;

/* TXNFULL is level-triggered: enabled only while a card is being presented
 * (an idle empty FIFO would storm). feeder_on_txfeed pushes one word per
 * entry; the IRQ re-fires while the FIFO still has room. */
static void __not_in_flash_func(pio0_irq0_handler)(void)
{
    feeder_on_txfeed();
}

void wire_tx_init(void)
{
    tx_offset = pio_add_program(tx_pio, &presenter_program);
    tx_sm = pio_claim_unused_sm(tx_pio, true);
    presenter_program_init(tx_pio, tx_sm, tx_offset);  /* SM left disabled */

    irq_set_exclusive_handler(PIO0_IRQ_0, pio0_irq0_handler);
    irq_set_priority(PIO0_IRQ_0, 0x80);   /* below RE capture and TU03N */
    irq_set_enabled(PIO0_IRQ_0, true);

    /* The output level shifters' OE# is a hardware jumper (pull-up to 3V3
     * = tri-stated/passive); firmware has no control over it. First
     * sessions on the machine only listen: leave the jumper open. */
}

void wire_tx_feed_irq(bool on)
{
    pio_set_irq0_source_enabled(tx_pio, pis_sm0_tx_fifo_not_full + tx_sm, on);
}

void wire_tx_arm(void)
{
    wire_tx_feed_irq(false);
    pio_sm_set_enabled(tx_pio, tx_sm, false);
    pio_sm_clear_fifos(tx_pio, tx_sm);
    pio_sm_restart(tx_pio, tx_sm);
    pio_sm_exec(tx_pio, tx_sm, pio_encode_jmp(tx_offset));
    /* Enabled and stalled at `pull block side 0b10`: LU08 inactive, LUPOB
     * ready -- the armed idle. */
    pio_sm_set_enabled(tx_pio, tx_sm, true);
}

void wire_tx_disarm(void)
{
    wire_tx_feed_irq(false);
    pio_sm_set_enabled(tx_pio, tx_sm, false);
    pio_sm_clear_fifos(tx_pio, tx_sm);
    /* Everything inactive, including LUPOB = not ready. */
    pio_sm_set_pins_with_mask(tx_pio, tx_sm, 0, 0x7FFu);
}

bool wire_tx_full(void)
{
    return pio_sm_is_tx_fifo_full(tx_pio, tx_sm);
}

void wire_tx_push(uint8_t nibble, bool fini, uint16_t w_ticks, uint16_t g_ticks)
{
    pio_sm_put(tx_pio, tx_sm,
               presenter_word(nibble, fini, w_ticks, g_ticks));
}

/* Deassert the standing FININ (held by the last card's OUT word) without
 * touching data or the LU08/LUPOB side-set: exec a SET on GP_FININ alone
 * with the side-set bits at the armed idle (LU08=0, LUPOB=1). Only called
 * between cards, when the SM is stalled at `pull block` -- the exec slots
 * into the stall and the pull's own side-set reasserts right after. */
void wire_tx_release_finin(void)
{
    uint32_t saved = tx_pio->sm[tx_sm].pinctrl;
    tx_pio->sm[tx_sm].pinctrl =
        (saved & ~(PIO_SM0_PINCTRL_SET_COUNT_BITS |
                   PIO_SM0_PINCTRL_SET_BASE_BITS))
        | (1u << PIO_SM0_PINCTRL_SET_COUNT_LSB)
        | ((uint32_t)GP_FININ << PIO_SM0_PINCTRL_SET_BASE_LSB);
    pio_sm_exec(tx_pio, tx_sm,
                pio_encode_set(pio_pins, 0) | pio_encode_sideset(2, 0b10));
    tx_pio->sm[tx_sm].pinctrl = saved;
}
