#ifndef PRACTICE_MEMVIEW_H
#define PRACTICE_MEMVIEW_H

#include "rdp.h"
#include "types.h"

/* The watch list: words that the HUD shows. 0: an empty place. Filled from the memory view. */
#define WATCH_COUNT 4
extern u32 watches[WATCH_COUNT];

void WatchDraw(Gfx **dl, int x, int y);
/* Locks the memory view on WORD, a word of the practice data, ready to change its lowest digit. Only that word shows
 * and changes, and a change is not a poke. Then MenuShowView(&memoryView) shows it. */
void MemViewEdit(u32 *word);
/* The address at the memory view's cursor. */
u32 MemViewAddress(void);
extern const struct View memoryView;
/* For the views: the C buttons that were pressed and released with no other C button held between (CONT_ bits of
 * the C buttons in HELD, the buttons held now). Call it once with each input. The menu opens and closes with all
 * four C buttons, and the buttons of that chord do nothing in a view. */
u16 ViewCTaps(u16 held);
/* The same view, opened on its second page: the list of pokes (corrupt.h). */
extern const struct View pokeView;

#endif
