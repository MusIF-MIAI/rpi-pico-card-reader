/*
 * bin2deck.c -- synthesize a scatter-loader deck from a raw binary image.
 *
 * The card format is decoded from the funktionalcpu SAT deck itself and
 * cross-checked against the loader listing (CPU[1] par. 3.8; gemu
 * loader/loader.s + software/loader.txt -- the loader reads each card to
 * 0x0036 and takes LL/II from buffer offset 8):
 *
 *   bytes 0-7    card label (the loader never reads it; left blank)
 *   byte  8      LL = payload length - 1  (MVC length convention)
 *   bytes 9-10   II = load address, big-endian
 *   bytes 11..   payload, LL+1 bytes
 *
 * One byte per column, COLBIN encoding: byte bit i -> punch row B2R[i],
 * B2R = {9,8,7,6,3,2,1,0} -- the exact inverse of transcode_column's
 * TC_COLBIN. Deck layout: the embedded unit-0x80 hex loader card first
 * (verbatim from funktionalcpu.hex.cap, byte-matches the loader.s listing
 * "PER 9E 80 ..."), then BIN2DECK_CHUNK-byte payload cards, then the
 * termination card observed on the same deck: 8 bytes at 0x0000 =
 * 07 00 07 00 43 F0 <entry>  (NOP2, NOP2, jump-always) so the loader's
 * closing JU 0x0000 falls straight into the jump to the program.
 *
 * Pure C over deck_img; host-testable (test/host/test_bin.c).
 */
#include <string.h>
#include "bin2deck.h"

/* funktionalcpu.hex.cap loader card for unit 0x80 (13-bit column masks).
 * cols[2] = 0x0100 is the row-8 marker deck_find_loader_card looks for. */
static const uint16_t loader_cols[CARD_COLS] = {
    0x0200, 0x0140, 0x0100, 0x0001, 0x0001, 0x0001, 0x0004, 0x0004,
    0x0200, 0x0140, 0x0100, 0x0001, 0x0001, 0x0001, 0x0004, 0x0001,
    0x0200, 0x0140, 0x0100, 0x0001, 0x0001, 0x0001, 0x0004, 0x0040,
    0x0010, 0x1008, 0x0002, 0x0001, 0x0001, 0x0001, 0x0002, 0x0020,
    0x0120, 0x0004, 0x0001, 0x0004, 0x0001, 0x0001, 0x0002, 0x0080,
    0x0001, 0x0001, 0x0008, 0x0140, 0x0120, 0x0004, 0x0001, 0x0001,
    0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0010, 0x0002,
    0x0010, 0x0008, 0x0180, 0x0001, 0x0001, 0x0001, 0x0001, 0x0010,
    0x0001, 0x0001, 0x0010, 0x0001, 0x0100, 0x0001, 0x0104, 0x0001,
    0x0001, 0x0001, 0x0008, 0x0040, 0x0110, 0x0001, 0x0010, 0x0104,
};

static uint16_t byte_to_col(uint8_t byte)
{
    static const int b2r[8] = {9, 8, 7, 6, 3, 2, 1, 0};
    uint16_t col = 0;
    for (int i = 0; i < 8; i++)
        if (byte & (1u << i))
            col |= (uint16_t)(1u << b2r[i]);
    return col;
}

static int append_card_cols(struct deck_img *img, const uint16_t *cols)
{
    if (img->n_cards >= DECK_MAX_CARDS)
        return -1;
    uint32_t off = img->n_cards
        ? img->idx[img->n_cards - 1].off + img->idx[img->n_cards - 1].ncols
        : 0;
    if (off + CARD_COLS > DECK_MAX_COLS)
        return -1;
    memcpy(&img->cols[off], cols, CARD_COLS * sizeof(uint16_t));
    img->idx[img->n_cards].off = off;
    img->idx[img->n_cards].ncols = CARD_COLS;
    img->n_cards++;
    return 0;
}

/* One scatter card: LL+II header + payload bytes, COLBIN-encoded. */
static int append_scatter_card(struct deck_img *img, uint16_t addr,
                               const uint8_t *payload, unsigned len)
{
    uint16_t cols[CARD_COLS] = {0};   /* label + tail stay blank */
    cols[8]  = byte_to_col((uint8_t)(len - 1));
    cols[9]  = byte_to_col((uint8_t)(addr >> 8));
    cols[10] = byte_to_col((uint8_t)addr);
    for (unsigned i = 0; i < len; i++)
        cols[11 + i] = byte_to_col(payload[i]);
    return append_card_cols(img, cols);
}

static int flush_card(struct bin2deck *b)
{
    if (!b->fill)
        return 0;
    if (b->addr + b->fill > 0x10000u)
        return -1;                    /* past the 16-bit address space */
    if (append_scatter_card(b->img, (uint16_t)b->addr, b->buf, b->fill))
        return -1;
    b->addr += b->fill;
    b->fill = 0;
    return 0;
}

int bin2deck_begin(struct bin2deck *b, struct deck_img *img,
                   const char *name, uint16_t base)
{
    memset(img, 0, sizeof(*img));
    memset(b, 0, sizeof(*b));
    b->img  = img;
    b->addr = base;
    strncpy(img->name, name, sizeof(img->name) - 1);
    img->loader_card = 0;
    return append_card_cols(img, loader_cols);
}

int bin2deck_feed(struct bin2deck *b, const uint8_t *data, size_t len)
{
    while (len) {
        size_t take = BIN2DECK_CHUNK - b->fill;
        if (take > len)
            take = len;
        memcpy(b->buf + b->fill, data, take);
        b->fill += (uint8_t)take;
        data += take;
        len  -= take;
        if (b->fill == BIN2DECK_CHUNK && flush_card(b))
            return -1;
    }
    return 0;
}

int bin2deck_end(struct bin2deck *b, uint16_t entry)
{
    if (flush_card(b))
        return -1;
    /* Termination: NOP2, NOP2, jump-always to entry, laid over the loader
     * head at 0x0000 (verbatim the SAT decks' own final card pattern). */
    const uint8_t term[8] = {
        0x07, 0x00, 0x07, 0x00,
        0x43, 0xF0, (uint8_t)(entry >> 8), (uint8_t)entry,
    };
    return append_scatter_card(b->img, 0x0000, term, sizeof(term));
}
