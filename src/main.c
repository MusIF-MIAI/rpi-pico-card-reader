/*
 * main.c -- core0 entry: config, USB composite (CDC console + MSC deck
 * drive), deck management. core1 gets the wire.
 */
#include "pico/stdlib.h"
#include "pico/multicore.h"

#include "config.h"
#include "console.h"
#include "ipc.h"
#include "storage.h"

void core1_entry(void);
void usb_composite_init(void);   /* usb_composite.c                        */
void usb_composite_task(void);
void monitor_drain(void);        /* monitor.c (when trace enabled)         */

int main(void)
{
    cfg_load();
    storage_init();
    usb_composite_init();

    ipc_init();
    multicore_launch_core1(core1_entry);

    while (true) {
        usb_composite_task();    /* tud_task() + CDC/MSC service           */
        console_poll();          /* command shell on CDC                   */
        monitor_drain();         /* event ring -> trace output             */
        storage_poll();          /* lazy flush of the MSC sector cache     */
    }
}
