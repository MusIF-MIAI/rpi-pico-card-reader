/*
 * button.c -- GP14 "arm last / rewind" button (core0).
 *
 * Button wired GP14 -> GND, internal pull-up: idle high, pressed low.
 * button_poll() runs in the core0 main loop; a 20 ms debounce window plus
 * release tracking make one physical press yield exactly one press event.
 */
#include "pico/stdlib.h"
#include "hardware/gpio.h"

#include "button.h"

#define BUTTON_PIN   14u
#define DEBOUNCE_US  20000u

static uint8_t s_pressed;    /* 0 = released, 1 = pressed (debounced) */

void button_init(void)
{
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    s_pressed = 0;
}

bool button_poll(void)
{
    if (s_pressed == 0) {
        if (!gpio_get(BUTTON_PIN)) {
            uint64_t t0 = time_us_64();
            while (time_us_64() - t0 < DEBOUNCE_US)
                ;
            if (!gpio_get(BUTTON_PIN)) {
                s_pressed = 1;
                return true;
            }
        }
    }
    else if (gpio_get(BUTTON_PIN))
        s_pressed = 0;
    return false;
}
