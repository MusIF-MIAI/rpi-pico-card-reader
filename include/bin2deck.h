/* bin2deck.h -- synthesize a scatter-loader deck from a raw binary image. */
#ifndef BIN2DECK_H
#define BIN2DECK_H

#include <stddef.h>
#include <stdint.h>
#include "deckimg.h"

#define BIN2DECK_CHUNK 66u   /* payload bytes per card (the SAT decks' own) */

/* Lowest usable load address: the loader occupies 0x0000-0x0035 and reads
 * each card into the 0x0036-0x0085 buffer. */
#define BIN2DECK_MIN_BASE 0x0086u

struct bin2deck {
    struct deck_img *img;
    uint32_t addr;               /* next load address                       */
    uint8_t  fill;
    uint8_t  buf[BIN2DECK_CHUNK];
};

/* Resets img, appends the embedded unit-0x80 loader card, sets the load
 * base. Returns 0 or negative. */
int bin2deck_begin(struct bin2deck *b, struct deck_img *img,
                   const char *name, uint16_t base);

/* Append binary data (any chunking). Returns 0 or negative (deck full /
 * address space overrun). */
int bin2deck_feed(struct bin2deck *b, const uint8_t *data, size_t len);

/* Flush the partial card and append the termination card (jump to entry).
 * Returns 0 or negative. */
int bin2deck_end(struct bin2deck *b, uint16_t entry);

/* IPL mode ("@0"): the whole program as ONE hex card -- the IPL itself
 * nibble-packs it to 0x0000 and executes it there. No loader, no
 * termination; len <= BIN2DECK_IPL_MAX bytes. */
#define BIN2DECK_IPL_MAX 40u
int bin2deck_ipl(struct deck_img *img, const char *name,
                 const uint8_t *data, size_t len);

#endif /* BIN2DECK_H */
