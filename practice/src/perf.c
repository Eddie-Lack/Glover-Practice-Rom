/* The counters of the practice code's time (perf.h). */
#include "perf.h"

Perf perf;

extern u32 __osViIntrCount;

void PerfAdd(int stage, u32 ticks) {
    PerfStage *s = &perf.stage[stage];

    s->n++;
    s->sum += ticks;
    if (ticks > s->max) {
        s->max = ticks;
    }
    s->last = ticks;
}

/* A new game frame starts at its first ProcessInput. Loads, pauses and the menu make long gaps; they show in the
 * last bucket. */
void PerfGameFrame(void) {
    u32 now = __osViIntrCount;
    u32 gap = now - perf.lastVi;
    u32 count = osGetCount();

    if (perf.lastVi != 0) {
        perf.interval[gap < PERF_INTERVALS ? gap : PERF_INTERVALS - 1]++;
        if (gap < PERF_INTERVALS - 1) {
            perf.gapTicks += count - perf.lastCount;
            perf.gapFrames++;
        }
    }
    perf.lastVi = now;
    perf.lastCount = count;
}
