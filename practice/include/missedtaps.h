#ifndef PRACTICE_MISSEDTAPS_H
#define PRACTICE_MISSEDTAPS_H

#include "types.h"

/* Show the taps that the game missed (Inspect menu, "Show missed taps"; Run.missedTaps, off at start, not saved).
 *
 * The game reads the controllers once a game frame, so a tap that starts and ends between two reads never reaches
 * it. With the switch on, a thread of the practice code reads controller 1 about every 5 ms as well. It counts the
 * presses that began after one game read and were over before the next, and the presses that the game would see as
 * one. The input display shows the buttons of a missed tap in orange for two seconds, and counts the taps.
 *
 * The extra read is the transfer that the game's own read makes (a read of the PIF's answer, then
 * osContGetReadData). It waits for the serial interface by polling its status register, with interrupts off. No
 * message reaches the queues of the game. It runs only while the game's controller thread waits for its next frame
 * message, the last serial request was a read of the buttons and no serial interrupt is raised; otherwise it skips
 * that turn. The answer it reads is the poll that the previous read made the PIF start, up to one tick old. The
 * game's own reads can see slightly newer data, and the CPU spends some time with interrupts off, so the game's
 * timing changes. A read that does not finish in 10 ms turns the tool off, with a HUD message. */
typedef struct MissedTaps {
    /* 0x00 */ u32 reads;     /* extra reads made */
    /* 0x04 */ u32 skipped;   /* turns skipped: the controller thread was busy, or the serial interface */
    /* 0x08 */ u32 gameReads; /* game reads while on */
    /* 0x0C */ u32 total;     /* taps missed by the game */
    /* 0x10 */ u16 last;      /* the buttons of the latest missed tap, as CONT_ bits */
    /* 0x12 */ u8 active;     /* the thread reads (the switch was on at the last game read) */
    /* 0x13 */ u8 failed;     /* 1: a read did not finish in time; the tool turned itself off */
    /* 0x14 */ u32 count[14]; /* missed taps of each button, in the order of padBits */
    /* 0x4C */ u8 show[14];   /* game reads left to show each button as missed */
} MissedTaps;

extern MissedTaps missedTaps;

/* The controller thread, at each game read, with controller 1's raw buttons. Starts and stops the extra reads
 * when the switch changes. Call it only while the switch is on or the tool is active. */
void MissedTapsRead(u16 held);
/* The input display: 1 if the game missed a tap of the button (a CONT_ bit) lately. */
int MissedTapsShown(u16 bit);

#endif
