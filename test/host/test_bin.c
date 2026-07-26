/*
 * test_bin.c -- host test for bin2deck: a pseudo-random binary must survive
 * the round trip  bytes -> cards -> transcode_column(TC_COLBIN) -> bytes,
 * with correct scatter headers, loader card and termination card.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bin2deck.h"
#include "ge_proto.h"

#define FAIL(...) do { printf("FAIL " __VA_ARGS__); printf("\n"); \
                       exit(1); } while (0)

static struct deck_img img;

static uint8_t card_byte(const struct deck_img *d, int card, int col)
{
    return transcode_column(d->cols[d->idx[card].off + col], TC_COLBIN);
}

int main(void)
{
    /* Deterministic pseudo-random binary, deliberately not a multiple of
     * the chunk size. */
    enum { N = 1494 };
    static uint8_t bin[N];
    uint32_t x = 0x2A2A2A2Au;
    for (int i = 0; i < N; i++) {
        x = x * 1664525u + 1013904223u;
        bin[i] = (uint8_t)(x >> 24);
    }

    const uint16_t base = 0x0100;
    struct bin2deck b;
    if (bin2deck_begin(&b, &img, "test.bin", base))
        FAIL("begin");
    /* Feed with awkward chunk sizes to exercise the buffering. */
    size_t pos = 0, chunk = 1;
    while (pos < N) {
        size_t take = chunk % 130 + 1;
        if (take > N - pos)
            take = N - pos;
        if (bin2deck_feed(&b, bin + pos, take))
            FAIL("feed at %zu", pos);
        pos += take;
        chunk = chunk * 7 + 3;
    }
    if (bin2deck_end(&b, base))
        FAIL("end");

    int payload_cards = (N + BIN2DECK_CHUNK - 1) / BIN2DECK_CHUNK;
    if (img.n_cards != 1 + payload_cards + 1)
        FAIL("n_cards %u != %d", img.n_cards, 1 + payload_cards + 1);
    if (img.loader_card != 0 || img.cols[2] != 0x0100)
        FAIL("loader card marker");

    /* Round-trip every payload card through the firmware's own decoder. */
    size_t off = 0;
    for (int c = 1; c <= payload_cards; c++) {
        unsigned ll = card_byte(&img, c, 8) + 1u;
        unsigned ii = (card_byte(&img, c, 9) << 8) | card_byte(&img, c, 10);
        if (ii != base + off)
            FAIL("card %d II 0x%04x != 0x%04zx", c, ii, base + off);
        if (ll > BIN2DECK_CHUNK || off + ll > N)
            FAIL("card %d LL %u", c, ll);
        for (unsigned i = 0; i < ll; i++)
            if (card_byte(&img, c, 11 + i) != bin[off + i])
                FAIL("card %d byte %u mismatch", c, i);
        off += ll;
    }
    if (off != N)
        FAIL("payload total %zu != %d", off, N);

    /* Termination card: 07 00 07 00 43 F0 <entry> at address 0. */
    int t = payload_cards + 1;
    static const uint8_t term[8] = { 0x07, 0, 0x07, 0, 0x43, 0xF0, 0x01, 0x00 };
    if (card_byte(&img, t, 8) != 7 ||
        card_byte(&img, t, 9) != 0 || card_byte(&img, t, 10) != 0)
        FAIL("termination header");
    for (int i = 0; i < 8; i++)
        if (card_byte(&img, t, 11 + i) != term[i])
            FAIL("termination byte %d", i);

    printf("bin2deck: %d bytes -> %u cards, byte-exact round trip\n",
           N, img.n_cards);

    /* IPL mode: <=40 bytes as one hex card, round-tripped through the
     * firmware's TC_HEX decoder with the IPL's hi-then-lo nibble packing. */
    uint8_t prog[40];
    for (int i = 0; i < 40; i++)
        prog[i] = bin[i * 7 % N];
    if (bin2deck_ipl(&img, "ipl.bin", prog, sizeof(prog)))
        FAIL("ipl build");
    if (img.n_cards != 1 || img.loader_card != 0)
        FAIL("ipl deck shape");
    for (int i = 0; i < 40; i++) {
        uint8_t hi = transcode_column(img.cols[2 * i],     TC_HEX);
        uint8_t lo = transcode_column(img.cols[2 * i + 1], TC_HEX);
        if (((hi << 4) | lo) != prog[i])
            FAIL("ipl byte %d: %02x != %02x", i, (hi << 4) | lo, prog[i]);
    }
    if (bin2deck_ipl(&img, "big.bin", bin, 41) == 0)
        FAIL("ipl accepted 41 bytes");

    printf("bin2deck: IPL card round trip OK\nALL OK\n");
    return 0;
}
