#ifndef PRACTICE_FEARSEED_H
#define PRACTICE_FEARSEED_H

#include "types.h"

/* Force the Fear boss seed (Inspect): at the first burst of the ball in each start of the Fear boss, the RNG seed
 * becomes FEARSEED_VALUE, a seed with which the burst ball presses the ball switch (glover burst, weemitt's brute
 * forcer). The seed is written by the draw thread at the start of the first frame drawn after the burst, before
 * that frame's lightning calls Random. 1: on. */
#define FEARSEED_VALUE 0x8BA4FEEE
extern u8 fearSeedOn;

/* The draw thread, at the start of each frame's display list (PracticeSendLastFrame). */
void FearSeedDraw(void);

#endif
