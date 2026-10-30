#ifndef PRACTICE_MEMVIEW_H
#define PRACTICE_MEMVIEW_H

#include "rdp.h"
#include "types.h"

/* The watch list: words that the HUD shows. 0: an empty place. Filled from the memory view. */
#define WATCH_COUNT 4
extern u32 watches[WATCH_COUNT];

void WatchDraw(Gfx **dl, int x, int y);
/* Points the memory view at WORD, ready to change its lowest digit. Then MenuShowView(&memoryView) shows it. */
void MemViewEdit(u32 *word);
/* The address at the memory view's cursor. */
u32 MemViewAddress(void);
extern const struct View memoryView;
/* The same view, opened on its second page: the list of pokes (corrupt.h). */
extern const struct View pokeView;

#endif
