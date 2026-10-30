#ifndef PRACTICE_SAVEEDIT_H
#define PRACTICE_SAVEEDIT_H

#include "types.h"

/* The save file editor (Settings, Save file). It edits a copy of the game's save (saveEditSlots and saveEditBests:
 * six files and thirty best times, as in game.h). "Write" merges the edits into the game's copy (saveSlot[] and
 * bestTimes[]) and posts the game's own EEPROM requests. "Reload" takes the cartridge's data over when it passes
 * the game's CRC check. The tests read these fields at their offsets (tests/layout.py). */
typedef struct SaveEdit {
    /* 0x00 */ u8 slot;       /* the file being edited, 0 to 5 */
    /* 0x01 */ u8 request;    /* for the controller thread: 1 write, 2 reload */
    /* 0x02 */ u8 status;     /* the last result: 0 none, 1 written, 2 reloaded, 3 no EEPROM, 4 data refused (CRC) */
    /* 0x03 */ u8 bestsEdited; /* 1: a best time was edited and is not written yet */
    /* 0x04 */ u8 page;       /* 0 the file, 1 the levels */
    /* 0x05 */ u8 cursor;     /* the row on this page */
    /* 0x06 */ u8 column;     /* on the levels page: 0 done, 1 all garibs, 2 best time */
    /* 0x07 */ u8 confirm;    /* the next A asks for: 1 a write, 2 a reload */
} SaveEdit;

extern SaveEdit saveEdit;
extern const struct View saveEditView;
/* Set by PracticeProcessInput (practice.c) while it holds the difficulty of the file in play at Easy. A write or a
 * reload waits while it is set, as the difficulty in RAM is not the real one then. */
extern volatile u8 difficultySwapped;

/* Called by the controller thread after its controller read. When the game has no EEPROM request waiting, a write
 * merges the edits and posts the game's requests, and a reload reads the cartridge and checks its CRCs. */
void SaveEditService(void);

#endif
