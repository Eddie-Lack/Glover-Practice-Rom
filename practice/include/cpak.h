#ifndef PRACTICE_CPAK_H
#define PRACTICE_CPAK_H

#include "types.h"

/* Controller Pak storage (docs/build.md, "The Controller Pak"). The game never uses the pak. The practice code
 * keeps one note on it, "GLOVER PRACTICE", in the standard layout of the pak's file system: the other notes on the
 * pak stay as they are. The note holds the full settings, the Hi Scores screen choice, the trainer switches, the
 * practice best times and, if it fits, the input recording.
 *
 * A pak is valid when one of the four ID blocks is valid and one of the two index tables is valid. The other copies
 * can be damaged: a check reports it, and "Repair" writes them again. A pak with no valid ID block or no valid index
 * table is not formatted; only that one is offered a format.
 *
 * All serial interface work is done by the controller thread, after its read of the controllers (CPakService),
 * transfers of 32 bytes for at most 25 ms each game frame. With no request, a game frame costs one byte test.
 * While a request runs, the game frame that moves blocks lasts longer, and the game's next controller read has to
 * send its command again.
 *
 * The pak's layout (32 KB, 128 pages of 256 bytes, all numbers big-endian):
 *   page 0       the ID area: four copies of a 32-byte ID block, at 0x20, 0x60, 0x80 and 0xC0 (block 0 is also
 *                where the driver tests that the device keeps data)
 *   page 1, 2    the index table and its backup: 128 entries of 2 bytes, one for each page. A free page holds 3,
 *                the last page of a note holds 1, any other holds the number of the note's next page. Byte 1 of
 *                the table is the sum of the low bytes of entries 5 to 127.
 *   pages 3, 4   the note table: 16 entries of 32 bytes (game code, company code, first page, status, name)
 *   pages 5 to 127  the data of the notes
 */

/* The note's data (the practice note's "format"), all numbers big-endian:
 *   0x00 u32 magic "GPRC"
 *   0x04 u8  version
 *   0x05 u8  parts: CPAK_PART_*
 *   0x06 u16 used: the length of the data in bytes, with this header
 *   0x08 u32 sum: the sum of all bytes of the data, with this field as 0
 *   0x0C u16 settingsSize: the length of the settings part
 *   0x0E u8  showGame: the Hi Scores screen choice (bestTimesState.showGame)
 *   0x0F u8  trainers: the trainer switches (run.trainers)
 *   0x10 u16 bestCount: the number of best times
 *   0x12 u16 reserved
 *   0x14 u32 frames: the game frames of the recording (0 with no recording)
 *   0x18 u32 runs: the runs of the recording
 *   0x1C u32 reserved
 *   0x20 the settings (sizeof(Settings) bytes), then bestCount u16 best times, then the recording
 * The recording is runs of 5 bytes: u16 buttons, s8 stick x, s8 stick y, u8 the number of game frames (1 to 255)
 * that the controller held exactly this. tests/cpak.py decodes it. */
#define CPAK_NOTE_MAGIC 0x47505243 /* "GPRC" */
#define CPAK_NOTE_VERSION 1
#define CPAK_HEADER_SIZE 0x20
#define CPAK_PART_SETTINGS 1
#define CPAK_PART_BESTS 2
#define CPAK_PART_RECORDING 4

/* The pak's results, for the menu. */
enum {
    CPAK_NONE,          /* nothing yet */
    CPAK_NO_PAK,        /* no Controller Pak in the controller */
    CPAK_NOT_FORMATTED, /* no ID block or no index table is valid: nothing was changed */
    CPAK_NO_ROOM,       /* not enough free pages or no free note */
    CPAK_SAVED,
    CPAK_SAVED_NO_REC,  /* saved, but the recording does not fit */
    CPAK_LOADED,
    CPAK_NO_NOTE,       /* the pak has no practice note */
    CPAK_FORMATTED,
    CPAK_DELETED,
    CPAK_ERROR,         /* a transfer failed */
    CPAK_BAD_NOTE,      /* the note is damaged or from another version: nothing was loaded */
    CPAK_FOUND,         /* a check: the pak is fine, and the practice note is there */
    CPAK_EMPTY,         /* a check: the pak is fine, and has no practice note */
    CPAK_UNSUPPORTED,   /* a pak with more than one bank */
    CPAK_ALREADY,       /* a format or a repair was asked for, but the pak's tables are valid */
    CPAK_NOT_A_PAK,     /* a device that keeps no data, such as a Rumble Pak: nothing was changed */
    CPAK_INCONSISTENT,  /* two practice notes, or pages that two notes share: nothing was changed */
    CPAK_ERROR_NOTE,    /* a save over the note's own pages failed: the note may be damaged */
    CPAK_REPAIRED,      /* the damaged copies of the ID block and the index table were written again */
    CPAK_TOO_LONG,      /* the note's recording is longer than RECORDING_MAX game frames: nothing was loaded */
    CPAK_STATUS_COUNT
};

enum { CPAK_REQ_NONE, CPAK_REQ_CHECK, CPAK_REQ_SAVE, CPAK_REQ_LOAD, CPAK_REQ_DELETE, CPAK_REQ_FORMAT, CPAK_REQ_REPAIR };

/* The tests read these at their offsets (tests/layout.py). */
typedef struct CPak {
    /* 0x00 */ u8 request;        /* for the controller thread: CPAK_REQ_* */
    /* 0x01 */ u8 status;         /* CPAK_* of the last operation; 0 while one runs */
    /* 0x02 */ u8 port;           /* the controller with the pak: 0 controller 1, 1 controller 2 */
    /* 0x03 */ u8 withRecording;  /* 1: a save also stores the input recording */
    /* 0x04 */ u8 working;        /* 1 while an operation runs */
    /* 0x05 */ u8 checked;        /* 1: freePages, freeNotes and noteBytes are from a check */
    /* 0x06 */ u8 freeNotes;      /* free entries of the note table */
    /* 0x07 */ u8 hasNote;        /* 1: the practice note is on the pak */
    /* 0x08 */ u8 canFormat;      /* 1: the last operation found a pak without a valid ID block or index table */
    /* 0x09 */ u8 confirm;        /* the view waits for A to confirm: a CPAK_REQ_* */
    /* 0x0A */ u16 freePages;     /* free pages of the pak */
    /* 0x0C */ u16 noteBytes;     /* the pages of the practice note, in bytes */
    /* 0x0E */ u8 cursor;         /* the row of the view */
    /* 0x0F */ u8 canRepair;     /* 1: a copy of the ID block or of the index table is damaged, another is valid */
    /* 0x10 */ u32 transfers;     /* blocks of 32 bytes moved since the boot */
    /* 0x14 */ u32 commands;      /* commands sent to the controller since the boot, with the status commands */
    /* 0x18 */ u32 failAt;        /* tests: from this value of transfers on, every block fails as if the pak was
                                   * pulled (0: never) */
} CPak;

extern CPak cpak;
extern const struct View cpakView;

/* Called by the controller thread after its controller read. Moves some blocks of the running operation, when the
 * game has no EEPROM request waiting. */
void CPakService(void);

#endif
