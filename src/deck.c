/*
 * deck.c -- streaming .cap parser into a RAM deck image.
 *
 * Same acceptance rules as gemu cap.c cap_load(): a card opens at a line of
 * the exact form "Card n. <N>"; inside a card, only whitespace-separated
 * strict 4-hex-digit tokens are collected (masked to 13 bits); every other
 * line (banner, FEED ON/OFF, feed counters, hole-art rows, "Total cards:")
 * is ignored. Cards that collect zero columns -- the visual-section
 * duplicates in a full capture -- are dropped at finish().
 *
 * Pure C, no pico-sdk dependencies: also compiled by test/host.
 */
#include <string.h>
#include "deckimg.h"

static int is_hex4(const char *s, size_t len, uint16_t *out)
{
    if (len != 4)
        return 0;
    uint16_t v = 0;
    for (int i = 0; i < 4; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (uint16_t)(c - '0');
        else if (c >= 'A' && c <= 'F') v |= (uint16_t)(c - 'A' + 10);
        else if (c >= 'a' && c <= 'f') v |= (uint16_t)(c - 'a' + 10);
        else return 0;
    }
    *out = (uint16_t)(v & 0x1FFFu);   /* 12-bit holes + spare, bit 10 unused */
    return 1;
}

/* "Card n. <digits>" with only trailing whitespace allowed. */
static int parse_card_header(const char *line, size_t len)
{
    static const char pfx[] = "Card n. ";
    size_t p = sizeof(pfx) - 1;
    if (len < p + 1 || memcmp(line, pfx, p) != 0)
        return 0;
    size_t i = p;
    while (i < len && line[i] >= '0' && line[i] <= '9')
        i++;
    if (i == p)
        return 0;
    for (; i < len; i++)
        if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r')
            return 0;
    return 1;
}

static void process_line(struct cap_parser *p, const char *line, size_t len)
{
    struct deck_img *img = p->img;

    if (parse_card_header(line, len)) {
        if (img->n_cards >= DECK_MAX_CARDS) {
            p->error = -1;
            return;
        }
        img->idx[img->n_cards].off =
            img->n_cards ? img->idx[img->n_cards - 1].off +
                           img->idx[img->n_cards - 1].ncols
                         : 0;
        img->idx[img->n_cards].ncols = 0;
        img->n_cards++;
        p->in_card = 1;
        return;
    }
    if (!p->in_card)
        return;

    struct deck_card *card = &img->idx[img->n_cards - 1];
    size_t tok = 0;
    for (size_t i = 0; i <= len; i++) {
        int delim = (i == len) || line[i] == ' ' || line[i] == '\t' ||
                    line[i] == '\r';
        if (!delim)
            continue;
        uint16_t v;
        if (i > tok && is_hex4(line + tok, i - tok, &v)) {
            if (card->off + card->ncols >= DECK_MAX_COLS) {
                p->error = -2;
                return;
            }
            img->cols[card->off + card->ncols++] = v;
        }
        tok = i + 1;
    }
}

void cap_parse_init(struct cap_parser *p, struct deck_img *img)
{
    memset(p, 0, sizeof(*p));
    memset(img, 0, sizeof(*img));
    img->loader_card = -1;
    p->img = img;
}

int cap_parse_chunk(struct cap_parser *p, const char *buf, size_t len)
{
    for (size_t i = 0; i < len && !p->error; i++) {
        char c = buf[i];
        if (c == '\n') {
            process_line(p, p->linebuf, (size_t)p->linelen);
            p->linelen = 0;
            p->overflow = 0;
        } else if (p->linelen < CAP_LINE_MAX) {
            p->linebuf[p->linelen++] = c;
        } else {
            p->overflow = 1;   /* line too long: cannot happen in real .caps */
        }
    }
    return p->error;
}

int cap_parse_finish(struct cap_parser *p)
{
    struct deck_img *img = p->img;
    if (p->linelen)
        process_line(p, p->linebuf, (size_t)p->linelen);
    if (p->error)
        return p->error;

    /* Drop zero-column cards (visual-section duplicates), compacting the
     * index. Column data needs no move: empty cards own no columns. */
    uint16_t w = 0;
    for (uint16_t r = 0; r < img->n_cards; r++)
        if (img->idx[r].ncols)
            img->idx[w++] = img->idx[r];
    img->n_cards = w;
    return w ? 0 : -3;
}

int deck_find_loader_card(struct deck_img *img)
{
    /* gemu sat_batches.c row8_loader_card(): the CR10/Hollerith loader card
     * carries a row-8 punch in column 3 (cols[2] == 0x0100). Search cards
     * 0..4 -- captured box decks lead with a title card (loader at 1+), but
     * synthesized decks (gasm --boot/--bootge) lead with the loader itself
     * at index 0; skipping index 0 made those decks feed a body card as
     * the "loader" and the IPL executed junk. Fall back to card 1 (the
     * historical title+loader layout) when no marker is found. */
    int found = (img->n_cards > 1) ? 1 : 0;
    uint16_t limit = img->n_cards < 5 ? img->n_cards : 5;
    for (uint16_t i = 0; i < limit; i++) {
        const uint16_t *cols = &img->cols[img->idx[i].off];
        if (img->idx[i].ncols >= 3 && cols[2] == 0x0100) {
            found = i;
            break;
        }
    }
    img->loader_card = (int16_t)found;
    return found;
}
