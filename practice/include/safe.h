#ifndef PRACTICE_SAFE_H
#define PRACTICE_SAFE_H

#include "types.h"

/* Checks for data that the practice code reads from the game's memory while the game may change it: lists can
 * hold freed or half-written entries. */

/* A pointer that can be followed: word-aligned and inside RDRAM (8 MB with the Expansion Pak). */
static inline int SafePointer(const void *p) {
    u32 a = (u32)p;
    return (a & 3) == 0 && a >= 0x80000000 && a < 0x80800000;
}

/* The game's threads run with the FPU's invalid-operation trap on, and the VR4300 traps on NaN and denormal
 * operands. A float read from memory that the practice code does not own is checked as bits first: 1 if it is
 * zero or a normal number below 2^20 in size. */
static inline int SafeFloat(const f32 *p) {
    u32 bits = *(const volatile u32 *)p;
    u32 e = (bits >> 23) & 0xFF;

    if (e == 0) {
        return (bits & 0x7FFFFF) == 0;
    }
    return e < 127 + 20;
}

static inline int SafeFloats(const f32 *p, int n) {
    for (int i = 0; i < n; i++) {
        if (!SafeFloat(&p[i])) {
            return 0;
        }
    }
    return 1;
}

/* Reads a float that is used in sums and products. Returns 0 for NaN, infinity and numbers of 2^20 or more.
 * A number smaller than 2^-30 reads as 0, because a product of such numbers could be denormal. The collision
 * view reads the game floats of its own maths this way. */
static inline int ReadFloat(const f32 *p, f32 *out) {
    union {
        u32 bits;
        f32 f;
    } v;
    u32 e;

    v.bits = *(const volatile u32 *)p;
    e = (v.bits >> 23) & 0xFF;
    if (e >= 127 + 20) {
        return 0;
    }
    if (e < 127 - 30) {
        v.bits = 0;
    }
    *out = v.f;
    return 1;
}

#endif
