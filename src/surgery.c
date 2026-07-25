/*
 * surgery.c -- deck-preparation recipes, ported from gemu sat_batches.c.
 *
 * The captured .cap decks are archived in box order: a title card first,
 * the serial Hollerith loader among cards 1..4, then the program body, then
 * a summary card. Feeding one raw would hand the IPL the title card. These
 * recipes rebuild the deck the operator would actually have placed in the
 * hopper.
 *
 * Only gemu's SAT_BATCH_READER recipes are ported: those are real card
 * sequences. gemu's SAT_BATCH_IMAGE batches (cpu-functional, card-reader-a,
 * printer-mechanical) are emulator memory-staging shortcuts, not decks --
 * on the real machine those files are fed with the generic
 * SURGERY_SERIAL_LOADER recipe (or raw, at the operator's choice).
 *
 * Pure C, host-testable.
 */
#include <string.h>
#include "deckimg.h"
#include "surgery.h"

static int append_card(struct deck_img *dst, const struct deck_img *src,
                       uint16_t card)
{
    if (card >= src->n_cards || dst->n_cards >= DECK_MAX_CARDS)
        return -1;
    const struct deck_card *c = &src->idx[card];
    uint32_t off = dst->n_cards
        ? dst->idx[dst->n_cards - 1].off + dst->idx[dst->n_cards - 1].ncols
        : 0;
    if (off + c->ncols > DECK_MAX_COLS)
        return -1;
    memcpy(&dst->cols[off], &src->cols[c->off], c->ncols * sizeof(uint16_t));
    dst->idx[dst->n_cards].off = off;
    dst->idx[dst->n_cards].ncols = c->ncols;
    dst->n_cards++;
    return 0;
}

static int append_range(struct deck_img *dst, const struct deck_img *src,
                        int first, int last_inclusive)
{
    for (int i = first; i <= last_inclusive; i++)
        if (append_card(dst, src, (uint16_t)i))
            return -1;
    return 0;
}

int surgery_apply(struct deck_img *dst, struct deck_img *src,
                  enum surgery_op op)
{
    int n = src->n_cards;
    switch (op) {
    case SURGERY_AS_IS:
        return append_range(dst, src, 0, n - 1);
    case SURGERY_TRIM_TITLE_SUMMARY:
        /* Drop the title card and the summary card. */
        return (n >= 3) ? append_range(dst, src, 1, n - 2) : -1;
    case SURGERY_SERIAL_LOADER:
        /* Keep only the loader card matching this reader, then the body:
         * cards 5..N-2 (gemu sat_batches.c append_source, SERIAL op). */
        if (n < 7)
            return -1;
        if (append_card(dst, src, (uint16_t)deck_find_loader_card(src)))
            return -1;
        return append_range(dst, src, 5, n - 2);
    }
    return -1;
}

/* The reader-feedable SAT batches (gemu sat_batches.c table). */
const struct surgery_batch surgery_batches[] = {
    { "control-program-cr", "Control Program CR",
      { { "control-program-cr.cap", SURGERY_SERIAL_LOADER } }, 1 },
    { "ls600-controller-sat", "LS600 Controller SAT Batch",
      { { "sat-ls600.cap",             SURGERY_SERIAL_LOADER },
        { "ls600-controller-test.cap", SURGERY_TRIM_TITLE_SUMMARY } }, 2 },
    { "ls600-transcoder-sat", "LS600 Transcoder SAT Batch",
      { { "sat-ls600.cap",             SURGERY_SERIAL_LOADER },
        { "ls600-transcoder-test.cap", SURGERY_TRIM_TITLE_SUMMARY } }, 2 },
    { "ls600-doe-sat", "LS600 D.O.E. SAT Batch",
      { { "sat-ls600.cap", SURGERY_SERIAL_LOADER },
        { "ls600-doe.cap",  SURGERY_TRIM_TITLE_SUMMARY } }, 2 },
};

const int surgery_n_batches =
    (int)(sizeof(surgery_batches) / sizeof(surgery_batches[0]));
