#ifndef PRACTICE_H
#define PRACTICE_H

#include "types.h"

/* The practice segment and the loader: ROM placement and RAM addresses, from the linker script. */
extern u8 practice_ROM_START[];
extern u8 practice_ROM_END[];
extern u8 practice_VRAM[];
extern u8 practice_BSS_START[];
extern u8 practice_BSS_END[];
extern u8 loader_VRAM[];
extern u8 loader_BSS_START[];
extern u8 loader_BSS_END[];

/* The practice code's state. The tests read it through the symbol practice. */
typedef struct Practice {
    /* 0x00 */ u32 magic;      /* PRACTICE_MAGIC once PracticeInit has run */
    /* 0x04 */ u32 drawFrames; /* frames on which PracticePrintTimers ran */
    /* 0x08 */ u32 textFrames; /* frames on which the practice text was drawn */
    /* 0x0C */ u32 levelStarts; /* levels started (StartNewLevel) */
    /* 0x10 */ u32 fogFrames;  /* frames drawn with the fog off */
    /* 0x14 */ u32 overlayPeak; /* most commands one frame has put in the overlay list */
    /* 0x18 */ u32 overlayFull; /* frames on which the overlay list was full and text was cut */
} Practice;

#define PRACTICE_MAGIC 0x50524143 /* "PRAC" */

extern Practice practice;

void PracticeLoader(void);
void PracticeInit(void);

#endif
