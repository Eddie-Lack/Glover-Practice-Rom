/* A small printf for the HUD and the menus. */
#include "fmt.h"

typedef struct Out {
    char *buf;
    int size;
    int len;
} Out;

static void Put(Out *o, char c) {
    if (o->len < o->size - 1) {
        o->buf[o->len] = c;
    }
    o->len++;
}

/* Writes the digits in DIGITS (most significant first) after the sign SIGN (0 for none), padded to WIDTH. */
static void PutField(Out *o, const char *digits, int n, char sign, int width, int left, char pad) {
    int total = n + (sign != 0);

    if (sign && pad == '0') {
        Put(o, sign);
    }
    for (; !left && total < width; total++) {
        Put(o, pad);
    }
    if (sign && pad != '0') {
        Put(o, sign);
    }
    for (int i = 0; i < n; i++) {
        Put(o, digits[i]);
    }
    for (; left && total < width; total++) {
        Put(o, ' ');
    }
}

static int Digits(char *out, u32 value, u32 base, int upper) {
    char tmp[12];
    int n = 0;

    do {
        u32 d = value % base;
        tmp[n++] = d < 10 ? '0' + d : (upper ? 'A' : 'a') + d - 10;
        value /= base;
    } while (value != 0);
    for (int i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

int FormatV(char *buf, int size, const char *fmt, va_list args) {
    Out o = { buf, size, 0 };

    for (; *fmt != '\0'; fmt++) {
        char digits[24];
        int left = 0, plus = 0, width = 0, precision = -1, n = 0, negative = 0;
        char pad = ' ';

        if (*fmt != '%') {
            Put(&o, *fmt);
            continue;
        }
        fmt++;
        for (; *fmt == '-' || *fmt == '0' || *fmt == '+'; fmt++) {
            if (*fmt == '-') {
                left = 1;
            } else if (*fmt == '+') {
                plus = 1;
            } else {
                pad = '0';
            }
        }
        for (; *fmt >= '0' && *fmt <= '9'; fmt++) {
            width = width * 10 + *fmt - '0';
        }
        if (*fmt == '.') {
            precision = 0;
            for (fmt++; *fmt >= '0' && *fmt <= '9'; fmt++) {
                precision = precision * 10 + *fmt - '0';
            }
        }
        if (left) {
            pad = ' ';
        }
        switch (*fmt) {
            case 'd': {
                s32 v = va_arg(args, s32);
                negative = v < 0;
                n = Digits(digits, negative ? -(u32)v : (u32)v, 10, 0);
                break;
            }
            case 'u':
                n = Digits(digits, va_arg(args, u32), 10, 0);
                break;
            case 'x':
            case 'X':
                n = Digits(digits, va_arg(args, u32), 16, *fmt == 'X');
                break;
            case 'c':
                digits[n++] = (char)va_arg(args, int);
                break;
            case 's': {
                const char *s = va_arg(args, const char *);
                int len = 0;
                while (s[len] != '\0' && (precision < 0 || len < precision)) {
                    len++;
                }
                for (int i = len; !left && i < width; i++) {
                    Put(&o, ' ');
                }
                for (int i = 0; i < len; i++) {
                    Put(&o, s[i]);
                }
                for (int i = len; left && i < width; i++) {
                    Put(&o, ' ');
                }
                continue;
            }
            case 'f': {
                double v = va_arg(args, double);
                u32 scale = 1;
                if (precision < 0) {
                    precision = 2;
                }
                for (int i = 0; i < precision; i++) {
                    scale *= 10;
                }
                negative = v < 0;
                if (negative) {
                    v = -v;
                }
                /* Values of 2^32 / scale or more do not fit in the fixed-point word. The conversion of such a value
                 * would trap, so they print as the largest that fits. */
                double fixedValue = v * scale + 0.5;
                u32 fixed = fixedValue < 4294967295.0 ? (u32)fixedValue : 4294967295u;
                n = Digits(digits, fixed / scale, 10, 0);
                if (precision > 0) {
                    char frac[12];
                    int m = Digits(frac, fixed % scale, 10, 0);
                    digits[n++] = '.';
                    for (int i = m; i < precision; i++) {
                        digits[n++] = '0';
                    }
                    for (int i = 0; i < m; i++) {
                        digits[n++] = frac[i];
                    }
                }
                negative = negative && fixed != 0;
                break;
            }
            case '%':
                digits[n++] = '%';
                break;
            default:
                return o.len;
        }
        PutField(&o, digits, n, negative ? '-' : (plus ? '+' : 0), width, left, pad);
    }
    if (o.size > 0) {
        o.buf[o.len < o.size ? o.len : o.size - 1] = '\0';
    }
    return o.len;
}

int Format(char *buf, int size, const char *fmt, ...) {
    va_list args;
    int len;

    va_start(args, fmt);
    len = FormatV(buf, size, fmt, args);
    va_end(args);
    return len;
}
