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

static volatile uint32_t tx_feed_irqs;

uint32_t wire_tx_feed_irqs(void) { return tx_feed_irqs; }

/* TXNFULL is level-triggered: enabled only while a card is being presented
 * (an idle empty FIFO would storm). feeder_on_txfeed pushes one word per
 * entry; the IRQ re-fires while the FIFO still has room. */
static void __not_in_flash_func(pio0_irq0_handler)(void)
{
    tx_feed_irqs++;
    feeder_on_txfeed();
}

void wire_tx_init(void)
{
    tx_offset = pio_add_program(tx_pio, &presenter_program);
    tx_sm = pio_claim_unused_sm(tx_pio, true);
    presenter_program_init(tx_pio, tx_sm, tx_offset);  /* SM left disabled */

    /* LUPOB: per-card busy/ready handshake, software GPIO (the GE latches
     * its fronts -- see presenter.pio header). Bench-verified: wire HIGH =
     * busy (scanning a card), wire LOW = ready/idle. Inverted like every
     * other output (firmware logical 1 = ready = wire low). Boot = busy. */
    gpio_init(GP_LUPOR);
    gpio_set_outover(GP_LUPOR, GPIO_OVERRIDE_INVERT);
    gpio_put(GP_LUPOR, 0);
    gpio_set_dir(GP_LUPOR, GPIO_OUT);

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
    /* Enabled and stalled at `pull block`: LU08/data inactive. The ready
     * front is the feeder's call (lupob_update), not ours. */
    pio_sm_set_enabled(tx_pio, tx_sm, true);
}

void wire_tx_disarm(void)
{
    wire_tx_feed_irq(false);
    pio_sm_set_enabled(tx_pio, tx_sm, false);
    pio_sm_clear_fifos(tx_pio, tx_sm);
    /* Everything inactive. */
    pio_sm_set_pins_with_mask(tx_pio, tx_sm, 0, 0x3FFu);
}

/* LUPOB per-card handshake: ready (wire LOW) between cards while armed;
 * busy (wire HIGH) from command-accept until FININ release, while the
 * emulator is "scanning the card". The GE wants the FRONTS. */
void wire_tx_set_ready(bool ready)
{
    gpio_put(GP_LUPOR, ready);   /* inverted pad: logical ready = wire low */
}

bool wire_tx_full(void)
{
    return pio_sm_is_tx_fifo_full(tx_pio, tx_sm);
}

unsigned wire_tx_fifo_level(void)
{
    return pio_sm_get_tx_fifo_level(tx_pio, tx_sm);
}

void wire_tx_push(uint8_t nibble, bool fini, uint16_t w_ticks, uint16_t g_ticks)
{
    pio_sm_put(tx_pio, tx_sm,
               presenter_word(nibble, fini, w_ticks, g_ticks));
}

/* Deassert the standing FININ (held by the last card's OUT word) without
 * touching data or the LU08N side-set: exec a SET on GP_FININ alone with
 * the side-set bit at idle (LU08=0). Only called between cards, when the
 * SM is stalled at `pull block` -- the exec slots into the stall and the
 * pull's own side-set reasserts right after. */
void wire_tx_release_finin(void)
{
    uint32_t saved = tx_pio->sm[tx_sm].pinctrl;
    tx_pio->sm[tx_sm].pinctrl =
        (saved & ~(PIO_SM0_PINCTRL_SET_COUNT_BITS |
                   PIO_SM0_PINCTRL_SET_BASE_BITS))
        | (1u << PIO_SM0_PINCTRL_SET_COUNT_LSB)
        | ((uint32_t)GP_FININ << PIO_SM0_PINCTRL_SET_BASE_LSB);
    pio_sm_exec(tx_pio, tx_sm,
                pio_encode_set(pio_pins, 0) | pio_encode_sideset(1, 0));
    tx_pio->sm[tx_sm].pinctrl = saved;
}
