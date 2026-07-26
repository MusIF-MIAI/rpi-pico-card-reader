/* feeder.h -- core1 protocol engine (see ARCHITECTURE.md sec. 4-6). */
#ifndef FEEDER_H
#define FEEDER_H

#include <stdint.h>
#include "ipc.h"

/* Called from core1_main after wire init. Never returns to flash code. */
void feeder_init(void);

/* Event entry points, called from core1 IRQ context: */
void feeder_on_re_cmd(uint8_t re);   /* PIO1 RX: RE byte latched on TU00N   */
void feeder_on_tu03(void);           /* GPIO IRQ: card-feed strobe          */
void feeder_on_txfeed(void);         /* PIO0 TX FIFO wants more nibbles     */

/* Mailbox commands from core0 (IPC_* opcodes): */
void feeder_on_ipc(uint32_t word);

/* Idle poll from core1's main loop (timeouts: FININ release, auto-rewind).
 * Returns nonzero while a timeout is pending -- caller must not __wfe. */
int feeder_poll(void);

#endif /* FEEDER_H */
