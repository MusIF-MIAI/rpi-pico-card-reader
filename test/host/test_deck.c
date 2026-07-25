/*
 * test_deck.c -- host-built unit test for the pure-C firmware core:
 * .cap streaming parser, transcode decoders, loader-card detection,
 * deck surgery. Run against a real capture:
 *
 *   ./test_deck ../../../gemu/Site_Acceptance_Test/funktionalcpu.cap
 *
 * Expectations for funktionalcpu.cap come from gemu (cap.c tests, docs):
 * 114 hex cards of 80 columns (the visual-section duplicates are dropped),
 * serial loader card among indices 1..4, TC_HEX-decoded loader starting
 * 9E 80 00 (PER 0x80, ...).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "deckimg.h"
#include "surgery.h"
#include "ge_proto.h"

static int failures;

#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                \
            printf("\n");                                       \
        }                                                       \
    } while (0)

static struct deck_img img, out;   /* large: keep off the stack */

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <deck.cap>\n", argv[0]);
        return 2;
    }

    /* --- transcode unit checks (values from gemu docs/punchcards.md) --- */
    /* rows 9+1 punched = 0x0202: COLBIN 0x41, BINARY 0x02 */
    CHECK(transcode_column(0x0202, TC_COLBIN) == 0x41, "colbin 0x0202");
    CHECK(transcode_column(0x0202, TC_BINARY) == 0x02, "binary 0x0202");
    /* TC_HEX: rows 2+8 punched -> 2+8 = 10 = 0xA */
    CHECK(transcode_column((1u << 2) | (1u << 8), TC_HEX) == 0x0A, "hex A");
    /* TC_HEX: row 9 alone -> 9 */
    CHECK(transcode_column(1u << 9, TC_HEX) == 0x09, "hex 9");
    /* COLBIN row 0 is the MSB */
    CHECK(transcode_column(1u << 0, TC_COLBIN) == 0x80, "colbin msb");

    /* --- parse the capture in awkward chunk sizes (streaming test) ----- */
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }

    struct cap_parser p;
    cap_parse_init(&p, &img);
    char buf[777];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        CHECK(cap_parse_chunk(&p, buf, n) == 0, "parse chunk");
    fclose(f);
    CHECK(cap_parse_finish(&p) == 0, "parse finish");

    printf("deck: %u cards\n", img.n_cards);
    CHECK(img.n_cards > 0, "no cards");
    for (uint16_t i = 0; i < img.n_cards; i++)
        CHECK(img.idx[i].ncols == CARD_COLS,
              "card %u has %u cols", i, img.idx[i].ncols);

    if (strstr(argv[1], "funktionalcpu"))
        CHECK(img.n_cards == 114, "funktionalcpu must have 114 hex cards");

    /* --- loader-card detection + TC_HEX decode of its head ------------- */
    int loader = deck_find_loader_card(&img);
    printf("loader card: index %d\n", loader);
    const uint16_t *lc = &img.cols[img.idx[loader].off];
    CHECK(lc[2] == 0x0100 || loader <= 1, "loader mark col3");

    /* Channel packing: byte = (low nibble of col k) << 4 | (low nibble of
     * col k+1) -- tests/initial-load.c:85-106. Loader must open 9E 80 00. */
    uint8_t head[3];
    for (int i = 0; i < 3; i++) {
        uint8_t hi = transcode_column(lc[2 * i],     TC_HEX);
        uint8_t lo = transcode_column(lc[2 * i + 1], TC_HEX);
        head[i] = (uint8_t)((hi << 4) | lo);
    }
    printf("loader head: %02X %02X %02X\n", head[0], head[1], head[2]);
    CHECK(head[0] == 0x9E && head[1] == 0x80 && head[2] == 0x00,
          "loader must start 9E 80 00 (PER 0x80, ...)");

    /* --- surgery: SERIAL_LOADER = loader + cards 5..N-2 ----------------- */
    memset(&out, 0, sizeof(out));
    CHECK(surgery_apply(&out, &img, SURGERY_SERIAL_LOADER) == 0, "surgery");
    CHECK(out.n_cards == 1 + (img.n_cards - 2 - 5 + 1),
          "serial-loader card count (got %u)", out.n_cards);
    CHECK(memcmp(&out.cols[out.idx[0].off], lc,
                 CARD_COLS * sizeof(uint16_t)) == 0,
          "surgery card 0 must be the loader card");

    /* --- trim: cards 1..N-2 --------------------------------------------- */
    memset(&out, 0, sizeof(out));
    CHECK(surgery_apply(&out, &img, SURGERY_TRIM_TITLE_SUMMARY) == 0, "trim");
    CHECK(out.n_cards == img.n_cards - 2, "trim card count");

    printf(failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}
