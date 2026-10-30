#ifndef PRACTICE_SAVESTATE_H
#define PRACTICE_SAVESTATE_H

#include "types.h"

/* A savestate holds the game's memory: its data and bss, the heap and the texture buffers (tools/mkstate.py).
 * States are kept compressed (lz.h) in a pool in the Expansion Pak's memory after the practice segment.
 *
 * The pool holds two kinds of items. A base is the state of one level, compressed on its own. A slot is a state
 * stored as its difference from a base: a block of a slot that equals the base's costs 2 bytes, a block that
 * differs is compressed from the XOR of the two, and a block that is better compressed on its own is. A state
 * saved in the level of a base takes some kilobytes. Saving in a level with no base makes a base first (about 0.6
 * MB). Bases that no slot uses are freed.
 *
 * The slots are STATE_USER_SLOTS that the player chooses, one that a recording keeps its state in, and one that
 * the instant reset keeps the state of the level's first game frame in. */
#define STATE_POOL 0x80480000
#define STATE_POOL_END 0x806EC000
/* 16 KB behind the pool for the SD card test mailbox (sd.c, only in the test build, make testrom). It is reserved in
 * every build: the test ROM then has the release ROM's memory map, and the mailbox needs no room in the practice
 * segment. The test link sets the symbol sdMailbox to this address. */
#define TEST_MAILBOX 0x806EC000
#define TEST_MAILBOX_SIZE 0x4000
/* 64 KB behind the pool for the work of a save and a load: two blocks of LZ_BLOCK_MAX bytes. They are not in the
 * practice segment, whose 512 KB is nearly full. */
#define STATE_SCRATCH 0x806F0000
/* 20 KB behind them for the stream of a card file (card.h), and 12 KB after that for the card code's own data: the file
 * system's state, the CRC table and a sector buffer. The practice segment has little room for them. The area ends
 * where the ghosts begin. */
#define CARD_STAGE 0x806F8000
#define CARD_STAGE_SIZE 0x5000
#define CARD_WORK 0x806FD000
#define CARD_WORK_SIZE 0x3000
_Static_assert(CARD_STAGE + CARD_STAGE_SIZE == CARD_WORK && CARD_WORK + CARD_WORK_SIZE <= 0x80700000,
               "the card's memory lies between the scratch blocks and the ghosts");
#define STATE_USER_SLOTS 10
#define STATE_SLOT_RECORD 10
#define STATE_SLOT_AUTO 11
#define STATE_SLOTS 12
/* A base takes about 0.6 MB and the pool is 2.4 MB: four bases never fit. A fourth level takes the closest base of
 * another level, and its states are larger. */
#define STATE_BASES 3

/* The game thread, at the start of a game frame (ActionsGameFrame). Each returns 1 if it saved or loaded. The first
 * two use the slot that the player chose (stateSlot). */
int StateSave(void);
int StateLoad(void);
int StateSaveSlot(int slot);
int StateLoadSlot(int slot);

/* 1 if Save state or Load state would try now (not on a title; for a load, the slot holds a state). The caller
 * shows "saving..." or "loading..." for a game frame before it calls them: they stop the game for a moment. */
int StateSaveReady(void);
int StateLoadReady(int slot);

/* Makes slot TO a copy of slot FROM (the same base). Returns 1 if it did. */
int StateCopySlot(int from, int to);

/* The game thread, at the start of each game frame: saves the state of a level's first game frame into the
 * automatic slot while the instant reset is on. */
void StateGameFrame(void);
/* The reset that loads the automatic state, if it belongs to the level start that runs now (not in a cameo, a pause,
 * a death or an exit). Returns 1 if it did. */
int StateInstantReset(void);

/* Empties a slot (STATE_SLOTS: every slot). The menu calls this from the controller thread, which runs only
 * while the game thread is not inside a save or a load. */
void StateClear(int slot);

/* The menu's text for the chosen slot and for the pool. */
void StateSlotText(char *buf, int size, int arg);
void StatePoolText(char *buf, int size, int arg);
void StateAutoText(char *buf, int size, int arg);

/* 1 if the user slot holds a state. */
int StateSlotUsed(int slot);

/* The slot that Save state and Load state use (0 to STATE_USER_SLOTS - 1). */
extern u8 stateSlot;
/* 1 if that slot holds a state. */
extern u8 stateSaved;
/* The number of states loaded. Practice code that follows the game's place compares it to find a load. */
extern u32 stateLoads;


/* ---- Card files (card.h) ----
 * A card file holds one state by itself: the record of each chunk, as in a base. It does not depend on a base in the
 * pool. The code that reads and writes the card gives these two streams. */

/* The most bytes that the record of one chunk takes: its header, up to 2 bytes of padding and LZ_BLOCK_MAX. */
#define CARD_MAX_RECORD 16400

/* Where an export puts its records. BUF is word-aligned. POS bytes are in it, from the front of the file's stream
 * (the file's offset is POS plus what earlier calls of FLUSH took out). The export calls FLUSH when fewer than
 * CARD_MAX_RECORD bytes are free. FLUSH takes out whole sectors from the front (all of them at LAST, with the
 * remainder padded), moves the rest to the front and lowers POS by a multiple of 512. It returns 0, or an error. */
typedef struct CardStream {
    u8 *buf;
    u32 pos;
    u32 cap;
    int (*flush)(struct CardStream *s, int last);
    u32 chunk;  /* the chunks written, for the progress bar */
    u32 chunks; /* all of them */
} CardStream;

/* Where an import takes its records from. BUF is word-aligned, POS is the next byte and LEN the end of what is in
 * it. The import calls FILL when fewer than CARD_MAX_RECORD bytes remain and EOF is 0. FILL drops the bytes before
 * POS in whole words (keeping the file's offset equal to the address modulo 4), moves the rest to the front and
 * reads more. It sets EOF when LEN is the end of the records. It returns 0, or an error. */
typedef struct CardSource {
    u8 *buf;
    u32 pos;
    u32 len;
    u32 cap;
    int eof;
    int (*fill)(struct CardSource *s);
    u32 chunk;
    u32 chunks;
} CardSource;

/* What a card file records about its state, and what a ROM build has to match: the number of chunks, the bytes
 * of the state and a hash of the ranges that it holds. */
typedef struct StateMeta {
    u8 level;
    s32 song;
    u32 chunks;
    u32 bytes;
    u32 layout;
} StateMeta;

/* Fills CHUNKS, BYTES and LAYOUT of this build. */
void StateLayout(StateMeta *m);
/* The same, with the level and song of a slot. Returns 1 if the slot holds a state. */
int StateSlotMeta(int slot, StateMeta *m);

/* Stops the game's other threads (the same stop as a save's), and lets them go. StateHold returns the priority to
 * give back, or STATE_HOLD_FAILED. The menu cannot clear a slot in between. */
#define STATE_HOLD_FAILED (-1)
s32 StateHold(void);
void StateResume(s32 pri);

/* Writes the records of a slot's state into S, calling its FLUSH as it goes. The game must be held. Returns 0. */
int StateCardExport(int slot, CardStream *s);
/* Loads the state that SRC holds, as a load of a slot does, with the level and song of M and the name NAME in the
 * message. Returns 1 if it loaded, 0 if it did not start (a title, the EEPROM busy, the game busy), and -1 if the
 * records did not fit or could not be read, after the memory was changed. */
int StateCardLoad(const StateMeta *m, CardSource *src, const char *name);

#endif
