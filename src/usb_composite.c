/*
 * usb_composite.c -- TinyUSB device: CDC-ACM console + MSC deck drive.
 *
 * The MSC LUN maps 1:1 onto the FAT flash region (config.h). Reads come
 * straight from XIP. Writes are refused for now (unit reports
 * write-protected): the safe flash-write path (pico_flash lockout, and only
 * while the feeder is DISARMED) is step-2 work -- see storage.c. Until then
 * the FAT region is populated with picotool/openocd.
 */
#include "tusb.h"
#include "config.h"
#include "ipc.h"
#include "storage.h"

void usb_composite_init(void)
{
    tud_init(0);
}

void usb_composite_task(void)
{
    tud_task();
}

/* ---- MSC callbacks ------------------------------------------------------ */

void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8],
                        uint8_t product_id[16], uint8_t product_rev[4])
{
    (void)lun;
    memcpy(vendor_id,  "GE-120  ", 8);
    memcpy(product_id, "Card decks      ", 16);
    memcpy(product_rev, "1.0 ", 4);
}

bool tud_msc_test_unit_ready_cb(uint8_t lun)
{
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t *block_count,
                         uint16_t *block_size)
{
    (void)lun;
    *block_count = FAT_FLASH_SIZE / 512;
    *block_size  = 512;
}

bool tud_msc_is_writable_cb(uint8_t lun)
{
    (void)lun;
    /* Writable only while no feed session is armed: flash programming
     * stalls XIP and must never race a live LOAD. */
    return g_feeder_status.state == FS_DISARMED;
}

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                          void *buffer, uint32_t bufsize)
{
    (void)lun;
    if (offset % 512 || bufsize % 512)
        return -1;
    if (storage_read_blocks(lba + offset / 512, buffer, bufsize / 512))
        return -1;
    return (int32_t)bufsize;
}

int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset,
                           uint8_t *buffer, uint32_t bufsize)
{
    (void)lun;
    if (g_feeder_status.state != FS_DISARMED)
        return -1;
    if (offset % 512 || bufsize % 512)
        return -1;
    if (storage_write_blocks(lba + offset / 512, buffer, bufsize / 512))
        return -1;
    return (int32_t)bufsize;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition,
                           bool start, bool load_eject)
{
    (void)lun; (void)power_condition; (void)start;
    if (load_eject)
        storage_flush();
    return true;
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16],
                        void *buffer, uint16_t bufsize)
{
    (void)lun; (void)buffer; (void)bufsize;
    switch (scsi_cmd[0]) {
    case 0x35:                   /* SYNCHRONIZE CACHE (10) */
        storage_flush();
        return 0;
    case 0x1E:                   /* PREVENT/ALLOW MEDIUM REMOVAL */
        return 0;
    default:
        return -1;               /* unsupported -> sense set by stack */
    }
}
