/* The savestate compressor (lz.h). It runs with the game's threads stopped, so it is written for speed. */
#pragma GCC optimize("O2")
#include "lz.h"

typedef struct Unaligned {
    u32 v;
} __attribute__((packed)) Unaligned;

static inline u32 Load(const u8 *p) {
    return ((const Unaligned *)p)->v;
}

static inline void Store(u8 *p, u32 v) {
    ((Unaligned *)p)->v = v;
}

#define MIX(w)              \
    do {                    \
        u32 x = mix ^ (w);  \
        mix = (x << 7 | x >> 25) + (w); \
    } while (0)

void LzHash(const u32 *p, u32 words, u32 *out) {
    u32 sum = 0;
    u32 mix = 0;

    for (; words >= 4; words -= 4, p += 4) {
        u32 a = p[0], b = p[1], c = p[2], d = p[3];

        sum += a + b + c + d;
        MIX(a);
        MIX(b);
        MIX(c);
        MIX(d);
    }
    for (; words > 0; words--, p++) {
        sum += *p;
        MIX(*p);
    }
    out[0] = sum;
    out[1] = mix;
}

/* The length of the match of Q against P, which agree in their first word. MAX is the room before the block's end. */
static u32 Extend(const u8 *p, const u8 *q, u32 max) {
    u32 m = 4;

    while (m + 4 <= max && Load(p + m) == Load(q + m)) {
        m += 4;
    }
    while (m < max && p[m] == q[m]) {
        m++;
    }
    return m;
}

static u8 *PutExtension(u8 *op, u32 v) {
    while (v >= 255) {
        *op++ = 255;
        v -= 255;
    }
    *op++ = (u8)v;
    return op;
}

/* One sequence: LIT literals from LITERALS, then a match of LEN bytes at OFFSET bytes back (LEN 0: no match). */
static u8 *Emit(u8 *op, const u8 *literals, u32 lit, u32 len, u32 offset, u32 rep) {
    u32 kind = 0;
    u32 words = offset >> 2;
    u8 *token = op++;

    if (lit >= 3) {
        op = PutExtension(op, lit - 3);
    }
    for (u32 i = 0; i < lit; i++) {
        *op++ = literals[i];
    }
    if (len != 0) {
        if (offset != rep) {
            kind = words < 0x100 ? 1 : 2;
            if (kind == 2) {
                *op++ = (u8)(words >> 8);
            }
            *op++ = (u8)words;
        }
        if (len - 4 >= 15) {
            op = PutExtension(op, len - 4 - 15);
        }
    }
    *token = (u8)((lit < 3 ? lit : 3) << 6 | kind << 4 | (len == 0 ? 0 : len - 4 < 15 ? len - 4 : 15));
    return op;
}

/* The bytes that a sequence takes: the same fields as Emit writes. */
static u32 ExtensionSize(u32 v) {
    return v / 255 + 1;
}

static u32 EmitSize(u32 lit, u32 len, u32 offset, u32 rep) {
    u32 size = 1 + lit;

    if (lit >= 3) {
        size += ExtensionSize(lit - 3);
    }
    if (len != 0) {
        if (offset != rep) {
            size += (offset >> 2) < 0x100 ? 1 : 2;
        }
        if (len - 4 >= 15) {
            size += ExtensionSize(len - 4 - 15);
        }
    }
    return size;
}

u8 *LzCompress(u8 *op, const u8 *limit, const u8 *in, u32 n, u16 *work) {
    u32 i = 0;
    u32 lit = 0; /* the first byte not yet coded */
    u32 rep = 0; /* the offset of the last match, in bytes */
    u32 fails = 0;

    for (u32 k = 0; k < LZ_WORK_ENTRIES; k++) {
        work[k] = 0;
    }
    while (i < n) {
        u32 w = Load(in + i);
        u16 *bucket = &work[((w * 0x9E3779B1u) >> (32 - LZ_BUCKET_BITS)) * LZ_WAYS];
        u32 bestLen = 0;
        u32 bestOff = 0;

        if (rep != 0 && i >= rep && Load(in + i - rep) == w) {
            bestLen = Extend(in + i - rep, in + i, n - i);
            bestOff = rep;
        }
        for (int way = 0; way < LZ_WAYS; way++) {
            u32 entry = bucket[way];
            u32 p;
            u32 len;

            if (entry == 0) {
                break;
            }
            p = (entry - 1) << 2;
            if (Load(in + p) != w) {
                continue;
            }
            /* A match that is longer than the best one so far has the byte after it equal too. */
            if (bestLen != 0 && (bestLen >= n - i || in[p + bestLen] != in[i + bestLen])) {
                continue;
            }
            len = Extend(in + p, in + i, n - i);
            /* A match at the last offset has no offset bytes. */
            if (len > bestLen + (bestOff == rep && bestOff != 0 ? 2 : 0)) {
                bestLen = len;
                bestOff = i - p;
            }
        }
        for (int way = LZ_WAYS - 1; way > 0; way--) {
            bucket[way] = bucket[way - 1];
        }
        bucket[0] = (u16)((i >> 2) + 1);
        if (bestLen < 4) {
            fails++;
            /* Data that does not match is skipped faster. */
            i += 4 + (fails >> 5 < 7 ? fails >> 5 : 7) * 4;
            continue;
        }
        fails = 0;
        {
            u32 start = i;
            u32 p = i - bestOff;
            u32 end;

            /* The match may begin before the word that found it. */
            while (start > lit && p > 0 && in[start - 1] == in[p - 1]) {
                start--;
                p--;
                bestLen++;
            }
            end = start + bestLen;
            if (op + EmitSize(start - lit, bestLen, bestOff, rep) > limit) {
                return NULL;
            }
            op = Emit(op, in + lit, start - lit, bestLen, bestOff, rep);
            rep = bestOff;
            /* The positions inside the match stay out of the table: adding them cost a fifth of the time and
             * changed the size of a state by 0.3 percent. */
            lit = end;
            i = (end + 3) & ~3u;
        }
    }
    if (lit < n) {
        if (op + EmitSize(n - lit, 0, 0, rep) > limit) {
            return NULL;
        }
        op = Emit(op, in + lit, n - lit, 0, 0, rep);
    }
    return op;
}

const u8 *LzDecompress(u8 *out, u32 n, const u8 *in) {
    const u8 *begin = out;
    u8 *end = out + n;
    u32 rep = 4;

    for (;;) {
        u32 token = *in++;
        u32 lit = token >> 6;
        u32 len;
        u32 offset;

        if (lit == 3) {
            u32 e;

            do {
                e = *in++;
                lit += e;
            } while (e == 255);
        }
        if (lit > (u32)(end - out)) {
            return NULL;
        }
        while (lit >= 8) {
            u32 a = Load(in);
            u32 b = Load(in + 4);

            Store(out, a);
            Store(out + 4, b);
            out += 8;
            in += 8;
            lit -= 8;
        }
        if (lit >= 4) {
            Store(out, Load(in));
            out += 4;
            in += 4;
            lit -= 4;
        }
        while (lit-- > 0) {
            *out++ = *in++;
        }
        if (out >= end) {
            return in;
        }
        switch ((token >> 4) & 3) {
            case 0:
                offset = rep;
                break;
            case 1:
                offset = (u32)in[0] << 2;
                in += 1;
                break;
            default:
                offset = ((u32)in[0] << 8 | in[1]) << 2;
                in += 2;
                break;
        }
        rep = offset;
        len = (token & 15) + 4;
        if ((token & 15) == 15) {
            u32 e;

            do {
                e = *in++;
                len += e;
            } while (e == 255);
        }
        /* A match reads only what the block has written, and ends inside the block. */
        if (offset == 0 || offset > (u32)(out - begin) || len > (u32)(end - out)) {
            return NULL;
        }
        {
            const u8 *src = out - offset;

            /* The offset is a multiple of 4, so SRC and OUT have the same alignment, and a word copy never reads
             * what it has just begun to write. */
            if (offset == 4) {
                u32 v = Load(src);

                while (len >= 4) {
                    Store(out, v);
                    out += 4;
                    len -= 4;
                }
                src = out - 4;
            } else if (((unsigned long)out & 3) == 0) {
                /* The four words are read before they are written, so the offset must be at least 16. */
                while (offset >= 16 && len >= 16) {
                    u32 a = *(const u32 *)src;
                    u32 b = *(const u32 *)(src + 4);
                    u32 c = *(const u32 *)(src + 8);
                    u32 d = *(const u32 *)(src + 12);

                    *(u32 *)out = a;
                    *(u32 *)(out + 4) = b;
                    *(u32 *)(out + 8) = c;
                    *(u32 *)(out + 12) = d;
                    out += 16;
                    src += 16;
                    len -= 16;
                }
                while (len >= 4) {
                    *(u32 *)out = *(const u32 *)src;
                    out += 4;
                    src += 4;
                    len -= 4;
                }
            } else {
                while (len >= 4) {
                    Store(out, Load(src));
                    out += 4;
                    src += 4;
                    len -= 4;
                }
            }
            if (len != 0) {
                /* offset 4 leaves SRC at the repeated word, which is also where the bytes come from */
                while (len-- > 0) {
                    *out++ = *src++;
                }
            }
        }
        if (out >= end) {
            return in;
        }
    }
}
