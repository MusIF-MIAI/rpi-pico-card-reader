/* console.h -- CDC-ACM shell (core0). */
#ifndef CONSOLE_H
#define CONSOLE_H

void console_poll(void);

/* Arm a deck by name (may carry "@hexbase" for .bin); 0 on success.
 * Shared by the console command and the GP14 button. */
int  arm_named(const char *name, int raw);

/* GP14 button handler: re-arm the last armed deck. */
void console_arm_last(void);

/* printf to the CDC console (chunked; drops output if no host attached). */
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* CONSOLE_H */
