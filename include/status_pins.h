/* status_pins.h -- FIDEN/POM01/LUREN slow outputs (core1). */
#ifndef STATUS_PINS_H
#define STATUS_PINS_H

#include <stdbool.h>

void status_pins_init(void);
void status_pin_set(unsigned pin, bool active);

#endif /* STATUS_PINS_H */
