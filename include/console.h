/* console.h -- CDC-ACM shell (core0). */
#ifndef CONSOLE_H
#define CONSOLE_H

void console_poll(void);

/* printf to the CDC console (chunked; drops output if no host attached). */
void con_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif /* CONSOLE_H */
