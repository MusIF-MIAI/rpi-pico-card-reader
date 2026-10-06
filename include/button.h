/* button.h -- GP14 "arm last" button (core0, polled). */
#ifndef BUTTON_H
#define BUTTON_H

#include <stdbool.h>

void button_init(void);

/* Poll from the core0 main loop; true once per physical press. */
bool button_poll(void);

#endif /* BUTTON_H */
