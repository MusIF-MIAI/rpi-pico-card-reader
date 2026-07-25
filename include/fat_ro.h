/*
 * fat_ro.h -- minimal read-only FAT12/FAT16 driver over storage_read_blocks.
 *
 * Scope: exactly what the firmware needs -- enumerate the root directory
 * and stream files out of it. No subdirectories, no writes (the PC writes
 * through raw MSC blocks; this driver re-reads the result). Long filenames
 * (VFAT LFN) are decoded because capstrip output like
 * "funktionalcpu.hex.cap" does not fit 8.3.
 */
#ifndef FAT_RO_H
#define FAT_RO_H

#include <stddef.h>
#include <stdint.h>

#define FAT_NAME_MAX 64

struct fat_vol {
    uint8_t  fat_type;        /* 12 or 16 */
    uint8_t  sec_per_clus;
    uint16_t reserved;
    uint32_t fat_start;       /* sector of first FAT                       */
    uint32_t root_start;      /* first root-directory sector               */
    uint32_t root_secs;
    uint32_t data_start;      /* first data sector (cluster 2)             */
    uint32_t clusters;        /* usable cluster count                      */
    uint32_t total_secs;
};

struct fat_file {
    const struct fat_vol *vol;
    uint32_t size;
    uint32_t pos;
    uint32_t first_clus;
    uint32_t cur_clus;        /* cluster containing pos                    */
    uint32_t clus_pos;        /* pos rounded down to cur_clus start        */
};

/* Parse the boot sector. Returns 0, or negative if the region does not
 * hold a mountable FAT12/16 volume (e.g. never formatted). */
int fat_mount(struct fat_vol *vol);

/* Iterate root-directory files (skips volume labels, subdirs, deleted).
 * cb gets the (LFN if present, else 8.3) name and size; return nonzero
 * from cb to stop early. Returns number of files visited or negative. */
int fat_list(const struct fat_vol *vol,
             int (*cb)(const char *name, uint32_t size, void *arg),
             void *arg);

/* Case-insensitive open by name. Returns 0 or negative. */
int fat_open(const struct fat_vol *vol, const char *name,
             struct fat_file *f);

/* Sequential/streaming read from the current position. Returns bytes read
 * (0 at EOF) or negative. */
int fat_read(struct fat_file *f, void *buf, size_t len);

#endif /* FAT_RO_H */
