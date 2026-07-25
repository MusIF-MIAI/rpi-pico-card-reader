/*
 * storage.c -- flash FAT region: MSC block device + firmware-side reads.
 *
 * Layout: config.h (FAT_FLASH_OFFSET/SIZE). The PC formats and fills the
 * region like a thumb drive; firmware reads .cap files via FatFs (to be
 * vendored under lib/fatfs in step 2 -- read-only mount).
 *
 * Write policy: flash_range_erase/program only under pico_flash
 * safe-execute with core1 lockout, and ONLY while the feeder is DISARMED
 * (usb_composite.c refuses MSC writes otherwise).
 *
 * STATUS: stub (step 2).
 */
#include <stdint.h>
#include <string.h>
#include "hardware/regs/addressmap.h"
#include "config.h"

void storage_init(void)
{
    /* TODO(step 2): sanity-check the FAT region (boot sector), expose
     * geometry to the MSC callbacks, mount FatFs read-only. */
}

int storage_read_blocks(uint32_t lba, void *buf, uint32_t count)
{
    uint32_t off = lba * 512u;
    uint32_t len = count * 512u;
    if (off + len > FAT_FLASH_SIZE)
        return -1;
    memcpy(buf, (const void *)(XIP_BASE + FAT_FLASH_OFFSET + off), len);
    return 0;
}

int storage_write_blocks(uint32_t lba, const void *buf, uint32_t count)
{
    /* pico_flash safe-execute; caller guarantees FS_DISARMED. */
    (void)lba; (void)buf; (void)count;
    return -1; /* TODO */
}
