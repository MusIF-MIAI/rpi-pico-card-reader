/*
 * test_fat.c -- host test for the fat_ro driver + full deck-from-FAT chain.
 *
 * Fixture: a FAT16 image built by tools/mkfatimg.py containing
 * funktionalcpu.hex.cap (long filename -> exercises LFN) and a small file.
 *
 *   ./test_fat fixture.img funktionalcpu.hex.cap <original-file>
 *
 * Verifies: mount, root listing, LFN name match, byte-exact streamed read,
 * and .cap parsing straight off the FAT (114 cards).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fat_ro.h"
#include "storage.h"
#include "deckimg.h"

static int failures;
#define CHECK(cond, ...) do {                                   \
        if (!(cond)) {                                          \
            failures++;                                         \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                \
            printf("\n");                                       \
        }                                                       \
    } while (0)

/* ---- host storage shim: the image file is "flash" ----------------------- */
static uint8_t *image;
static size_t   image_len;

int storage_read_blocks(uint32_t lba, void *buf, uint32_t count)
{
    if ((size_t)(lba + count) * 512 > image_len)
        return -1;
    memcpy(buf, image + (size_t)lba * 512, (size_t)count * 512);
    return 0;
}

static struct deck_img img_buf;

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <image> <name-in-image> <original>\n",
                argv[0]);
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END);
    image_len = (size_t)ftell(f);
    rewind(f);
    image = malloc(image_len);
    if (fread(image, 1, image_len, f) != image_len) { perror("read"); return 2; }
    fclose(f);

    struct fat_vol vol;
    CHECK(fat_mount(&vol) == 0, "mount");
    printf("FAT%u, %u clusters, root at %u\n",
           vol.fat_type, vol.clusters, vol.root_start);
    CHECK(vol.fat_type == 16, "expected FAT16");

    /* listing must contain the long name */
    int seen = 0;
    int cb(const char *name, uint32_t size, void *arg) {
        (void)arg;
        printf("  %-40s %u B\n", name, size);
        if (!strcmp(name, argv[2]))
            seen = 1;
        return 0;
    }
    CHECK(fat_list(&vol, cb, NULL) >= 1, "list");
    CHECK(seen, "long filename '%s' not listed", argv[2]);

    /* byte-exact streamed read vs. the original file */
    FILE *orig = fopen(argv[3], "rb");
    if (!orig) { perror(argv[3]); return 2; }
    struct fat_file ff;
    CHECK(fat_open(&vol, argv[2], &ff) == 0, "open");
    uint8_t a[1013], b[1013];       /* odd size: crosses sector edges */
    size_t total = 0;
    int n;
    while ((n = fat_read(&ff, a, sizeof(a))) > 0) {
        size_t m = fread(b, 1, (size_t)n, orig);
        CHECK((int)m == n, "original shorter than FAT copy");
        CHECK(!memcmp(a, b, (size_t)n), "content mismatch at %zu", total);
        total += (size_t)n;
    }
    CHECK(n == 0, "read error");
    CHECK(fgetc(orig) == EOF, "FAT copy shorter than original");
    fclose(orig);
    printf("streamed %zu bytes, byte-exact\n", total);

    /* full chain: parse the deck straight off the FAT image */
    struct cap_parser p;
    cap_parse_init(&p, &img_buf);
    CHECK(fat_open(&vol, argv[2], &ff) == 0, "reopen");
    char chunk[499];
    while ((n = fat_read(&ff, chunk, sizeof(chunk))) > 0)
        CHECK(cap_parse_chunk(&p, chunk, (size_t)n) == 0, "parse");
    CHECK(cap_parse_finish(&p) == 0, "parse finish");
    printf("deck off FAT: %u cards\n", img_buf.n_cards);
    CHECK(img_buf.n_cards == 114, "funktionalcpu = 114 cards");

    printf(failures ? "FAILED (%d)\n" : "ALL OK\n", failures);
    return failures ? 1 : 0;
}
