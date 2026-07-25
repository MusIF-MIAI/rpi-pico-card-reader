/* storage.h -- 512-byte block access to the FAT flash region. */
#ifndef STORAGE_H
#define STORAGE_H

#include <stdint.h>

void storage_init(void);

/* Reads come straight from XIP; always allowed. */
int storage_read_blocks(uint32_t lba, void *buf, uint32_t count);

/* Writes go through a 4 KB sector cache and pico_flash safe-execute.
 * Callers must only allow them while the feeder is DISARMED (the MSC
 * write-protect callback enforces this for USB). */
int storage_write_blocks(uint32_t lba, const void *buf, uint32_t count);

/* Flush the dirty sector cache (SCSI SYNCHRONIZE CACHE, eject, idle). */
int storage_flush(void);

/* Called from the core0 main loop: flush if the cache has been dirty for
 * a while with no new writes. */
void storage_poll(void);

#endif /* STORAGE_H */
