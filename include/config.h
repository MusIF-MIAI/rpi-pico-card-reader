/*
 * config.h -- runtime tunables, persisted in a 4 KB flash sector.
 *
 * Flash map (4 MB Pico 2 W):
 *   0x000000  firmware (XIP)
 *   0x0F0000  this config sector (4 KB)
 *   0x100000  FAT16 region exposed over USB MSC (3 MB, .cap files)
 */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "ge_proto.h"

#define CFG_FLASH_OFFSET   0x0F0000u
#define FAT_FLASH_OFFSET   0x100000u
#define FAT_FLASH_SIZE     0x300000u
#define CFG_MAGIC          0x47453132u   /* "GE12" */

struct cfg {
    uint32_t magic;
    uint16_t w_ticks;        /* LU08N width                                  */
    uint16_t g_ticks;        /* gap                                          */
    uint16_t s_ticks;        /* setup                                        */
    uint16_t d_us;           /* command -> first strobe                      */
    uint16_t finin_to_us;
    uint8_t  post_loader_colbin;  /* OPEN #1 policy, default 1               */
    uint8_t  auto_rewind;         /* idle auto-rewind, default 1             */
    uint8_t  auto_rewind_s;       /* idle threshold, default 5               */
    uint8_t  passive;             /* boot with outputs tri-stated, default 1 */
    char     last_deck[48];
    uint32_t crc;
};

extern struct cfg g_cfg;

void cfg_load(void);        /* flash -> g_cfg, defaults if invalid          */
int  cfg_save(void);        /* g_cfg -> flash; only while DISARMED          */
void cfg_defaults(struct cfg *c);

#endif /* CONFIG_H */
