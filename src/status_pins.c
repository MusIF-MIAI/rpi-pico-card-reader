/*
 * status_pins.c -- the slow status outputs owned by core1:
 * FIDEN (end-of-sequence), POM01 (binary mode), LUREN (error).
 *
 * LU08N/LUPOR are NOT here -- they are the presenter's side-set pair.
 * LESAB/LUSEN/LENON are hardware straps (docs/PINOUT.md sec. 3).
 */
#include "pico/stdlib.h"
#include "ge_proto.h"

static const uint pins[] = { GP_FIDEN, GP_POM01, GP_LUREN };

void status_pins_init(void)
{
    for (unsigned i = 0; i < sizeof(pins) / sizeof(pins[0]); i++) {
        gpio_init(pins[i]);
        gpio_set_outover(pins[i], GPIO_OVERRIDE_INVERT); /* active-low wire */
        gpio_put(pins[i], 0);                            /* inactive */
        gpio_set_dir(pins[i], GPIO_OUT);
    }
}

void status_pin_set(uint pin, bool active)
{
    gpio_put(pin, active);
}
