#ifndef PRACTICE_CARD_H
#define PRACTICE_CARD_H

#include "types.h"

/* Savestates on the SD card of a flash cart (docs/build.md, "Savestates on the SD card").
 *
 * The card holds up to CARD_SLOTS files, STATE001.GST to STATE100.GST, in the folder GLOVERPR of the card's first
 * FAT16 or FAT32 volume. A file is one state with no dependence on a base, in the format that docs/build.md
 * describes. The card is read when the player asks (Check card, Load, Save, Delete) and written only by Save and
 * Delete. Nothing touches the cart at boot or while the card is not in use.
 *
 * The Savestate menu sets cardRequest. The game thread carries it out at the start of a game frame, or at once
 * while the menu holds the game (CardGameFrame), because a save and a load stop the game's other threads. */
#define CARD_SLOTS 100

enum CardRequest {
    CARD_REQ_NONE,
    CARD_REQ_CHECK,  /* look at the card and list the files */
    CARD_REQ_SAVE,   /* the chosen slot of the pool into card file cardSlot */
    CARD_REQ_LOAD,   /* card file cardSlot into the game */
    CARD_REQ_DELETE, /* delete card file cardSlot */
    CARD_REQ_SAVE_REC,   /* the input recording and its state into the file RECORD.GRC */
    CARD_REQ_LOAD_REC,   /* that file into the game, and the recording into its slot */
    CARD_REQ_SAVE_BESTS, /* the practice best times into BESTS.GRB */
    CARD_REQ_LOAD_BESTS, /* that file into the best times */
};

enum CardStatus {
    CARD_UNCHECKED,
    CARD_NO_CART,   /* no flash cart that has a driver answered */
    CARD_NO_CARD,   /* the cart has no card */
    CARD_NO_FS,     /* the card is not FAT16 or FAT32 */
    CARD_NO_FOLDER, /* a card with no folder GLOVERPR yet */
    CARD_READY,
    CARD_ERROR,
    CARD_WRITEBACK,   /* the cart has a save to write back to the card: try again */
    CARD_READ_FAILED, /* a test read of the card did not fill the cart's buffer */
};

/* The card file the menu chose, 1 to CARD_SLOTS. */
extern u8 cardSlot;
extern u8 cardStatus;
/* What the last Check (or Save or Delete) saw of each file: 0 for none, else the state's level plus 1, or 255 for a
 * file that is not one of ours. cardScanned is 1 after a Check. */
extern u8 cardHave[CARD_SLOTS];
extern u8 cardScanned;

/* The menu asks for something. Save and Load close the menu: the game stops after "saving to card..." shows. */
void CardRequest(int request);
/* The game thread: carries out a request. A Save or a Load first shows its message for a game frame. */
void CardGameFrame(void);
/* 1 if a request waits. */
int CardPending(void);

/* The menu's text. */
void CardStatusText(char *buf, int size, int arg);
void CardFileText(char *buf, int size, int arg);

#endif
