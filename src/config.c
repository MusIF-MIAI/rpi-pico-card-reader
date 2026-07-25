/*
 * config.c -- tunables: defaults, flash persistence.
 * STATUS: defaults implemented; flash IO is step-2 work.
 */
#include <string.h>
#include "config.h"

struct cfg g_cfg;

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
    c->passive            = 1;   /* boot listening, never driving */
}

void cfg_load(void)
{
    /* TODO(step 2): read CFG_FLASH_OFFSET via XIP, validate magic+crc. */
    cfg_defaults(&g_cfg);
}

int cfg_save(void)
{
    /* TODO(step 2): pico_flash safe-execute; only while FS_DISARMED. */
    return -1;
}
