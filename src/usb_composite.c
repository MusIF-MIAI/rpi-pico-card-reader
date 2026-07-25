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

int storage_read_blocks(uint32_t lba, void *buf, uint32_t count);

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
    /* TODO(step 2): true while g_feeder_status.state == FS_DISARMED, once
     * storage_write_blocks runs under pico_flash safe-execute. */
    return false;
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
    (void)lun; (void)lba; (void)offset; (void)buffer; (void)bufsize;
    return -1;   /* write-protected (see tud_msc_is_writable_cb) */
}

int32_t tud_msc_scsi_cb(uint8_t lun, const uint8_t scsi_cmd[16],
                        void *buffer, uint16_t bufsize)
{
    (void)lun; (void)scsi_cmd; (void)buffer; (void)bufsize;
    return -1;   /* unsupported commands -> sense set by stack */
}
