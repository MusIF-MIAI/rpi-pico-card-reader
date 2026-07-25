/*
 * deckimg.h -- RAM image of a parsed card deck.
 *
 * A deck is parsed from a .cap file (full capture or capstrip-reduced; the
 * parser skips everything that is not "Card n. K" + 4-hex-digit tokens,
 * matching gemu cap.c). Columns are 13-bit hole masks. The image is built by
 * core0 while DISARMED and is read-only for core1 during a feed session --
 * it must live in SRAM (never flash/XIP; MSC flash writes stall XIP).
 */
#ifndef DECKIMG_H
#define DECKIMG_H

#include <stddef.h>
#include <stdint.h>

#define DECK_MAX_CARDS 512          /* largest SAT deck: 292 cards          */
#define DECK_MAX_COLS  (DECK_MAX_CARDS * 80)
#define CARD_COLS      80

struct deck_card {
    uint32_t off;                   /* index into deck_img.cols             */
    uint16_t ncols;                 /* normally 80                          */
};

struct deck_img {
    char     name[48];              /* file or batch name                   */
    uint16_t n_cards;
    int16_t  loader_card;           /* index of the hex loader card, or -1  */
    struct deck_card idx[DECK_MAX_CARDS];
    uint16_t cols[DECK_MAX_COLS];   /* 13-bit column values                 */
};

/* Streaming .cap parser: feed arbitrary text chunks, cards accumulate into
 * img. Returns 0 on success, negative on overflow/parse trouble. */
#define CAP_LINE_MAX 600            /* hex lines are 401 chars              */

struct cap_parser {
    struct deck_img *img;
    int      in_card;               /* a "Card n." header is open           */
    char     linebuf[CAP_LINE_MAX];
    int      linelen;
    int      overflow;              /* line exceeded CAP_LINE_MAX           */
    int      error;
};

void cap_parse_init(struct cap_parser *p, struct deck_img *img);
int  cap_parse_chunk(struct cap_parser *p, const char *buf, size_t len);
int  cap_parse_finish(struct cap_parser *p);   /* drop empty cards, validate */

/* Loader-card detection (gemu cardreader.c:113-148, sat_batches.c:140-151):
 * first card among indices 0..3 with cols[2] == 0x0100 (row-8 punch in
 * column 3), else 0. Sets img->loader_card and returns it. */
int deck_find_loader_card(struct deck_img *img);

#endif /* DECKIMG_H */
