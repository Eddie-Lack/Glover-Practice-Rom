/* A host test of the savestate compressor (practice/src/lz.c): round trips of made-up data, block by block.
 * tests/lzhost.py builds and runs it. It reads no game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lz.h"

static unsigned long long seed = 12345;

static unsigned int rnd(void) {
    seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)(seed >> 33);
}

static u16 work[LZ_WORK_ENTRIES];
static u8 in[LZ_BLOCK_MAX] __attribute__((aligned(8)));
static u8 packed[LZ_BLOCK_MAX * 2];
static u8 back[LZ_BLOCK_MAX] __attribute__((aligned(8)));
static long totalIn, totalOut;

static void Fill(int kind, u32 n) {
    memset(in, 0, n);
    switch (kind) {
        case 0: /* zeros */
            break;
        case 1: /* random bytes: does not compress */
            for (u32 i = 0; i < n; i++) in[i] = (u8)rnd();
            break;
        case 2: /* an array of 24-byte records that differ in one field */
            for (u32 i = 0; i + 24 <= n; i += 24) {
                memcpy(in + i, "GLOVERRECORDXYZ01234567", 24);
                in[i + 7] = (u8)(i / 24);
                in[i + 20] = (u8)rnd();
            }
            break;
        case 3: /* runs of one word, and short runs of random words */
            for (u32 i = 0; i + 4 <= n;) {
                u32 run = 1 + rnd() % 300;
                u32 w = rnd();
                int same = rnd() & 1;
                for (u32 k = 0; k < run && i + 4 <= n; k++, i += 4) {
                    u32 v = same ? w : rnd();
                    memcpy(in + i, &v, 4);
                }
            }
            break;
        case 4: /* the same 1000 bytes again and again, at odd shifts */
            for (u32 i = 0; i < n; i++) in[i] = (u8)((i % 1000) * 7);
            break;
        case 5: /* sparse changes: a few random words in zeros */
            for (u32 k = 0; k < n / 64; k++) {
                u32 v = rnd();
                memcpy(in + (rnd() % (n / 4)) * 4, &v, 4);
            }
            break;
        default: /* bytes from a small alphabet */
            for (u32 i = 0; i < n; i++) in[i] = (u8)(rnd() % 5);
            break;
    }
}

/* The decoder meets bad streams: it must give NULL or an end, and write only inside the block. The block is a
 * malloc of exactly its size, which the address sanitizer watches. */
static int Crafted(const char *what, u32 n, const u8 *stream, u32 length) {
    u8 *out = malloc(n);
    u8 padded[64];
    int ok;

    memset(padded, 0, sizeof(padded));
    memcpy(padded, stream, length);
    ok = LzDecompress(out, n, padded) == NULL;
    free(out);
    if (!ok) {
        printf("FAIL decoder accepted a stream with %s\n", what);
    }
    return ok;
}

static int BadStreams(void) {
    /* token: literal count (2 bits), offset kind (2 bits), match length minus 4 (4 bits) */
    static const u8 litPastEnd[] = { 0xC0, 250 };                              /* 253 literals in a block of 8 */
    static const u8 lenPastEnd[] = { 0xC0 | 0x10 | 15, 1, 'a', 'b', 'c', 'd', 1, 0 }; /* 19 bytes of match, 4 left */
    static const u8 offsetBefore[] = { 0x40 | 0x10, 'a', 2 };                  /* 8 bytes back, 1 written */
    static const u8 offsetZero[] = { 0x40 | 0x10, 'a', 0 };
    static const u8 firstRep[] = { 0x40, 'a' };                                /* a match at the last offset, none before */

    if (!Crafted("literals past the block's end", 8, litPastEnd, sizeof(litPastEnd)) ||
        !Crafted("a match past the block's end", 16, lenPastEnd, sizeof(lenPastEnd)) ||
        !Crafted("an offset before the block's start", 16, offsetBefore, sizeof(offsetBefore)) ||
        !Crafted("offset 0", 16, offsetZero, sizeof(offsetZero)) ||
        !Crafted("a match at the last offset in the first match", 16, firstRep, sizeof(firstRep))) {
        return 0;
    }
    /* A good stream, cut short or changed at random. The input has room for what the decoder may read. */
    for (int round = 0; round < 3000; round++) {
        u32 n = 4 + 4 * (rnd() % 600);
        u8 *end;
        u32 size;
        u8 *stream;
        u8 *out;
        u32 cut;

        Fill(round % 7, n);
        end = LzCompress(packed, packed + LZ_BLOCK_MAX * 2, in, n, work);
        size = (u32)(end - packed);
        stream = calloc(size + 3 * n + 64, 1);
        memcpy(stream, packed, size);
        if (round & 1) {
            /* cut: everything from CUT on is zero */
            cut = rnd() % size;
            memset(stream + cut, 0, size - cut);
        } else {
            /* damaged: a few bytes changed */
            for (int k = 0; k < 1 + (int)(rnd() % 4); k++) {
                stream[rnd() % size] = (u8)rnd();
            }
        }
        out = malloc(n);
        (void)LzDecompress(out, n, stream);
        free(out);
        free(stream);
    }
    return 1;
}

/* The hash tells blocks apart that have the same sum. */
static int Hashes(void) {
    for (int round = 0; round < 2000; round++) {
        u32 n = 3 + rnd() % 200;
        u32 a[256], b[256];
        u32 ha[2], hb[2];
        u32 i = rnd() % (n - 2);

        for (u32 k = 0; k < n; k++) {
            a[k] = b[k] = rnd();
        }
        /* up by one, down by two, up by one: the sum of the words and the sum of their running sums stay */
        b[i] += 1;
        b[i + 1] -= 2;
        b[i + 2] += 1;
        LzHash(a, n, ha);
        LzHash(b, n, hb);
        if (ha[0] != hb[0] || (ha[1] == hb[1])) {
            printf("FAIL hash: the second part must differ where the sum is equal (round %d)\n", round);
            return 0;
        }
        /* two words swapped */
        memcpy(b, a, n * 4);
        if (a[i] != a[i + 1]) {
            b[i] = a[i + 1];
            b[i + 1] = a[i];
            LzHash(b, n, hb);
            if (ha[0] != hb[0] || ha[1] == hb[1]) {
                printf("FAIL hash: swapped words (round %d)\n", round);
                return 0;
            }
        }
        /* a word moved to another place, with another word moved the other way: the sum stays */
        memcpy(b, a, n * 4);
        b[i] += 0x10000;
        b[i + 2] -= 0x10000;
        LzHash(b, n, hb);
        if (ha[0] != hb[0] || ha[1] == hb[1]) {
            printf("FAIL hash: a transfer between words (round %d)\n", round);
            return 0;
        }
    }
    return 1;
}

int main(void) {
    static const u32 sizes[] = { 4, 8, 12, 16, 100, 256, 1000, 4096, 8196, LZ_BLOCK_MAX };

    for (int kind = 0; kind < 7; kind++) {
        for (u32 s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
            for (int round = 0; round < 20; round++) {
                u32 n = sizes[s];
                u8 *end;

                Fill(kind, n);
                end = LzCompress(packed, packed + LZ_BLOCK_MAX * 2, in, n, work);
                if (end == NULL) {
                    printf("FAIL kind %d size %u: no room in a double-size buffer\n", kind, n);
                    return 1;
                }
                memset(back, 0xAA, sizeof(back));
                if (LzDecompress(back, n, packed) != end || memcmp(back, in, n) != 0) {
                    printf("FAIL kind %d size %u round %d: the round trip differs\n", kind, n, round);
                    return 1;
                }
                /* With a limit under the output's size, the compressor gives NULL and writes nothing beyond the limit (the
                 * address sanitizer watches the exact-size buffer). */
                {
                    u32 size = (u32)(end - packed);
                    u8 *tight = malloc(size - 1);

                    if (LzCompress(tight, tight + size - 1, in, n, work) != NULL) {
                        printf("FAIL kind %d size %u: output under the size it needs\n", kind, n);
                        return 1;
                    }
                    free(tight);
                    /* A limit exactly at the output's end is enough. */
                    tight = malloc(size);
                    if (LzCompress(tight, tight + size, in, n, work) != tight + size ||
                        memcmp(tight, packed, size) != 0) {
                        printf("FAIL kind %d size %u: no output with a limit at its exact size\n", kind, n);
                        return 1;
                    }
                    free(tight);
                }
                if (s == 9) {
                    totalIn += n;
                    totalOut += end - packed;
                }
            }
        }
    }
    if (!BadStreams() || !Hashes()) {
        return 1;
    }
    printf("lzhost: round trips, exact limits, bad streams and hashes ok (block-size data: %ld bytes in, %ld out)\n", totalIn, totalOut);
    return 0;
}
