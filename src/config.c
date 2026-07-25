/*
 * config.c -- tunables: defaults, 4 KB flash sector persistence.
 * cfg_save must only run while the feeder is DISARMED (console enforces:
 * arm blocks while armed anyway; flash_safe_execute locks out core1).
 */
#include <string.h>

#include "pico/flash.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"

#include "config.h"
#include "ipc.h"

struct cfg g_cfg;

static uint32_t crc32(const void *data, size_t len)
{
    const uint8_t *p = data;
    uint32_t crc = 0xFFFFFFFFu;
    while (len--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
    }
    return ~crc;
}

void cfg_defaults(struct cfg *c)
{
    memset(c, 0, sizeof(*c));
    c->magic              = CFG_MAGIC;
    c->w_ticks            = T_STROBE_TICKS_DEF;
    c->g_ticks            = T_GAP_TICKS_DEF;
    c->s_ticks            = T_SETUP_TICKS_DEF;
    c->d_us               = T_CMD_DELAY_US_DEF;
    c->finin_to_us        = T_FININ_TIMEOUT_US_DEF;
    c->post_loader_colbin = 1;   /* OPEN #1 policy */
    c->auto_rewind        = 1;
    c->auto_rewind_s      = 5;
}

void cfg_load(void)
{
    const struct cfg *f = (const struct cfg *)(XIP_BASE + CFG_FLASH_OFFSET);
    if (f->magic == CFG_MAGIC &&
        f->crc == crc32(f, offsetof(struct cfg, crc))) {
        g_cfg = *f;
        return;
    }
    cfg_defaults(&g_cfg);
}

struct save_args { const uint8_t *data; };

static void do_save(void *param)
{
    struct save_args *a = param;
    flash_range_erase(CFG_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CFG_FLASH_OFFSET, a->data, FLASH_PAGE_SIZE);
}

int cfg_save(void)
{
    if (g_feeder_status.state != FS_DISARMED)
        return -1;
    static uint8_t page[FLASH_PAGE_SIZE];       /* 256 B >= sizeof(cfg) */
    _Static_assert(sizeof(struct cfg) <= FLASH_PAGE_SIZE, "cfg too big");
    g_cfg.crc = crc32(&g_cfg, offsetof(struct cfg, crc));
    memset(page, 0xFF, sizeof(page));
    memcpy(page, &g_cfg, sizeof(g_cfg));
    struct save_args a = { page };
    return flash_safe_execute(do_save, &a, 2000) == PICO_OK ? 0 : -2;
}
