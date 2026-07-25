/*
 * storage.c -- flash FAT region: block reads via XIP, block writes via a
 * 4 KB sector cache + pico_flash safe-execute.
 *
 * Layout: config.h (FAT_FLASH_OFFSET/SIZE). The PC formats and fills the
 * region through USB MSC like a thumb drive; the firmware reads it with the
 * fat_ro driver.
 *
 * Write policy: the MSC layer only accepts writes while the feeder is
 * DISARMED (tud_msc_is_writable_cb). flash_safe_execute wins the XIP bus by
 * locking out core1 -- harmless then, since a disarmed core1 idles in SRAM.
 * Erase granularity is 4 KB, MSC blocks are 512 B: writes land in a one-
 * sector cache flushed on sector change, SCSI sync, or idle timeout.
 */
#include <stdint.h>
#include <string.h>

#include "pico/flash.h"
#include "pico/time.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"

#include "config.h"
#include "storage.h"

#define SECTOR_SIZE   4096u
#define BLOCKS_PER_SECTOR (SECTOR_SIZE / 512u)
#define FLUSH_IDLE_US (500 * 1000)

static struct {
    uint8_t  data[SECTOR_SIZE];
    uint32_t sector;              /* sector index within the FAT region */
    int      dirty;
    uint64_t last_write_us;
} cache = { .dirty = 0, .sector = UINT32_MAX };

void storage_init(void)
{
    cache.sector = UINT32_MAX;
    cache.dirty = 0;
}

int storage_read_blocks(uint32_t lba, void *buf, uint32_t count)
{
    uint32_t off = lba * 512u;
    uint32_t len = count * 512u;
    if (off + len > FAT_FLASH_SIZE)
        return -1;
    memcpy(buf, (const void *)(XIP_BASE + FAT_FLASH_OFFSET + off), len);

    /* Read-through: if part of the range sits dirty in the cache, overlay
     * it so the host never sees stale flash under cached writes. */
    if (cache.dirty) {
        uint32_t coff = cache.sector * SECTOR_SIZE;
        uint32_t lo = off > coff ? off : coff;
        uint32_t hi = (off + len) < (coff + SECTOR_SIZE) ? (off + len)
                                                         : (coff + SECTOR_SIZE);
        if (lo < hi)
            memcpy((uint8_t *)buf + (lo - off), &cache.data[lo - coff],
                   hi - lo);
    }
    return 0;
}

struct flush_args { uint32_t flash_off; const uint8_t *data; };

static void do_flush(void *param)
{
    struct flush_args *a = param;
    flash_range_erase(a->flash_off, SECTOR_SIZE);
    flash_range_program(a->flash_off, a->data, SECTOR_SIZE);
}

int storage_flush(void)
{
    if (!cache.dirty)
        return 0;
    struct flush_args a = {
        .flash_off = FAT_FLASH_OFFSET + cache.sector * SECTOR_SIZE,
        .data = cache.data,
    };
    if (flash_safe_execute(do_flush, &a, 2000) != PICO_OK)
        return -1;
    cache.dirty = 0;
    return 0;
}

static int cache_load(uint32_t sector)
{
    if (cache.sector == sector)
        return 0;
    if (storage_flush())
        return -1;
    memcpy(cache.data,
           (const void *)(XIP_BASE + FAT_FLASH_OFFSET + sector * SECTOR_SIZE),
           SECTOR_SIZE);
    cache.sector = sector;
    return 0;
}

int storage_write_blocks(uint32_t lba, const void *buf, uint32_t count)
{
    if ((lba + count) * 512u > FAT_FLASH_SIZE)
        return -1;
    const uint8_t *src = buf;
    for (uint32_t b = 0; b < count; b++, src += 512, lba++) {
        if (cache_load(lba / BLOCKS_PER_SECTOR))
            return -1;
        memcpy(&cache.data[(lba % BLOCKS_PER_SECTOR) * 512u], src, 512);
        cache.dirty = 1;
        cache.last_write_us = time_us_64();
    }
    return 0;
}

void storage_poll(void)
{
    if (cache.dirty && time_us_64() - cache.last_write_us > FLUSH_IDLE_US)
        storage_flush();
}
