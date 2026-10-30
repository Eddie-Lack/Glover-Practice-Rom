/* Controller Pak storage (cpak.h): a small driver for the pak's serial interface commands and its file system, and
 * the "Controller Pak" view of the Settings menu. The driver follows the public description of the pak: the read and
 * write commands of the controller port with their address and data checks, and the layout of the ID area, the index
 * table and the note table.
 *
 * The driver first identifies the device: it writes the inverse of one block of page 0 and reads it back, then
 * writes the old block again. A Rumble Pak or a Transfer Pak ignores the write and is refused. After that it writes
 * only to a pak with a valid ID block and a valid index table, except for Format, which is offered only for a pak
 * that has neither. A write never leaves the pak's tables inconsistent (see "Operations").
 *
 * An operation (check, save, load, delete, repair, format) is a list of steps. The controller thread runs them from
 * CPakService: blocks of 32 bytes for at most 25 ms in each game frame. A game frame that moves blocks takes
 * longer, and the game's next controller read has to send its command again. */
#include "besttimes.h"
#include "actions.h"
#include "cpak.h"
#include "fmt.h"
#include "game.h"
#include "ghost.h"
#include "hud.h"
#include "menu.h"
#include "recording.h"
#include "savestate.h"
#include "settings.h"
#include "text.h"
#include "trainers.h"

/* libultra, as the game links it. */
s32 __osSiRawStartDma(s32 dir, void *pifRam);
void __osSiGetAccess(void);
void __osSiRelAccess(void);
extern u8 __osContLastCmd;

#define COUNTER_RATE 46875000u
/* The most that one game frame spends on transfers: the controller thread waits for the serial interface most of that
 * time, and a game frame lasts 50 ms. */
#define BUDGET (COUNTER_RATE / 40) /* 25 ms */
#define SI_WRITE 1                  /* RDRAM to the PIF */
#define SI_READ 0

#define PIF_NO_DEVICE 1 /* the controller did not answer */
#define PIF_ERROR 2     /* a transfer or a check failed */
#define PIF_NO_PAK 3    /* the pak slot is empty */

#define PIF_NOT_PAK 4   /* a device that answers, but is not a Controller Pak (ours only) */

#define CMD_STATUS 0x00
#define CMD_READ 0x02
#define CMD_WRITE 0x03
#define TRIES 3

#define PAGE 256
#define BLOCK 32
#define PAGES 128
#define FIRST_DATA_PAGE 5
#define NOTES 16
#define PAGE_FREE 0x0003
#define PAGE_LAST 0x0001
#define ID_OFFSET 0x20 /* the first of the four ID blocks */
#define NOTE_MAX_BYTES ((PAGES - FIRST_DATA_PAGE) * PAGE)
#define NOTE_CODE 0x47505243 /* "GPRC" */
#define NOTE_COMPANY 0x4750  /* "GP" */
#define NOTE_STATUS 2

CPak cpak;

_Static_assert(__builtin_offsetof(CPak, status) == 0x01 && __builtin_offsetof(CPak, hasNote) == 0x07 &&
                   __builtin_offsetof(CPak, confirm) == 0x09 && __builtin_offsetof(CPak, canRepair) == 0x0F &&
                   __builtin_offsetof(CPak, freePages) == 0x0A && __builtin_offsetof(CPak, cursor) == 0x0E &&
                   __builtin_offsetof(CPak, transfers) == 0x10 && __builtin_offsetof(CPak, commands) == 0x14 &&
                   __builtin_offsetof(CPak, failAt) == 0x18,
               "tests/layout.py has the offsets");

/* ---------------------------------------------------------------------------------------------------------
 * The serial interface
 */

/* The commands go to the PIF as one block of its RAM: for each controller before ours a 0, then 0xFF, the number
 * of bytes sent, the number of bytes to receive, the bytes sent, room for the answer, and 0xFE. Byte 63 set to 1
 * starts the PIF. The PIF sets bits 6 and 7 of the receive count when the controller did not answer. */
static u8 pif[64] __attribute__((aligned(16)));

static u16 Be16(const u8 *p) {
    return (u16)(p[0] << 8 | p[1]);
}

static u32 Be32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static void Put16(u8 *p, u32 v) {
    p[0] = (u8)(v >> 8);
    p[1] = (u8)v;
}

static void Put32(u8 *p, u32 v) {
    Put16(p, v >> 16);
    Put16(p + 2, v);
}

static void Copy(u8 *to, const u8 *from, u32 n) {
    for (u32 i = 0; i < n; i++) {
        to[i] = from[i];
    }
}

static void Fill(u8 *to, u8 value, u32 n) {
    for (u32 i = 0; i < n; i++) {
        to[i] = value;
    }
}

/* The check bits for the address of a block: the address with its low 5 bits replaced by the remainder of the
 * upper 11 bits, times x^5, divided by x^5 + x^2 + 1. */
static u16 AddressWord(u16 addr) {
    u32 v = addr & 0xFFE0;

    for (int bit = 15; bit >= 5; bit--) {
        if (v & (1u << bit)) {
            v ^= 0x25u << (bit - 5);
        }
    }
    return (u16)((addr & 0xFFE0) | (v & 0x1F));
}

/* The check byte of 32 bytes of data: CRC-8 with the polynomial x^8 + x^7 + x^2 + 1 (0x85), start value 0. */
static u8 DataCrc(const u8 *data) {
    u8 crc = 0;

    for (int i = 0; i < BLOCK; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (u8)(crc << 1 ^ 0x85) : (u8)(crc << 1);
        }
    }
    return crc;
}

/* Sends one command to controller CH and takes RXLEN bytes of the answer. Returns 0, PIF_NO_DEVICE or PIF_ERROR.
 * The answer area starts as 0xFF, which fails the data check of a read. libultra's serial lock keeps the game's own
 * transfers apart from this one. The next read of the game sends its command again, because the block in the PIF
 * is not its own. */
static int Command(int ch, const u8 *tx, int txLen, u8 *rx, int rxLen) {
    int i = 0;
    int ok;
    int header;

    cpak.commands++;
    Fill(pif, 0, sizeof(pif));
    for (int c = 0; c < ch; c++) {
        pif[i++] = 0;
    }
    pif[i++] = 0xFF;
    header = i;
    pif[i++] = (u8)txLen;
    pif[i++] = (u8)rxLen;
    Copy(pif + i, tx, txLen);
    i += txLen;
    int answer = i;
    Fill(pif + i, 0xFF, rxLen);
    i += rxLen;
    pif[i] = 0xFE;
    pif[63] = 1;

    __osSiGetAccess();
    __osContLastCmd = CMD_WRITE;
    ok = __osSiRawStartDma(SI_WRITE, pif) == 0;
    if (ok) {
        osRecvMesg(&controllerMsgQ, NULL, OS_MESG_BLOCK);
        ok = __osSiRawStartDma(SI_READ, pif) == 0;
        if (ok) {
            osRecvMesg(&controllerMsgQ, NULL, OS_MESG_BLOCK);
        }
    }
    __osSiRelAccess();
    if (!ok) {
        return PIF_ERROR;
    }
    if (pif[header + 1] & 0x80) {
        return PIF_NO_DEVICE;
    }
    if (pif[header + 1] & 0x40) {
        return PIF_ERROR;
    }
    Copy(rx, pif + answer, rxLen);
    return 0;
}

/* Whether controller CH has a pak: the status command answers with the type (0x0500, a controller) and a status byte
 * whose bit 0 is set when a pak is plugged in. */
static int PakPresent(int ch) {
    u8 tx[1] = { CMD_STATUS };
    u8 rx[3];
    int r = Command(ch, tx, 1, rx, 3);

    if (r != 0) {
        return r;
    }
    return (rx[0] == 0x05 && rx[1] == 0x00 && (rx[2] & 1)) ? 0 : PIF_NO_PAK;
}

/* The tests can make the pak "disappear" in the middle of an operation (CPak.failAt). */
static int Injected(void) {
    if (cpak.failAt != 0 && cpak.transfers >= cpak.failAt) {
        cpak.transfers++;
        return 1;
    }
    return 0;
}

static int ReadBlock(int ch, u16 addr, u8 *out) {
    u8 tx[3] = { CMD_READ, 0, 0 };
    u8 rx[BLOCK + 1];
    int r = PIF_ERROR;

    if (Injected()) {
        return PIF_NO_PAK;
    }
    Put16(tx + 1, AddressWord(addr));
    for (int t = 0; t < TRIES && r == PIF_ERROR; t++) {
        r = Command(ch, tx, 3, rx, BLOCK + 1);
        if (r == 0) {
            u8 crc = DataCrc(rx);

            if (rx[BLOCK] == crc) {
                Copy(out, rx, BLOCK);
            } else {
                /* The pak answers an empty slot with the check byte inverted. */
                r = rx[BLOCK] == (u8)~crc ? PIF_NO_PAK : PIF_ERROR;
            }
        }
    }
    cpak.transfers++;
    return r;
}

static int WriteBlock(int ch, u16 addr, const u8 *data) {
    u8 tx[3 + BLOCK] = { CMD_WRITE, 0, 0 };
    u8 rx[1];
    int r = PIF_ERROR;

    if (Injected()) {
        return PIF_NO_PAK;
    }
    Put16(tx + 1, AddressWord(addr));
    Copy(tx + 3, data, BLOCK);
    for (int t = 0; t < TRIES && r == PIF_ERROR; t++) {
        r = Command(ch, tx, 3 + BLOCK, rx, 1);
        if (r == 0 && rx[0] != DataCrc(data)) {
            /* As for a read, an empty slot answers with the check byte inverted. */
            r = rx[0] == (u8)~DataCrc(data) ? PIF_NO_PAK : PIF_ERROR;
        }
    }
    cpak.transfers++;
    return r;
}

/* ---------------------------------------------------------------------------------------------------------
 * The file system
 */

/* The pak's own data, as read. */
static u8 tabPage0[PAGE];       /* the ID area */
static u8 tabIndex[PAGE];       /* the index table */
static u8 tabNotes[NOTES * BLOCK];
static u8 scratch[PAGE];        /* the second index table, and the blocks that a verify reads back */
static u8 foreign[PAGES];       /* 1: a page that the chain of another note reaches */
/* The note: its pages, written or read in one piece. */
/* It lives in the card code's spare memory (savestate.h): a Controller Pak operation and an SD card operation never
 * run together, because the menu stays in the Controller Pak view while an operation runs. */
#define noteBuf ((u8 *)CARD_STAGE)
_Static_assert(NOTE_MAX_BYTES <= CARD_STAGE_SIZE + CARD_WORK_SIZE, "the note fits the card's spare memory");

static const u8 pageList0[] = { 0 };
static const u8 pageList1[] = { 1 };
static const u8 pageList2[] = { 2 };
static const u8 pageList34[] = { 3, 4 };

/* The four copies of the ID block. */
static const u16 idOffsets[4] = { 0x20, 0x60, 0x80, 0xC0 };

static int Same(const u8 *a, const u8 *b, u32 n) {
    for (u32 i = 0; i < n; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

/* The ID block checks: the sum of its first 14 words is in word 14, and 0xFFF2 minus the sum in word 15. */
static u16 IdSum(const u8 *b) {
    u16 sum = 0;

    for (int i = 0; i < 14; i++) {
        sum = (u16)(sum + Be16(b + i * 2));
    }
    return sum;
}

static int IdValid(const u8 *b) {
    u16 sum = IdSum(b);

    return Be16(b + 0x1C) == sum && Be16(b + 0x1E) == (u16)(0xFFF2 - sum);
}

/* The index table's byte 1 is the sum of the low bytes of the entries of pages 5 to 127. */
static u8 IndexSum(const u8 *t) {
    u8 sum = 0;

    for (int p = FIRST_DATA_PAGE; p < PAGES; p++) {
        sum = (u8)(sum + t[p * 2 + 1]);
    }
    return sum;
}

static int IndexValid(const u8 *t) {
    return t[1] == IndexSum(t);
}

/* What a scan of the tables found. */
static struct {
    u8 chain[PAGES]; /* the pages of the practice note, in order */
    u8 chainLen;     /* 0: no note, or its chain is damaged */
    s8 slot;         /* the note table entry of the practice note, or -1 */
    s8 freeSlot;     /* the first free entry, or -1 */
    u8 problem;      /* 1: a second practice note, or a chain that shares a page with another note's chain */
    u16 freePages;
} scan;

static u16 Entry(int page) {
    return Be16(tabIndex + page * 2);
}

static const u8 *NoteEntry(int slot) {
    return tabNotes + slot * BLOCK;
}

static int EntryUsed(const u8 *e) {
    return Be32(e) != 0 && Be16(e + 4) != 0;
}

/* Follows a chain of pages from P into OUT and returns the number of pages. ENDED is 1 if the chain ended in the
 * last-page mark. A chain that leaves the data pages, loops or runs on does not end. */
static int Walk(u16 p, u8 *out, int *ended) {
    int n = 0;

    *ended = 0;
    while (p >= FIRST_DATA_PAGE && p < PAGES && n < PAGES - FIRST_DATA_PAGE) {
        out[n++] = (u8)p;
        p = Entry(p);
        if (p == PAGE_LAST) {
            *ended = 1;
            break;
        }
    }
    return n;
}

static u16 CountFree(void) {
    u16 n = 0;

    for (int p = FIRST_DATA_PAGE; p < PAGES; p++) {
        n += Entry(p) == PAGE_FREE && !foreign[p];
    }
    return n;
}

/* Reads the tables in tabIndex and tabNotes: finds the practice note and follows its chain of pages. */
static void Scan(void) {
    u8 pages[PAGES];
    int ended;

    scan.chainLen = 0;
    scan.slot = -1;
    scan.freeSlot = -1;
    scan.problem = 0;
    Fill(foreign, 0, sizeof(foreign));
    for (int s = 0; s < NOTES; s++) {
        const u8 *e = NoteEntry(s);

        if (!EntryUsed(e)) {
            if (scan.freeSlot < 0) {
                scan.freeSlot = (s8)s;
            }
        } else if (Be32(e) == NOTE_CODE && Be16(e + 4) == NOTE_COMPANY) {
            if (scan.slot < 0) {
                scan.slot = (s8)s;
            } else {
                scan.problem = 1; /* a second practice note: save and delete would see only one */
            }
        }
    }
    /* The pages that other notes reach, also from a chain that is damaged. */
    for (int s = 0; s < NOTES; s++) {
        if (s != scan.slot && EntryUsed(NoteEntry(s))) {
            int n = Walk(Be16(NoteEntry(s) + 6), pages, &ended);

            for (int i = 0; i < n; i++) {
                foreign[pages[i]] = 1;
            }
        }
    }
    scan.freePages = CountFree();
    if (scan.slot >= 0) {
        int n = Walk(Be16(NoteEntry(scan.slot) + 6), scan.chain, &ended);

        if (ended) {
            scan.chainLen = (u8)n;
        }
        for (int i = 0; i < n; i++) {
            if (foreign[scan.chain[i]]) {
                scan.problem = 1;
            }
        }
    }
    cpak.checked = 1;
    cpak.freePages = scan.freePages;
    cpak.freeNotes = 0;
    for (int s = 0; s < NOTES; s++) {
        cpak.freeNotes += !EntryUsed(NoteEntry(s));
    }
    cpak.hasNote = scan.chainLen != 0;
    cpak.noteBytes = (u16)(scan.chainLen * PAGE);
}

/* The name of the note, in the pak's character set: 0x0F is a space, 0x10 to 0x19 the digits and 0x1A to 0x33 the
 * capital letters. */
static void NoteName(u8 *out) {
    static const char name[] = "GLOVER PRACTICE";

    Fill(out, 0, 16);
    for (int i = 0; name[i] != '\0'; i++) {
        out[i] = name[i] == ' ' ? 0x0F : (u8)(0x1A + name[i] - 'A');
    }
}

/* A new ID block. The words before the checks are the pak's serial number and its kind: one bank, device 1. */
static void MakeId(u8 *b) {
    u32 r = osGetCount();

    Fill(b, 0, BLOCK);
    Put32(b + 0x00, 0);
    Put32(b + 0x04, r ^ 0x5A5A1234);
    Put32(b + 0x08, r * 1103515245u + 12345u);
    Put32(b + 0x0C, r ^ 0x0F0F0F0F);
    Put32(b + 0x10, r * 69069u);
    Put16(b + 0x14, r >> 7);
    Put16(b + 0x18, 1);
    b[0x1A] = 1;
    b[0x1B] = 0;
    Put16(b + 0x1C, IdSum(b));
    Put16(b + 0x1E, (u16)(0xFFF2 - IdSum(b)));
}

/* The index table of an empty pak: the system pages hold 0, the data pages are free. */
static void MakeIndex(u8 *t) {
    Fill(t, 0, PAGE);
    for (int p = FIRST_DATA_PAGE; p < PAGES; p++) {
        Put16(t + p * 2, PAGE_FREE);
    }
    t[1] = IndexSum(t);
}

/* ---------------------------------------------------------------------------------------------------------
 * The note
 */

static u32 NoteSum(const u8 *p, u32 n) {
    u32 sum = NOTE_CODE;

    for (u32 i = 0; i < n; i++) {
        sum = (sum << 1 | sum >> 31) ^ p[i];
    }
    return sum;
}

/* Builds the note in noteBuf with room for CAP bytes. Returns the length in bytes. The recording goes in if
 * asked and if it fits. */
static u32 BuildNote(u32 cap, u8 *recordingLeftOut) {
    u8 *p = noteBuf;
    u32 at = CPAK_HEADER_SIZE;
    u32 frames = 0;
    u32 runs = 0;
    u8 parts = CPAK_PART_SETTINGS | CPAK_PART_BESTS;

    Fill(noteBuf, 0, NOTE_MAX_BYTES);
    Copy(p + at, (const u8 *)&settings, sizeof(Settings));
    at += sizeof(Settings);
    for (int i = 0; i < BEST_LEVELS; i++) {
        Put16(p + at, practiceBests.frames[i]);
        at += 2;
    }
    *recordingLeftOut = 0;
    if (cpak.withRecording && recording.count > 0) {
        int n = cap > at ? RecordingPack(p + at, cap - at, &runs) : -1;

        if (n >= 0) {
            at += (u32)n;
            frames = recording.count;
            parts |= CPAK_PART_RECORDING;
        } else {
            *recordingLeftOut = 1;
        }
    }
    Put32(p, CPAK_NOTE_MAGIC);
    p[4] = CPAK_NOTE_VERSION;
    p[5] = parts;
    Put16(p + 6, at);
    Put16(p + 0x0C, sizeof(Settings));
    p[0x0E] = bestTimesState.showGame;
    p[0x0F] = run.trainers;
    Put16(p + 0x10, BEST_LEVELS);
    Put32(p + 0x14, frames);
    Put32(p + 0x18, runs);
    Put32(p + 8, NoteSum(p, at));
    return at;
}

/* Checks the note in noteBuf, whose first page has been read, and returns its length, or 0. */
static u32 NoteHeader(void) {
    u32 used = Be16(noteBuf + 6);

    if (Be32(noteBuf) != NOTE_CODE || noteBuf[4] != CPAK_NOTE_VERSION || used < CPAK_HEADER_SIZE ||
        used > scan.chainLen * PAGE) {
        return 0;
    }
    return used;
}

static int NoteValid(u32 used) {
    u32 stored = Be32(noteBuf + 8);
    u32 settingsSize = Be16(noteBuf + 0x0C);
    u32 bests = Be16(noteBuf + 0x10);
    u32 need = CPAK_HEADER_SIZE + settingsSize + bests * 2;
    int sumOk;

    Put32(noteBuf + 8, 0);
    sumOk = NoteSum(noteBuf, used) == stored;
    Put32(noteBuf + 8, stored);
    if (!sumOk || settingsSize != sizeof(Settings) || bests != BEST_LEVELS || need > used) {
        return 0;
    }
    if (noteBuf[5] & CPAK_PART_RECORDING) {
        u32 runs = Be32(noteBuf + 0x18);
        u32 frames = 0;
        const u8 *r = noteBuf + need;

        /* The runs must be in the note, each of 1 to 255 game frames, and add up to the header's count. */
        if (runs > (used - need) / RECORDING_RUN_SIZE) {
            return 0;
        }
        for (u32 i = 0; i < runs; i++) {
            if (r[i * RECORDING_RUN_SIZE + 4] == 0) {
                return 0;
            }
            frames += r[i * RECORDING_RUN_SIZE + 4];
        }
        if (frames != Be32(noteBuf + 0x14) || frames > RECORDING_MAX) {
            return 0;
        }
    }
    return 1;
}

/* Takes the note's data into the practice state. The note was checked. The settings are taken only if they are from
 * this version of the settings; the rest is taken in any case. */
static void ApplyNote(void) {
    const u8 *p = noteBuf + CPAK_HEADER_SIZE;
    u32 settingsSize = Be16(noteBuf + 0x0C);
    u8 *const flags[] = { &settings.ballNormal, &settings.menuPauses, &settings.hudShadow, &settings.timerFrames,
                          &settings.inputHistory, &settings.debugPad, &settings.splitByCrystal, &settings.debugPadL,
                          &settings.instantReset };

    if ((noteBuf[5] & CPAK_PART_SETTINGS) && Be32(p) == SETTINGS_MAGIC && p[4] == SETTINGS_VERSION) {
        Copy((u8 *)&settings, p, sizeof(Settings));
        for (u32 i = 0; i < ARRAY_COUNT(flags); i++) {
            *flags[i] &= 1;
        }
        for (int i = 0; i < HUD_COUNT; i++) {
            settings.hud[i].on &= 1;
        }
        SettingsFixMenuLook();
    }
    p += settingsSize;
    if (noteBuf[5] & CPAK_PART_BESTS) {
        for (int i = 0; i < BEST_LEVELS; i++) {
            practiceBests.frames[i] = Be16(p + i * 2);
        }
    }
    p += BEST_LEVELS * 2;
    bestTimesState.showGame = noteBuf[0x0E] & 1;
    for (int bit = 1; bit <= TRAINER_CLIP; bit <<= 1) {
        if ((run.trainers ^ noteBuf[0x0F]) & bit) {
            TrainersToggle(bit);
        }
    }
    if ((noteBuf[5] & CPAK_PART_RECORDING) && !recording.recording && !recording.playing) {
        RecordingUnpack(p, Be32(noteBuf + 0x18));
        /* The ghosts belong to the recordings that made them. */
        GhostClear();
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * Operations
 *
 * A save keeps the pak consistent after every write. With enough free pages, the order is: the data into free
 * pages, the index tables with the new chain linked, the note table entry (one block) with the new first page, the
 * index tables with the old pages freed. A step that fails leaves the old note (before the entry), or the new one
 * (after it); at worst some pages stay marked as used. With too few free pages the save writes over the note's own
 * pages. It then clears the entry first and frees the old pages, so a step that fails leaves no note. A delete
 * clears the entry first, then frees the pages.
 */

enum {
    PH_PROBE,
    PH_ID,
    PH_INDEX,
    PH_INDEX2,
    PH_NOTES,
    PH_SCAN,
    PH_SEQ,
    PH_VERIFY,
    PH_LOAD_FIRST,
    PH_LOAD_REST,
};

/* The steps of a sequence. */
enum {
    OP_PLAN,        /* chooses the pages of the new note from the free pages */
    OP_DATA,        /* writes the note's pages */
    OP_LINK,        /* links the new pages in the index table */
    OP_FREE_OLD,    /* frees the old note's pages in the index table */
    OP_IDX1,        /* writes the index table */
    OP_IDX2,        /* writes its backup */
    OP_ENTRY_SET,   /* makes the note table entry */
    OP_ENTRY_CLEAR, /* clears it */
    OP_ENTRY_W,     /* writes the block of the entry */
    OP_FMT_PREP,    /* makes the tables of an empty pak */
    OP_NOTES_W,     /* writes the note table */
    OP_REPAIR_PREP, /* puts the valid ID block in all four places */
    OP_ID_W,        /* writes the ID area */
    OP_VERIFY,      /* reads the tables back and compares */
    OP_DONE,
};

static const u8 seqSave[] = { OP_PLAN, OP_DATA, OP_LINK, OP_IDX1, OP_IDX2, OP_ENTRY_SET, OP_ENTRY_W,
                              OP_FREE_OLD, OP_IDX1, OP_IDX2, OP_VERIFY, OP_DONE };
static const u8 seqSaveNew[] = { OP_PLAN, OP_DATA, OP_LINK, OP_IDX1, OP_IDX2, OP_ENTRY_SET, OP_ENTRY_W, OP_VERIFY,
                                 OP_DONE };
static const u8 seqInPlace[] = { OP_ENTRY_CLEAR, OP_ENTRY_W, OP_FREE_OLD, OP_IDX1, OP_IDX2, OP_PLAN, OP_DATA,
                                 OP_LINK, OP_IDX1, OP_IDX2, OP_ENTRY_SET, OP_ENTRY_W, OP_VERIFY, OP_DONE };
static const u8 seqDelete[] = { OP_ENTRY_CLEAR, OP_ENTRY_W, OP_FREE_OLD, OP_IDX1, OP_IDX2, OP_VERIFY, OP_DONE };
/* The note table first, the ID area last: a format that stops half way leaves a pak that is not valid. */
static const u8 seqFormat[] = { OP_FMT_PREP, OP_NOTES_W, OP_IDX1, OP_IDX2, OP_ID_W, OP_VERIFY, OP_DONE };
static const u8 seqRepair[] = { OP_REPAIR_PREP, OP_ID_W, OP_IDX1, OP_IDX2, OP_VERIFY, OP_DONE };

static struct {
    u8 op;     /* CPAK_REQ_*, or 0 */
    u8 phase;
    u8 ch;
    u8 recordingLeftOut;
    u8 plan[PAGES]; /* a save: the pages of the new note */
    u8 planLen;
    u8 pages;       /* a save: the pages the new note needs */
    u8 oldSlot;     /* a save, a delete: the entry */
    u8 newSlot;     /* a save: the entry was free */
    u8 inPlace;     /* a save: over the note's own pages */
    u8 risky;       /* a failure from now on may damage the note */
    u8 damaged;     /* the check found a bad copy of the ID block or of the index table */
    u8 idBad;       /* ... of the ID block */
    u8 idx1Ok;
    u8 vstep;
    u8 pos;
    u16 idOff;      /* the place of a valid ID block */
    u8 id[BLOCK];   /* ... and its content */
    const u8 *seq;
} job;

/* A transfer of blocks: BLOCK to END of the pages in the list, 8 blocks to a page, between the list's pages and BUF
 * (which holds the block FIRST). */
static struct {
    u8 active;
    u8 write;
    const u8 *pages;
    u16 block;
    u16 end;
    u8 *buf;
    u8 result;
} xfer;

static void StartXfer(int write, const u8 *pages, int first, int end, u8 *buf) {
    xfer.active = 1;
    xfer.write = (u8)write;
    xfer.pages = pages;
    xfer.block = (u16)first;
    xfer.end = (u16)end;
    xfer.buf = buf - first * BLOCK;
    xfer.result = 0;
}

/* Moves blocks until the transfer is done or the time is up. Returns 1 when it ended (xfer.result 0 or an error). */
static int Pump(u32 start) {
    while (xfer.block < xfer.end) {
        u16 b = xfer.block;
        u16 addr = (u16)(xfer.pages[b >> 3] * PAGE + (b & 7) * BLOCK);
        int r;

        if (osGetCount() - start >= BUDGET) {
            return 0;
        }
        r = xfer.write ? WriteBlock(job.ch, addr, xfer.buf + b * BLOCK) : ReadBlock(job.ch, addr, xfer.buf + b * BLOCK);
        if (r != 0) {
            xfer.result = (u8)r;
            return 1;
        }
        xfer.block++;
    }
    return 1;
}

static void Finish(u8 status) {
    cpak.status = status;
    cpak.working = 0;
    job.op = 0;
    xfer.active = 0;
}

static u8 FailStatus(int r) {
    return r == PIF_ERROR ? CPAK_ERROR : CPAK_NO_PAK;
}

/* A step failed: R is the result of the transfer. */
static void Fail(int r) {
    Finish(job.risky ? CPAK_ERROR_NOTE : FailStatus(r));
}

static void IndexChecksum(void) {
    tabIndex[1] = IndexSum(tabIndex);
}

static int PagesFor(u32 bytes) {
    return (int)((bytes + PAGE - 1) / PAGE);
}

/* Identifies the device. A Controller Pak keeps what is written to its pages: the inverse of block 0 is written, read
 * back, and replaced by the old block. Returns 0, PIF_NOT_PAK or the error of a transfer. */
static int Identify(int ch) {
    u8 old[BLOCK];
    u8 test[BLOCK];
    u8 back[BLOCK];
    int r = ReadBlock(ch, 0, old);
    int kept;
    int restored;

    if (r != 0) {
        return r;
    }
    for (int i = 0; i < BLOCK; i++) {
        test[i] = (u8)~old[i];
    }
    r = WriteBlock(ch, 0, test);
    if (r == 0) {
        r = ReadBlock(ch, 0, back);
    }
    kept = r == 0 && Same(back, test, BLOCK);
    restored = WriteBlock(ch, 0, old);
    if (r != 0) {
        return r;
    }
    if (restored != 0) {
        return restored;
    }
    return kept ? 0 : PIF_NOT_PAK;
}

/* Plans a save: the size of the note and the order of the writes. Returns a CPAK_ status to stop with, or 0. */
static u8 PlanSave(void) {
    int own = scan.chainLen;
    int avail = scan.freePages + own;
    u32 used;

    if (scan.problem) {
        return CPAK_INCONSISTENT;
    }
    if (scan.slot < 0 && scan.freeSlot < 0) {
        return CPAK_NO_ROOM;
    }
    used = BuildNote((u32)avail * PAGE, &job.recordingLeftOut);
    job.pages = (u8)PagesFor(used);
    if (job.pages > avail) {
        return CPAK_NO_ROOM;
    }
    job.inPlace = job.pages > scan.freePages;
    job.oldSlot = (u8)(scan.slot >= 0 ? scan.slot : scan.freeSlot);
    job.newSlot = scan.slot < 0;
    job.seq = job.inPlace ? seqInPlace : own ? seqSave : seqSaveNew;
    return 0;
}

/* Runs the steps of the sequence up to the next transfer. Returns 1 when the operation ended. */
static int RunSeq(void) {
    for (;;) {
        u8 *e = tabNotes + job.oldSlot * BLOCK;

        switch (job.seq[job.pos++]) {
            case OP_PLAN:
                job.planLen = 0;
                for (int p = FIRST_DATA_PAGE; p < PAGES && job.planLen < job.pages; p++) {
                    if (Entry(p) == PAGE_FREE && !foreign[p]) {
                        job.plan[job.planLen++] = (u8)p;
                    }
                }
                if (job.planLen < job.pages) {
                    Finish(job.risky ? CPAK_ERROR_NOTE : CPAK_NO_ROOM);
                    return 1;
                }
                break;
            case OP_DATA:
                StartXfer(1, job.plan, 0, job.planLen * 8, noteBuf);
                return 0;
            case OP_LINK:
                for (int i = 0; i < job.planLen; i++) {
                    Put16(tabIndex + job.plan[i] * 2, i + 1 < job.planLen ? job.plan[i + 1] : PAGE_LAST);
                }
                IndexChecksum();
                break;
            case OP_FREE_OLD:
                for (int i = 0; i < scan.chainLen; i++) {
                    Put16(tabIndex + scan.chain[i] * 2, PAGE_FREE);
                }
                IndexChecksum();
                break;
            case OP_IDX1:
                StartXfer(1, pageList1, 0, 8, tabIndex);
                return 0;
            case OP_IDX2:
                StartXfer(1, pageList2, 0, 8, tabIndex);
                return 0;
            case OP_ENTRY_SET:
                Fill(e, 0, BLOCK);
                Put32(e, NOTE_CODE);
                Put16(e + 4, NOTE_COMPANY);
                Put16(e + 6, job.plan[0]);
                e[8] = NOTE_STATUS;
                NoteName(e + 0x10);
                break;
            case OP_ENTRY_CLEAR:
                /* From here a save over the note's own pages can lose the note. */
                job.risky = job.op == CPAK_REQ_SAVE;
                Fill(e, 0, BLOCK);
                break;
            case OP_ENTRY_W:
                StartXfer(1, pageList34, job.oldSlot, job.oldSlot + 1, e);
                return 0;
            case OP_FMT_PREP:
                Fill(tabNotes, 0, sizeof(tabNotes));
                MakeIndex(tabIndex);
                Fill(foreign, 0, sizeof(foreign));
                MakeId(job.id);
                job.idOff = ID_OFFSET;
                /* The page holds four copies of the ID block, and nothing else. */
                Fill(tabPage0, 0, PAGE);
                for (int i = 0; i < 4; i++) {
                    Copy(tabPage0 + idOffsets[i], job.id, BLOCK);
                }
                break;
            case OP_NOTES_W:
                StartXfer(1, pageList34, 0, NOTES, tabNotes);
                return 0;
            case OP_REPAIR_PREP:
                for (int i = 0; i < 4; i++) {
                    Copy(tabPage0 + idOffsets[i], job.id, BLOCK);
                }
                job.idOff = ID_OFFSET;
                break;
            case OP_ID_W:
                StartXfer(1, pageList0, 0, 8, tabPage0);
                return 0;
            case OP_VERIFY:
                job.vstep = 0;
                job.phase = PH_VERIFY;
                StartXfer(0, pageList0, job.idOff / BLOCK, job.idOff / BLOCK + 1, scratch);
                return 0;
            case OP_DONE:
                switch (job.op) {
                    case CPAK_REQ_SAVE:
                        cpak.hasNote = 1;
                        cpak.noteBytes = (u16)(job.planLen * PAGE);
                        cpak.freePages = CountFree();
                        cpak.canRepair = job.idBad; /* the save wrote both index tables */
                        if (job.newSlot && cpak.freeNotes > 0) {
                            cpak.freeNotes--;
                        }
                        Finish(job.recordingLeftOut ? CPAK_SAVED_NO_REC : CPAK_SAVED);
                        break;
                    case CPAK_REQ_DELETE:
                        cpak.hasNote = 0;
                        cpak.noteBytes = 0;
                        cpak.freePages = CountFree();
                        cpak.canRepair = job.idBad;
                        cpak.freeNotes++;
                        Finish(CPAK_DELETED);
                        break;
                    case CPAK_REQ_REPAIR:
                        cpak.canRepair = 0;
                        Finish(CPAK_REPAIRED);
                        break;
                    default:
                        cpak.canFormat = 0;
                        cpak.canRepair = 0;
                        cpak.checked = 1;
                        cpak.hasNote = 0;
                        cpak.noteBytes = 0;
                        cpak.freeNotes = NOTES;
                        cpak.freePages = PAGES - FIRST_DATA_PAGE;
                        Finish(CPAK_FORMATTED);
                        break;
                }
                return 1;
            default:
                Finish(CPAK_ERROR);
                return 1;
        }
    }
}

/* Runs the next step of the operation. Returns 1 when the operation ended. */
static int Advance(void) {
    int r;

    switch (job.phase) {
        case PH_PROBE:
            r = PakPresent(job.ch);
            if (r == 0) {
                /* Bank 0, as libultra selects it before it uses the pak (32 bytes of 0 to 0x8000). A failure does
                 * not matter for a pak of one bank. */
                u8 zero[BLOCK];

                Fill(zero, 0, BLOCK);
                WriteBlock(job.ch, 0x8000, zero);
                r = Identify(job.ch);
            }
            if (r != 0) {
                cpak.canFormat = 0;
                cpak.canRepair = 0;
                Finish(r == PIF_NOT_PAK ? CPAK_NOT_A_PAK : FailStatus(r));
                return 1;
            }
            job.risky = 0;
            Fill(tabPage0, 0, PAGE);
            StartXfer(0, pageList0, 0, 8, tabPage0);
            job.phase = PH_ID;
            return 0;
        case PH_ID: {
            int good = -1;

            if (xfer.result != 0) {
                Finish(FailStatus(xfer.result));
                return 1;
            }
            /* One valid copy of the ID block is enough. */
            job.damaged = 0;
            for (int i = 0; i < 4; i++) {
                if (IdValid(tabPage0 + idOffsets[i])) {
                    good = good < 0 ? i : good;
                } else {
                    job.damaged = 1;
                }
            }
            job.idBad = job.damaged;
            if (good < 0) {
                if (job.op == CPAK_REQ_FORMAT) {
                    job.seq = seqFormat;
                    job.pos = 0;
                    job.phase = PH_SEQ;
                    return 0;
                }
                cpak.canFormat = 1;
                cpak.canRepair = 0;
                Finish(CPAK_NOT_FORMATTED);
                return 1;
            }
            job.idOff = idOffsets[good];
            Copy(job.id, tabPage0 + job.idOff, BLOCK);
            if (job.id[0x1A] != 1) {
                cpak.canFormat = 0;
                cpak.canRepair = 0;
                Finish(CPAK_UNSUPPORTED);
                return 1;
            }
            StartXfer(0, pageList1, 0, 8, tabIndex);
            job.phase = PH_INDEX;
            return 0;
        }
        case PH_INDEX:
            if (xfer.result != 0) {
                Finish(FailStatus(xfer.result));
                return 1;
            }
            job.idx1Ok = IndexValid(tabIndex);
            if (!job.idx1Ok) {
                job.damaged = 1;
            }
            if (job.idx1Ok && job.op != CPAK_REQ_CHECK && job.op != CPAK_REQ_REPAIR) {
                job.phase = PH_NOTES;
                return 0;
            }
            /* The backup is read when the first copy is bad, and by a check, which looks for damage. */
            StartXfer(0, pageList2, 0, 8, scratch);
            job.phase = PH_INDEX2;
            return 0;
        case PH_INDEX2: {
            int ok2;

            if (xfer.result != 0) {
                Finish(FailStatus(xfer.result));
                return 1;
            }
            ok2 = IndexValid(scratch);
            if (job.idx1Ok) {
                if (!ok2 || !Same(tabIndex, scratch, PAGE)) {
                    job.damaged = 1;
                }
            } else if (ok2) {
                Copy(tabIndex, scratch, PAGE);
            } else {
                if (job.op == CPAK_REQ_FORMAT) {
                    job.seq = seqFormat;
                    job.pos = 0;
                    job.phase = PH_SEQ;
                    return 0;
                }
                cpak.canFormat = 1;
                cpak.canRepair = 0;
                Finish(CPAK_NOT_FORMATTED);
                return 1;
            }
            job.phase = PH_NOTES;
            return 0;
        }
        case PH_NOTES:
            cpak.canRepair = job.damaged ? 1 : (job.op == CPAK_REQ_CHECK ? 0 : cpak.canRepair);
            cpak.canFormat = 0;
            if (job.op == CPAK_REQ_FORMAT) {
                Finish(CPAK_ALREADY);
                return 1;
            }
            if (job.op == CPAK_REQ_REPAIR) {
                if (!job.damaged) {
                    cpak.canRepair = 0;
                    Finish(CPAK_ALREADY);
                    return 1;
                }
                /* The index table in tabIndex is the valid one. */
                job.seq = seqRepair;
                job.pos = 0;
                job.phase = PH_SEQ;
                return 0;
            }
            StartXfer(0, pageList34, 0, NOTES, tabNotes);
            job.phase = PH_SCAN;
            return 0;
        case PH_SCAN:
            if (xfer.result != 0) {
                Finish(FailStatus(xfer.result));
                return 1;
            }
            Scan();
            switch (job.op) {
                case CPAK_REQ_CHECK:
                    Finish(cpak.hasNote ? CPAK_FOUND : CPAK_EMPTY);
                    return 1;
                case CPAK_REQ_SAVE:
                    r = PlanSave();
                    if (r != 0) {
                        Finish((u8)r);
                        return 1;
                    }
                    job.pos = 0;
                    job.phase = PH_SEQ;
                    return 0;
                case CPAK_REQ_LOAD:
                    if (scan.chainLen == 0) {
                        Finish(CPAK_NO_NOTE);
                        return 1;
                    }
                    Copy(job.plan, scan.chain, scan.chainLen);
                    StartXfer(0, job.plan, 0, 8, noteBuf);
                    job.phase = PH_LOAD_FIRST;
                    return 0;
                case CPAK_REQ_DELETE:
                    if (scan.problem) {
                        Finish(CPAK_INCONSISTENT);
                        return 1;
                    }
                    if (scan.chainLen == 0) {
                        Finish(CPAK_NO_NOTE);
                        return 1;
                    }
                    job.oldSlot = (u8)scan.slot;
                    job.newSlot = 0;
                    job.seq = seqDelete;
                    job.pos = 0;
                    job.phase = PH_SEQ;
                    return 0;
            }
            Finish(CPAK_ERROR);
            return 1;
        case PH_SEQ:
            if (xfer.result != 0) {
                Fail(xfer.result);
                return 1;
            }
            return RunSeq();
        case PH_VERIFY: {
            /* Reads back the ID block, both index tables and, for a save or a delete, the note table entry. */
            int step = job.vstep++;
            int same;
            int more = 1;

            if (xfer.result != 0) {
                Fail(xfer.result);
                return 1;
            }
            if (step == 0) {
                same = Same(scratch, job.id, BLOCK);
                StartXfer(0, pageList1, 0, 8, scratch);
            } else if (step == 1) {
                same = Same(scratch, tabIndex, PAGE);
                StartXfer(0, pageList2, 0, 8, scratch);
            } else if (step == 2) {
                same = Same(scratch, tabIndex, PAGE);
                if (job.op == CPAK_REQ_SAVE || job.op == CPAK_REQ_DELETE) {
                    StartXfer(0, pageList34, job.oldSlot, job.oldSlot + 1, scratch);
                } else {
                    more = 0;
                }
            } else {
                same = Same(scratch, tabNotes + job.oldSlot * BLOCK, BLOCK);
                more = 0;
            }
            if (!same) {
                Fail(PIF_ERROR);
                return 1;
            }
            if (!more) {
                job.phase = PH_SEQ;
            }
            return 0;
        }
        case PH_LOAD_FIRST: {
            u32 used;

            if (xfer.result != 0) {
                Finish(FailStatus(xfer.result));
                return 1;
            }
            used = NoteHeader();
            if (used == 0) {
                Finish(CPAK_BAD_NOTE);
                return 1;
            }
            if (used > PAGE) {
                StartXfer(0, job.plan, 8, PagesFor(used) * 8, noteBuf + PAGE);
                job.phase = PH_LOAD_REST;
                return 0;
            }
            job.phase = PH_LOAD_REST;
            xfer.result = 0;
            return 0;
        }
        case PH_LOAD_REST: {
            u32 used = Be16(noteBuf + 6);

            if (xfer.result != 0) {
                Finish(FailStatus(xfer.result));
                return 1;
            }
            if (!NoteValid(used)) {
                Finish(CPAK_BAD_NOTE);
                return 1;
            }
            ApplyNote();
            Finish(CPAK_LOADED);
            return 1;
        }
    }
    Finish(CPAK_ERROR);
    return 1;
}

void CPakService(void) {
    u32 start;

    if (!(cpak.request | cpak.working)) {
        return;
    }
    if (eepromMessageQueue[0] != 0) {
        return;
    }
    start = osGetCount();
    if (job.op == 0) {
        job.op = cpak.request;
        cpak.request = 0;
        cpak.working = 1;
        cpak.status = CPAK_NONE;
        job.ch = cpak.port;
        job.phase = PH_PROBE;
        xfer.active = 0;
    }
    for (;;) {
        if (xfer.active) {
            if (!Pump(start)) {
                return;
            }
            xfer.active = 0;
        }
        if (osGetCount() - start >= BUDGET) {
            return;
        }
        if (Advance() && !cpak.working) {
            return;
        }
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * The view
 */

#define VIEW_X 16
#define VIEW_Y 28
#define VIEW_W 288
#define ROW_H 10

enum { ROW_PORT, ROW_CHECK, ROW_SAVE, ROW_LOAD, ROW_RECORDING, ROW_DELETE, ROW_REPAIR, ROW_FORMAT, ROW_COUNT };

static const char *const rowNames[ROW_COUNT] = {
    "Controller with the pak", "Check the pak", "Save to the pak", "Load from the pak", "Save the recording too",
    "Delete the practice note", "Repair the pak", "Format the pak",
};

static const char *const statusText[CPAK_STATUS_COUNT] = {
    "", "No Controller Pak in the controller.", "The pak is not formatted. Nothing was changed.",
    "Not enough room on the pak.", "Saved to the pak.", "Saved without the recording: it does not fit.",
    "Loaded from the pak.", "The pak has no practice note.", "Formatted.", "Practice note deleted.",
    "A transfer failed. Try again.", "The note is damaged or from another version. Nothing was loaded.",
    "The pak is fine. The practice note is on it.", "The pak is fine. It has no practice note.",
    "This pak has more than one bank. Not supported.", "The pak's tables are valid. Nothing was changed.",
    "This is not a Controller Pak. Nothing was changed.", "The pak's notes overlap or repeat. Nothing was changed.",
    "The save failed. The practice note may be damaged.", "The damaged copies were written again.",
};

static void Ask(int op) {
    cpak.request = (u8)op;
    cpak.status = CPAK_NONE;
    cpak.working = 1;
}

static void ViewOpen(void) {
    cpak.confirm = 0;
    cpak.cursor = ROW_CHECK;
    if (!cpak.working) {
        Ask(CPAK_REQ_CHECK);
    }
}

static int ViewInput(u16 pressed, u16 held) {
    (void)held;
    if (cpak.confirm) {
        if (pressed & CONT_A) {
            int op = cpak.confirm;

            cpak.confirm = 0;
            if (!cpak.working) {
                Ask(op);
            }
        } else if (pressed) {
            cpak.confirm = 0;
        }
        return 1;
    }
    if (pressed & CONT_B) {
        /* A running operation keeps the view open: a load must not apply its settings after the view closed. */
        return cpak.working;
    }
    if (pressed & CONT_UP) {
        cpak.cursor = (u8)((cpak.cursor + ROW_COUNT - 1) % ROW_COUNT);
    }
    if (pressed & CONT_DOWN) {
        cpak.cursor = (u8)((cpak.cursor + 1) % ROW_COUNT);
    }
    if (pressed & (CONT_LEFT | CONT_RIGHT)) {
        if (cpak.cursor == ROW_PORT && !cpak.working) {
            cpak.port ^= 1;
            cpak.checked = 0;
            cpak.canFormat = 0;
            cpak.canRepair = 0;
            cpak.status = CPAK_NONE;
        } else if (cpak.cursor == ROW_RECORDING) {
            cpak.withRecording ^= 1;
        }
    }
    if (pressed & CONT_A) {
        if (cpak.working) {
            return 1;
        }
        switch (cpak.cursor) {
            case ROW_PORT:
                cpak.port ^= 1;
                cpak.checked = 0;
                cpak.canFormat = 0;
                cpak.canRepair = 0;
                cpak.status = CPAK_NONE;
                break;
            case ROW_CHECK:
                Ask(CPAK_REQ_CHECK);
                break;
            case ROW_SAVE:
                cpak.confirm = CPAK_REQ_SAVE;
                break;
            case ROW_LOAD:
                Ask(CPAK_REQ_LOAD);
                break;
            case ROW_RECORDING:
                cpak.withRecording ^= 1;
                break;
            case ROW_DELETE:
                if (cpak.hasNote) {
                    cpak.confirm = CPAK_REQ_DELETE;
                }
                break;
            case ROW_REPAIR:
                if (cpak.canRepair) {
                    cpak.confirm = CPAK_REQ_REPAIR;
                }
                break;
            case ROW_FORMAT:
                if (cpak.canFormat) {
                    cpak.confirm = CPAK_REQ_FORMAT;
                }
                break;
        }
    }
    return 1;
}

static void ViewDraw(Gfx **dl) {
    char buf[64];
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;

    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 28 + (ROW_COUNT + 5) * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    TextPrint(dl, x, VIEW_Y + 3, 1, 0xFFE070FF, "Controller Pak");
    for (int r = 0; r < ROW_COUNT; r++) {
        int row = y + r * ROW_H + 2;
        int selected = r == cpak.cursor;
        int off = (r == ROW_DELETE && !cpak.hasNote) || (r == ROW_FORMAT && !cpak.canFormat) ||
                  (r == ROW_REPAIR && !cpak.canRepair) ||
                  (r == ROW_LOAD && cpak.checked && !cpak.hasNote);

        buf[0] = '\0';
        switch (r) {
            case ROW_PORT:
                Format(buf, sizeof(buf), "%d", cpak.port + 1);
                break;
            case ROW_RECORDING:
                Format(buf, sizeof(buf), "%s", cpak.withRecording ? "yes" : "no");
                break;
            case ROW_SAVE:
                if (recording.count > 0 && cpak.withRecording) {
                    Format(buf, sizeof(buf), "%d recorded frames", recording.count);
                }
                break;
        }
        if (selected) {
            TextRect(dl, VIEW_X + 2, row - 1, VIEW_X + VIEW_W - 2, row + ROW_H - 2, 0x5070D060);
        }
        TextPrint(dl, x, row, 1, off ? 0x707888FF : selected ? 0xFFFFFFFF : 0xB0B8C8FF, rowNames[r]);
        TextPrint(dl, VIEW_X + VIEW_W - 8 - TextWidth(buf, 1), row, 1, selected ? 0xFFE070FF : 0xC0A860FF, buf);
    }
    y += (ROW_COUNT + 1) * ROW_H;
    if (cpak.checked) {
        Format(buf, sizeof(buf), "%d free pages, %d free notes", cpak.freePages, cpak.freeNotes);
        TextPrint(dl, x, y, 1, 0x8090A8FF, buf);
        if (cpak.hasNote) {
            Format(buf, sizeof(buf), "Practice note: %d bytes", cpak.noteBytes);
        } else {
            Format(buf, sizeof(buf), "No practice note");
        }
        TextPrint(dl, x, y + ROW_H, 1, 0x8090A8FF, buf);
    }
    y += 2 * ROW_H;
    if (cpak.canRepair) {
        TextPrint(dl, x, y, 1, 0xFF9060FF, "A copy of the pak's tables is damaged. Repair it.");
    }
    y += ROW_H;
    if (cpak.confirm == CPAK_REQ_SAVE) {
        TextPrint(dl, x, y, 1, 0xFF9060FF, cpak.hasNote ? "A: write over the practice note. Other: no"
                                                        : "A: add the practice note to the pak. Other: no");
    } else if (cpak.confirm == CPAK_REQ_DELETE) {
        TextPrint(dl, x, y, 1, 0xFF9060FF, "A: delete the practice note. Other: no");
    } else if (cpak.confirm == CPAK_REQ_REPAIR) {
        TextPrint(dl, x, y, 1, 0xFF9060FF, "A: write the damaged copies again. Other: no");
    } else if (cpak.confirm == CPAK_REQ_FORMAT) {
        TextPrint(dl, x, y, 1, 0xFF9060FF, "A: ERASE the whole pak. Other: no");
    } else if (cpak.working || cpak.request) {
        TextPrint(dl, x, y, 1, 0x80D080FF, "Working... B and the menu button wait.");
    } else if (cpak.status != CPAK_NONE) {
        int bad = cpak.status == CPAK_NO_PAK || cpak.status == CPAK_NOT_FORMATTED || cpak.status == CPAK_NO_ROOM ||
                  cpak.status == CPAK_ERROR || cpak.status == CPAK_BAD_NOTE || cpak.status == CPAK_UNSUPPORTED ||
                  cpak.status == CPAK_NOT_A_PAK || cpak.status == CPAK_INCONSISTENT || cpak.status == CPAK_ERROR_NOTE;

        TextPrint(dl, x, y, 1, bad ? 0xFF9060FF : 0x80D080FF, statusText[cpak.status % CPAK_STATUS_COUNT]);
    } else {
        TextPrint(dl, x, y, 1, 0x8090A8FF, "A choose  </> change  B back");
    }
    TextPrint(dl, x, y + ROW_H, 1, 0x8090A8FF, "Saving and loading can make the game lag.");
}

const View cpakView = { ViewOpen, ViewInput, ViewDraw };
