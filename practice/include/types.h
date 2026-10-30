#ifndef PRACTICE_TYPES_H
#define PRACTICE_TYPES_H

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed int s32;
typedef unsigned int u32;
typedef signed long long s64;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;

#define NULL ((void *)0)
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))

#endif
