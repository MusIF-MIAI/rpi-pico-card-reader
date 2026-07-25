/* deckload.h -- .cap file / SAT batch -> RAM deck image, via fat_ro. */
#ifndef DECKLOAD_H
#define DECKLOAD_H

#include "deckimg.h"
#include "fat_ro.h"
#include "surgery.h"

/* Parse one .cap file into img. If `name` is not found and ends in ".cap",
 * "<stem>.hex.cap" is tried too (capstrip naming). Returns 0 or negative. */
int deckload_file(const struct fat_vol *vol, const char *name,
                  struct deck_img *img);

/* Prepare a single file for feeding: parse into tmp, then build dst.
 * raw=1: deck as captured (box order). raw=0: operator order -- if a serial
 * loader card is detected past index 0 and the deck is big enough, apply
 * SURGERY_SERIAL_LOADER (loader first + body); otherwise keep as-is. */
int deckload_prepare(const struct fat_vol *vol, const char *name, int raw,
                     struct deck_img *dst, struct deck_img *tmp);

/* Build a SAT batch (surgery_batches[]) into dst using tmp as scratch. */
int deckload_batch(const struct fat_vol *vol, const struct surgery_batch *b,
                   struct deck_img *dst, struct deck_img *tmp);

const struct surgery_batch *deckload_find_batch(const char *name);

#endif /* DECKLOAD_H */
