/*
 * feeder.c -- the reader-side protocol engine (core1).
 *
 * Port of gemu cardreader.c's feeder with wall-clock pacing replacing the
 * emulator's RASI-gated machine cycles. Full FSM and command matrix in
 * ARCHITECTURE.md sec. 4-6; presentation cadence in sec. 5.
 *
 * All of this file must be SRAM-resident (__not_in_flash_func) -- MSC flash
 * writes stall XIP and core1 keeps running through them.
 */
#include "pico/time.h"

#include "feeder.h"
#include "events.h"
#include "ge_proto.h"
#include "status_pins.h"
#include "wire_tx.h"

struct feeder_status g_feeder_status;
struct deck_img      g_deck[2];

static struct {
    const struct deck_img *deck;   /* armed deck (RAM), NULL if disarmed    */
    enum tc_mode latched_mode;     /* from COCON-equivalent command decode  */
    uint8_t      presenting;
    uint8_t      finin_pending;    /* FININ nibble pushed, not yet released */
    uint32_t     finin_deadline;   /* time_us_32 for release path (c)       */
    /* live copies of the tunables (core1-owned; updated via IPC_SET_PARAM) */
    uint16_t     w_ticks, g_ticks, s_ticks, d_us, finin_to_us;
    uint8_t      post_loader_colbin, auto_rewind;
} fd;

static void cursor_reset(void)
{
    g_feeder_status.card = 0;
    g_feeder_status.col  = 0;
    g_feeder_status.half = 0;
    fd.presenting = 0;
    fd.finin_pending = 0;
}

void feeder_init(void)
{
    g_feeder_status.state = FS_DISARMED;
    fd.latched_mode = TC_HEX;      /* IPL reads the loader card "unchanged" */
    fd.w_ticks = T_STROBE_TICKS_DEF;
    fd.g_ticks = T_GAP_TICKS_DEF;
    fd.s_ticks = T_SETUP_TICKS_DEF;
    fd.d_us    = T_CMD_DELAY_US_DEF;
    fd.finin_to_us = T_FININ_TIMEOUT_US_DEF;
    fd.post_loader_colbin = 1;     /* OPEN #1 default policy                */
    fd.auto_rewind = 1;
}

/* Effective transcode mode for one card (gemu cardreader.c:325-348): the
 * loader card always reads in the IPL's own TC_HEX (a CPU mode-select must
 * not corrupt A-F nibbles); other cards follow the OPEN #1 policy
 * (post_loader_colbin) or the CPU-latched mode. */
static enum tc_mode effective_mode(uint16_t card)
{
    if (fd.deck->loader_card >= 0 && card == (uint16_t)fd.deck->loader_card)
        return TC_HEX;
    if (fd.post_loader_colbin)
        return TC_COLBIN;
    return fd.latched_mode;
}

/* LUPOB per-card handshake, single point of truth. The GE latches the
 * FRONTS on this line: ready (high) while armed and waiting for a command
 * or parked after a released end-of-card; busy (low) from command-accept
 * until the FININ release -- and whenever disarmed, presenting, in error,
 * or out of cards. Called after every state change. */
static void lupob_update(void)
{
    wire_tx_set_ready(fd.deck && !fd.presenting && !fd.finin_pending &&
                      (g_feeder_status.state == FS_ARMED_WAIT ||
                       g_feeder_status.state == FS_CARD_DONE));
}

/* Deassert the standing FININ -- on TU03N (a), the next command (b), or the
 * feeder_poll timeout (c); it must not leak into the next card
 * (reader.c:98-107, OPEN #4). */
static void finin_release(void)
{
    if (!fd.finin_pending)
        return;
    fd.finin_pending = 0;
    wire_tx_release_finin();
}

/* Feed: move to the next card; FS_DONE + FIDEN once the deck is exhausted. */
static void advance_card(void)
{
    g_feeder_status.col  = 0;
    g_feeder_status.half = 0;
    if (g_feeder_status.card + 1u >= fd.deck->n_cards) {
        g_feeder_status.state = FS_DONE;
        status_pin_set(GP_FIDEN, true);
        ev_push(EV_FIDEN, (uint8_t)g_feeder_status.card);
    } else {
        g_feeder_status.card++;
        g_feeder_status.state = FS_ARMED_WAIT;
    }
}

/* One nibble presentation (gemu cardreader.c:362-418): transcode the current
 * column per the effective mode; packed modes (COLBIN/BINARY) present the
 * hi then the lo nibble, one column per byte; TC_HEX/TC_NORMAL present one
 * value per column. FININ rides the last presented nibble of the card. */
static void present_next(void)
{
    const struct deck_img  *d  = fd.deck;
    uint16_t                card = g_feeder_status.card;
    const struct deck_card *dc = &d->idx[card];
    uint16_t                col = g_feeder_status.col;
    enum tc_mode            eff = effective_mode(card);

    uint8_t byte     = transcode_column(d->cols[dc->off + col], eff);
    int     packed   = (eff == TC_COLBIN || eff == TC_BINARY);
    int     last_col = (col + 1u >= dc->ncols);
    uint8_t present;
    bool    fini;

    if (packed && g_feeder_status.half == 0) {
        present = (uint8_t)((byte >> 4) & 0x0F);
        fini    = false;
    } else {
        present = packed ? (uint8_t)(byte & 0x0F) : byte;
        fini    = last_col;
    }
    wire_tx_push(present, fini, fd.w_ticks, fd.g_ticks);
    g_feeder_status.n_nibbles++;

    if (packed && g_feeder_status.half == 0) {
        g_feeder_status.half = 1;
        return;
    }
    g_feeder_status.half = 0;
    if (!last_col) {
        g_feeder_status.col = col + 1;
        return;
    }
    /* End of card: the FININ word is in the FIFO; park until TU03N, the
     * next command (auto-advance), or the release timeout. */
    g_feeder_status.col   = 0;
    fd.presenting         = 0;
    fd.finin_pending      = 1;
    fd.finin_deadline     = time_us_32() + fd.finin_to_us;
    g_feeder_status.state = FS_CARD_DONE;
    ev_push(EV_PRESENT_END, (uint8_t)card);
    wire_tx_feed_irq(false);
}

/* A read command arrived: start strobing the current card. Runs in the RE
 * capture IRQ. */
static void start_presenting(void)
{
    if (!fd.deck)
        return;
    finin_release();
    if (g_feeder_status.state == FS_CARD_DONE) {
        /* Parked at end-of-card and no TU03N came: fallback advance
         * (ARCHITECTURE.md sec. 6, OPEN #3). */
        g_feeder_status.n_autofeeds++;
        ev_push(EV_AUTOFEED, (uint8_t)g_feeder_status.card);
        advance_card();
    }
    if (g_feeder_status.state != FS_ARMED_WAIT)
        return;                        /* DONE / ERROR / already presenting */

    enum tc_mode eff = effective_mode(g_feeder_status.card);
    g_feeder_status.mode = eff;
    status_pin_set(GP_POM01, eff == TC_COLBIN || eff == TC_BINARY);

    ev_push(EV_PRESENT_START, (uint8_t)g_feeder_status.card);
    g_feeder_status.state = FS_PRESENTING;
    fd.presenting = 1;
    lupob_update();                    /* busy front, D us before strobe 1 */

    busy_wait_us_32(fd.d_us);          /* D: command -> first strobe delay */
    while (fd.presenting && !wire_tx_full())
        present_next();                /* prefill the FIFO */
    if (fd.presenting)
        wire_tx_feed_irq(true);        /* TXNFULL keeps it fed from here */
}

void feeder_on_re_cmd(uint8_t re)
{
    ev_push(EV_RE_CMD, re);
    g_feeder_status.n_cmds++;

    switch (re) {
    case GE_CMD_READ_UNCHANGED:
        start_presenting();            /* mode untouched */
        break;
    case GE_CMD_READ_NORMAL_1: case GE_CMD_READ_NORMAL_2:
    case GE_CMD_READ_MIXED_1:  case GE_CMD_READ_MIXED_2:
        fd.latched_mode = TC_NORMAL;
        ev_push(EV_MODE, TC_NORMAL);
        start_presenting();
        break;
    case GE_CMD_READ_BINARY:
        fd.latched_mode = TC_COLBIN;
        ev_push(EV_MODE, TC_COLBIN);
        start_presenting();
        break;
    case GE_CMD_PUT_BINARY:
        fd.latched_mode = TC_COLBIN;   /* logged; we never accept output */
        break;
    case GE_CMD_EXAM:
        /* No action: status wires are kept valid at all times; the CPU
         * samples them itself at state cc. */
        break;
    case GE_CMD_RESET_ERROR:
        status_pin_set(GP_LUREN, false);
        if (g_feeder_status.state == FS_ERROR)
            g_feeder_status.state = fd.deck ? FS_ARMED_WAIT : FS_DISARMED;
        break;
    case GE_CMD_CARD_REJECT:
        /* Advance past the current card without presenting it. */
        if (fd.deck && (g_feeder_status.state == FS_ARMED_WAIT ||
                        g_feeder_status.state == FS_CARD_DONE)) {
            finin_release();
            advance_card();
        }
        break;
    case GE_CMD_NO_FUNCTION:
        break;
    default:
        g_feeder_status.n_unknown_cmd++;
        ev_push(EV_ANOMALY, re);
        break;
    }
    lupob_update();
}

void feeder_on_tu03(void)
{
    ev_push(EV_TU03, 0);
    g_feeder_status.n_feeds++;
    /* Any TU03N means "feed" (OPEN #3). Only honoured when parked at an
     * end-of-card; mid-presentation or idle pulses are just logged. */
    if (fd.deck && g_feeder_status.state == FS_CARD_DONE) {
        finin_release();
        advance_card();
        lupob_update();                /* ready front: card fed, reader free */
    }
}

void feeder_on_txfeed(void)
{
    if (fd.presenting)
        present_next();
}

void feeder_on_ipc(uint32_t word)
{
    switch (IPC_OP(word)) {
    case IPC_ARM:
        fd.deck = &g_deck[IPC_ARG(word) & 1];
        fd.latched_mode = TC_HEX;
        cursor_reset();
        g_feeder_status.n_cmds = g_feeder_status.n_feeds = 0;
        g_feeder_status.n_autofeeds = g_feeder_status.n_nibbles = 0;
        g_feeder_status.n_unknown_cmd = 0;
        g_feeder_status.mode = TC_HEX;
        g_feeder_status.state = FS_ARMED_WAIT;
        status_pin_set(GP_FIDEN, false);
        status_pin_set(GP_POM01, false);
        wire_tx_arm();                 /* SM idle side-set = LUPOB ready */
        break;
    case IPC_DISARM:
        fd.deck = NULL;
        cursor_reset();
        g_feeder_status.state = FS_DISARMED;
        status_pin_set(GP_FIDEN, false);
        status_pin_set(GP_POM01, false);
        wire_tx_disarm();              /* drain FIFO, LUPOB not ready */
        break;
    case IPC_REWIND:
        cursor_reset();
        if (fd.deck) {
            g_feeder_status.state = FS_ARMED_WAIT;
            status_pin_set(GP_FIDEN, false);
            wire_tx_arm();             /* restart: aborts any in-flight card */
        }
        break;
    case IPC_EJECT:
        if (fd.deck && g_feeder_status.card + 1u < fd.deck->n_cards) {
            g_feeder_status.card++;
            g_feeder_status.col = g_feeder_status.half = 0;
        }
        break;
    case IPC_SET_PARAM:
        switch (IPC_ARG(word)) {
        case PARAM_W_TICKS:     fd.w_ticks     = IPC_VAL(word); break;
        case PARAM_G_TICKS:     fd.g_ticks     = IPC_VAL(word); break;
        case PARAM_S_TICKS:     fd.s_ticks     = IPC_VAL(word); break;
        case PARAM_D_US:        fd.d_us        = IPC_VAL(word); break;
        case PARAM_FININ_TO_US: fd.finin_to_us = IPC_VAL(word); break;
        case PARAM_POLICY:
            fd.post_loader_colbin = IPC_VAL(word) & 1;
            fd.auto_rewind        = (IPC_VAL(word) >> 1) & 1;
            break;
        }
        break;
    case IPC_INJECT_ERROR:
        status_pin_set(GP_LUREN, true);
        g_feeder_status.state = FS_ERROR;
        break;
    }
    lupob_update();
}

/* Returns nonzero while a timeout is pending, so core1's loop keeps polling
 * instead of parking in __wfe (nothing else would wake it in time). */
int feeder_poll(void)
{
    if (fd.finin_pending) {
        if ((int32_t)(time_us_32() - fd.finin_deadline) >= 0) {
            finin_release();           /* OPEN #4 release path (c) */
            lupob_update();            /* ready front on timeout release */
        } else {
            return 1;
        }
    }
    /* TODO: idle auto-rewind policy (armed + mid-deck + quiet for N s +
     * fresh 0x40). */
    return 0;
}
