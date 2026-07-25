/*
 * deckload.c -- .cap file / SAT batch -> RAM deck image (core0, DISARMED
 * only). Pure C over fat_ro + the cap parser; host-testable.
 */
#include <string.h>
#include "deckload.h"

static int open_with_fallback(const struct fat_vol *vol, const char *name,
                              struct fat_file *f)
{
    if (fat_open(vol, name, f) == 0)
        return 0;
    /* "x.cap" -> "x.hex.cap" (capstrip output naming) */
    size_t n = strlen(name);
    if (n > 4 && n < FAT_NAME_MAX - 4 && !strcmp(name + n - 4, ".cap")) {
        char alt[FAT_NAME_MAX + 8];
        memcpy(alt, name, n - 4);
        strcpy(alt + n - 4, ".hex.cap");
        if (fat_open(vol, alt, f) == 0)
            return 0;
    }
    return -1;
}

int deckload_file(const struct fat_vol *vol, const char *name,
                  struct deck_img *img)
{
    struct fat_file f;
    if (open_with_fallback(vol, name, &f))
        return -1;

    struct cap_parser p;
    cap_parse_init(&p, img);
    char buf[512];
    int n;
    while ((n = fat_read(&f, buf, sizeof(buf))) > 0)
        if (cap_parse_chunk(&p, buf, (size_t)n))
            return -2;
    if (n < 0 || cap_parse_finish(&p))
        return -2;

    strncpy(img->name, name, sizeof(img->name) - 1);
    img->name[sizeof(img->name) - 1] = 0;
    deck_find_loader_card(img);
    return 0;
}

int deckload_prepare(const struct fat_vol *vol, const char *name, int raw,
                     struct deck_img *dst, struct deck_img *tmp)
{
    if (deckload_file(vol, name, tmp))
        return -1;

    memset(dst, 0, sizeof(*dst));
    dst->loader_card = -1;
    enum surgery_op op = SURGERY_AS_IS;
    if (!raw && tmp->loader_card > 0 && tmp->n_cards >= 7)
        op = SURGERY_SERIAL_LOADER;
    if (surgery_apply(dst, tmp, op))
        return -2;

    strncpy(dst->name, tmp->name, sizeof(dst->name) - 1);
    deck_find_loader_card(dst);
    return 0;
}

int deckload_batch(const struct fat_vol *vol, const struct surgery_batch *b,
                   struct deck_img *dst, struct deck_img *tmp)
{
    memset(dst, 0, sizeof(*dst));
    dst->loader_card = -1;
    for (int i = 0; i < b->n_src; i++) {
        if (deckload_file(vol, b->src[i].file, tmp))
            return -1;
        if (surgery_apply(dst, tmp, b->src[i].op))
            return -2;
    }
    strncpy(dst->name, b->name, sizeof(dst->name) - 1);
    dst->name[sizeof(dst->name) - 1] = 0;
    deck_find_loader_card(dst);
    return 0;
}

const struct surgery_batch *deckload_find_batch(const char *name)
{
    for (int i = 0; i < surgery_n_batches; i++)
        if (!strcmp(surgery_batches[i].name, name))
            return &surgery_batches[i];
    return NULL;
}
