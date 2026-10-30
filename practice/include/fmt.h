#ifndef PRACTICE_FMT_H
#define PRACTICE_FMT_H

#include <stdarg.h>
#include "types.h"

/* Writes FMT with its arguments into BUF, at most SIZE bytes with the final zero, and returns the length. It knows
 * %d, %u, %x, %X, %s, %c, %% and %f, with the flags -, + and 0, a width and a precision (%5.2f, %08X, %-12s). */
int Format(char *buf, int size, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
int FormatV(char *buf, int size, const char *fmt, va_list args);

#endif
