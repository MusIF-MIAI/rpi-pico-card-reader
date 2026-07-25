/*
 * feeder.c -- the reader-side protocol engine (core1).
 *
 * Port of gemu cardreader.c's feeder with wall-clock pacing replacing the
 * emulator's RASI-gated machine cycles. Full FSM and command matrix in
 * ARCHITECTURE.md sec. 4-6; presentation cadence in sec. 5.
 *
 * All of this file must be SRAM-resident (__not_in_flash_func) -- MSC flash
 * writes stall XIP and core1 keeps running through them.
 *
 * STATUS: skeleton. The dispatch structure is final; the TODO bodies are
 * step-1/step-4 work in the implementation order.
 */
#include "feeder.h"
#include "events.h"
#include "ge_proto.h"

struct feeder_status g_feeder_status;
struct deck_img      g_deck[2];

static struct {
    const struct deck_img *deck;   /* armed deck (RAM), NULL if disarmed    */
    enum tc_mode latched_mode;     /* from COCON-equivalent command decode  */
    uint8_t      post_loader_colbin;
    uint8_t      presenting;
    uint8_t      finin_pending;    /* FININ nibble pushed, not yet released */
} fd;

void feeder_init(void)
{
    g_feeder_status.state = FS_DISARMED;
    fd.latched_mode = TC_HEX;      /* IPL reads the loader card "unchanged" */
    fd.post_loader_colbin = 1;     /* OPEN #1 default policy                */
}

/* One nibble presentation: transcode the current column per the effective
 * mode (loader card => TC_HEX one nibble/column; post-loader => TC_COLBIN
 * hi-then-lo nibble; FININ rides the low nibble of the last column), pack a
 * presenter word and push it to PIO0. */
static void present_next(void)
{
    /* TODO(step 1/4): port cardreader.c:300-418 presentation loop:
     *   - effective mode selection (loader card vs post_loader_colbin
     *     vs latched mode), gemu cardreader.c:325-348;
     *   - nibble split + FININ ride, cardreader.c:362-376;
     *   - POM01 while binary, status_pins_set(POM01, ...);
     *   - push presenter_word(nibble, fini, cfg.w, cfg.g) via wire_tx. */
}

void feeder_on_re_cmd(uint8_t re)
{
    ev_push(EV_RE_CMD, re);
    g_feeder_status.n_cmds++;

    switch (re) {
    case GE_CMD_READ_UNCHANGED:
        /* TODO: if ARMED_WAIT/CARD_DONE: (auto-advance if parked at
         * end-of-card without TU03N), delay cfg.d_us, start presenting. */
        break;
    case GE_CMD_READ_NORMAL_1: case GE_CMD_READ_NORMAL_2:
    case GE_CMD_READ_MIXED_1:  case GE_CMD_READ_MIXED_2:
        fd.latched_mode = TC_NORMAL;
        ev_push(EV_MODE, TC_NORMAL);
        /* TODO: start presenting as above. */
        break;
    case GE_CMD_READ_BINARY:
        fd.latched_mode = TC_COLBIN;
        ev_push(EV_MODE, TC_COLBIN);
        /* TODO: POM01 high; start presenting. */
        break;
    case GE_CMD_PUT_BINARY:
        fd.latched_mode = TC_COLBIN;   /* logged; we never accept output */
        break;
    case GE_CMD_EXAM:
        /* No action: status wires are kept valid at all times; the CPU
         * samples them itself at state cc. */
        break;
    case GE_CMD_RESET_ERROR:
        /* TODO: clear LUREN latch + pin; FS_ERROR -> previous state. */
        break;
    case GE_CMD_CARD_REJECT:
        /* TODO: advance past current card without presenting. */
        break;
    case GE_CMD_NO_FUNCTION:
        break;
    default:
        g_feeder_status.n_unknown_cmd++;
        ev_push(EV_ANOMALY, re);
        break;
    }
}

void feeder_on_tu03(void)
{
    ev_push(EV_TU03, 0);
    g_feeder_status.n_feeds++;
    /* TODO: release FININ, advance to next card (or FS_DONE + FIDEN when
     * the deck is exhausted). Any TU03N means "feed" -- see OPEN #3. */
}

void feeder_on_txfeed(void)
{
    if (fd.presenting)
        present_next();
}

void feeder_on_ipc(uint32_t word)
{
    /* TODO: decode enum ipc_op (low 8 bits) + arg; ARM copies the deck
     * pointer, raises LUPOR (enables the presenter SM), REWIND resets the
     * cursor, SET_PARAM updates cfg mirrors, PASSIVE toggles GP_SHIFT_OE. */
    (void)word;
}

void feeder_poll(void)
{
    /* TODO: FININ release timeout (cfg.finin_to_us, OPEN #4); idle
     * auto-rewind policy (armed + mid-deck + quiet for N s + fresh 0x40). */
}
