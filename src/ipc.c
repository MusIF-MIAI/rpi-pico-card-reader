/*
 * ipc.c -- core0 -> core1 op queue.
 *
 * Deliberately NOT the SIO multicore FIFO: core1 runs
 * multicore_lockout_victim_init() so core0's flash_safe_execute (MSC/config
 * writes) can pause it, and the SDK's lockout handler claims the FIFO IRQ --
 * it pops and discards every word that is not the lockout magic, so IPC
 * words pushed there vanish. A pico_util queue (spinlock-based) instead;
 * ipc_send() ends with __sev() so core1's __wfe() idle loop wakes up.
 */
#include "pico/util/queue.h"
#include "hardware/sync.h"

#include "ipc.h"

#define IPC_QUEUE_DEPTH 16

static queue_t ipc_q;

void ipc_init(void)
{
    queue_init(&ipc_q, sizeof(uint32_t), IPC_QUEUE_DEPTH);
}

void ipc_send(uint8_t op, uint8_t arg, uint16_t val)
{
    uint32_t w = IPC_WORD(op, arg, val);
    queue_add_blocking(&ipc_q, &w);
    __sev();
}

bool ipc_try_recv(uint32_t *w)
{
    return queue_try_remove(&ipc_q, w);
}
