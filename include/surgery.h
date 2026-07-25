/* surgery.h -- deck-preparation recipes (see src/surgery.c). */
#ifndef SURGERY_H
#define SURGERY_H

#include "deckimg.h"

enum surgery_op {
    SURGERY_AS_IS,               /* whole deck, box order                    */
    SURGERY_TRIM_TITLE_SUMMARY,  /* cards 1..N-2                             */
    SURGERY_SERIAL_LOADER,       /* loader card (row-8 col-3 mark) + 5..N-2  */
};

struct surgery_source {
    const char     *file;        /* .cap name on the FAT region              */
    enum surgery_op op;
};

#define SURGERY_MAX_SOURCES 2

struct surgery_batch {
    const char *name;            /* console handle                           */
    const char *title;
    struct surgery_source src[SURGERY_MAX_SOURCES];
    int n_src;
};

extern const struct surgery_batch surgery_batches[];
extern const int surgery_n_batches;

/* Append src transformed by op onto dst (dst must be initialised, possibly
 * already holding earlier sources of the same batch). Returns 0 or -1. */
int surgery_apply(struct deck_img *dst, struct deck_img *src,
                  enum surgery_op op);

#endif /* SURGERY_H */
