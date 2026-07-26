/* wire_tx.h -- PIO0 presenter control (core1 only). */
#ifndef WIRE_TX_H
#define WIRE_TX_H

#include <stdbool.h>
#include <stdint.h>

void wire_tx_init(void);       /* core1: PIO program + TXNFULL IRQ (off)    */
void wire_tx_arm(void);        /* restart SM (LUPOB is separate, see below) */
void wire_tx_disarm(void);     /* stop SM, drain, all pins inactive         */
void wire_tx_set_ready(bool ready); /* LUPOB per-card handshake (feeder)    */
void wire_tx_feed_irq(bool on);/* TXNFULL source (level: only while feeding)*/
bool wire_tx_full(void);
unsigned wire_tx_fifo_level(void);  /* diagnostic for console status */
uint32_t wire_tx_feed_irqs(void);   /* TXNFULL handler entries       */
void wire_tx_push(uint8_t nibble, bool fini, uint16_t w_ticks,
                  uint16_t g_ticks);          /* callers check wire_tx_full */
void wire_tx_release_finin(void); /* deassert standing FININ between cards  */

#endif /* WIRE_TX_H */
