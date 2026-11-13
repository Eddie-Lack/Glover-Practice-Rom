/* Savestates on the SD card (card.h). The file format is in docs/build.md, "Savestates on the SD card". */
#include "besttimes.h"
#include "card.h"
#include "fat.h"
#include "fmt.h"
#include "game.h"
#include "ghost.h"
#include "hud.h"
#include "lz.h"
#include "recording.h"
#include "savestate.h"
#include "sd.h"

#define FOLDER "GLOVERPR"
#define HEADER_SIZE 64
#define MAGIC 0x47505353 /* "GPSS" */
#define VERSION 2

u8 cardSlot = 1;
u8 cardStatus;
u8 cardHave[CARD_SLOTS];
u8 cardScanned;
u32 cardDone;       /* requests carried out: the tests count them */
s32 cardError;      /* the last error code (fat.h or sd.h) */
u32 cardLastTicks;  /* the time of the last save or load, in CPU counter ticks (46.875 MHz) */
u32 cardLastBytes;  /* the size of the file of the last save or load */
u32 cardLastWait;   /* of cardLastTicks, the ticks spent waiting for the cart */
u8 cardProgress;    /* the progress bar's percent */
volatile u8 cardWanted;       /* the Savestate menu sets it; the tests too */
static u8 announced;
static u16 cardKib[CARD_SLOTS];
static u8 *const stage = (u8 *)CARD_STAGE;

/* The card code's data, in the spare memory at CARD_WORK. FatMount sets the file system's state each time. */
typedef struct CardWork {
    Fat fat;
    u32 crcTable[256];
    u8 sector[FAT_SECTOR] __attribute__((aligned(4)));
} CardWork;

#define work ((CardWork *)CARD_WORK)
#define fat (work->fat)
#define crcTable (work->crcTable)
_Static_assert(sizeof(CardWork) <= CARD_WORK_SIZE, "the card's data fits its memory");

_Static_assert(CARD_STAGE == STATE_SCRATCH + 2 * LZ_BLOCK_MAX, "the stage lies behind the scratch blocks");

/* ---- Small helpers -------------------------------------------------------------------------------------------- */

static u8 crcReady;

/* CRC-32 as zlib has it: CRC is the value so far (0 to start). */
static u32 Crc(u32 crc, const u8 *p, u32 n) {
    if (!crcReady) {
        for (u32 i = 0; i < 256; i++) {
            u32 c = i;

            for (int k = 0; k < 8; k++) {
                c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            crcTable[i] = c;
        }
        crcReady = 1;
    }
    crc = ~crc;
    while (n-- > 0) {
        crc = crcTable[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

static void Put32(u8 *p, u32 v) {
    p[0] = (u8)(v >> 24);
    p[1] = (u8)(v >> 16);
    p[2] = (u8)(v >> 8);
    p[3] = (u8)v;
}

static u32 Get32(const u8 *p) {
    return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3];
}

static u32 Get16(const u8 *p) {
    return (u32)p[0] << 8 | p[1];
}

/* Moves N bytes down to DST (DST below SRC; both word-aligned). */
static void MoveDown(u8 *dst, const u8 *src, u32 n) {
    u32 i = 0;

    for (; i + 4 <= n; i += 4) {
        *(u32 *)(dst + i) = *(const u32 *)(src + i);
    }
    for (; i < n; i++) {
        dst[i] = src[i];
    }
}

/* Card file 0 is the recording's file. */
static void FileName(char *buf, int n) {
    if (n == 0) {
        Format(buf, 16, "RECORD.GRC");
    } else {
        Format(buf, 16, "STATE%03d.GST", n);
    }
}

static char message[64];

static void Shown(const char *text) {
    Format(message, sizeof(message), "%s", text);
    HudMessage(message);
}

/* "2.31 s" */
static void Seconds(char *buf, int size, u32 ticks) {
    u32 ms = ticks / 46875;

    Format(buf, size, "%u.%02u s", ms / 1000, ms % 1000 / 10);
}

/* ---- The progress bar ----------------------------------------------------------------------------------------- */

/* A save and a load stop the draw thread. The HUD cannot show progress then. The bar is written into the frame buffer
 * that the video interface shows, near its bottom edge: 240 by 8 pixels. The next frame the game draws covers it. */
#define BAR_X 40
#define BAR_Y 214
#define BAR_W 240
#define BAR_H 8
#define FB_WIDTH 320

static void DrawProgress(u32 done, u32 total) {
    u32 fb = (u32)osViGetCurrentFramebuffer();
    volatile u16 *p;
    u32 filled = total != 0 ? done * BAR_W / total : 0;

    cardProgress = (u8)(total != 0 ? done * 100 / total : 0);
    if (fb < 0x80000000 || fb >= 0x80400000) {
        return;
    }
    p = (volatile u16 *)(fb | 0xA0000000);
    for (u32 y = 0; y < BAR_H; y++) {
        for (u32 x = 0; x < BAR_W; x++) {
            int edge = y == 0 || y == BAR_H - 1 || x == 0 || x == BAR_W - 1;

            p[(BAR_Y + y) * FB_WIDTH + BAR_X + x] = edge ? 0x0001 : x < filled ? 0xFFC1 : 0x39CF;
        }
    }
}

/* ---- The card ------------------------------------------------------------------------------------------------- */

/* Opens the cart's card and the file system. Returns 0, or 1 with cardStatus and cardError set. Every 0 needs an
 * SdClose. */
static int Begin(void) {
    int r = SdOpen();

    if (r == SD_OK) {
        r = FatMount(&fat, SdDevice(), stage, SD_BURST);
        if (r != FAT_OK) {
            SdClose();
            cardError = r;
            cardStatus = r == FAT_ERR_NOFS || r == FAT_ERR_UNSUPPORTED ? CARD_NO_FS : CARD_ERROR;
            return 1;
        }
        return 0;
    }
    cardError = r;
    switch (r) {
        case SD_ERR_NO_CART:
            cardStatus = CARD_NO_CART;
            break;
        case SD_ERR_NO_CARD:
            cardStatus = CARD_NO_CARD;
            break;
        case SD_ERR_WRITEBACK:
            cardStatus = CARD_WRITEBACK;
            break;
        case SD_ERR_BUFFER:
            cardStatus = CARD_READ_FAILED;
            break;
        default:
            cardStatus = CARD_ERROR;
            break;
    }
    return 1;
}

static const char *WhyNot(void) {
    switch (cardStatus) {
        case CARD_NO_CART:
            return "no flash cart found";
        case CARD_NO_CARD:
            return "no card in the cart";
        case CARD_NO_FS:
            return "card is not FAT16 or FAT32";
        case CARD_WRITEBACK:
            return "save writeback pending, try again";
        case CARD_READ_FAILED:
            return "card read failed";
        default:
            return "card error";
    }
}

/* The files of the folder: the number of each one, its size and its level. */
static int ScanFn(void *ctx, const FatEntry *e) {
    u8 *sector = work->sector;
    FatReader rd;
    int n = 0;

    (void)ctx;
    if ((e->attr & FAT_ATTR_DIR) || e->name[0] != 'S' || e->name[1] != 'T' || e->name[2] != 'A' || e->name[3] != 'T' ||
        e->name[4] != 'E' || e->name[8] != 'G' || e->name[9] != 'S' || e->name[10] != 'T') {
        return 0;
    }
    for (int i = 5; i < 8; i++) {
        if (e->name[i] < '0' || e->name[i] > '9') {
            return 0;
        }
        n = n * 10 + e->name[i] - '0';
    }
    if (n < 1 || n > CARD_SLOTS) {
        return 0;
    }
    cardKib[n - 1] = (u16)((e->size + 1023) >> 10);
    cardHave[n - 1] = 255;
    if (e->size > HEADER_SIZE && FatOpen(&fat, e, &rd) == FAT_OK && FatRead(&fat, &rd, sector, 1) == FAT_OK &&
        Get32(sector) == MAGIC && sector[8] < NUM_LEVELS) {
        cardHave[n - 1] = (u8)(sector[8] + 1);
    }
    return 0;
}

static int Scan(u32 folder) {
    int r;

    for (int i = 0; i < CARD_SLOTS; i++) {
        cardHave[i] = 0;
        cardKib[i] = 0;
    }
    r = FatList(&fat, folder, ScanFn, NULL);
    cardScanned = r == FAT_OK;
    return r;
}

static void DoCheck(void) {
    u32 folder;
    int r;

    if (Begin()) {
        return;
    }
    r = FatFolder(&fat, 0, FOLDER, 0, &folder);
    if (r == FAT_ERR_NOTFOUND) {
        for (int i = 0; i < CARD_SLOTS; i++) {
            cardHave[i] = 0;
        }
        cardScanned = 1;
        cardStatus = CARD_NO_FOLDER;
    } else {
        if (r == FAT_OK) {
            r = Scan(folder);
        }
        cardStatus = r == FAT_OK ? CARD_READY : CARD_ERROR;
        cardError = r;
    }
    SdClose();
}

static void DoDelete(void) {
    char name[16];
    u32 folder;
    int n = cardSlot;
    int r;

    if (Begin()) {
        Shown(WhyNot());
        return;
    }
    FileName(name, n);
    r = FatFolder(&fat, 0, FOLDER, 0, &folder);
    if (r == FAT_OK) {
        r = FatDelete(&fat, folder, name);
    }
    SdClose();
    if (r == FAT_OK) {
        cardHave[n - 1] = 0;
        cardKib[n - 1] = 0;
        cardStatus = CARD_READY;
        Format(message, sizeof(message), "card file %d deleted", n);
        HudMessage(message);
    } else if (r == FAT_ERR_NOTFOUND) {
        Format(message, sizeof(message), "no card file %d", n);
        HudMessage(message);
    } else {
        cardError = r;
        cardStatus = CARD_ERROR;
        Shown("card file not deleted: card error");
    }
}

/* ---- Saving a state to the card ------------------------------------------------------------------------------- */

static struct {
    FatWriter writer;
    u32 crc;   /* of the bytes taken out so far */
    u32 total; /* the bytes taken out so far, then the file's size */
    int error;
} out;

/* Takes the whole sectors out of the stream and writes them. At the last call it adds the CRC and writes all. */
static int FlushStream(CardStream *s, int last) {
    u32 bytes = s->pos & ~(u32)(FAT_SECTOR - 1);
    int r;

    if (last) {
        u32 end;

        out.crc = Crc(out.crc, s->buf, s->pos);
        Put32(s->buf + s->pos, out.crc);
        s->pos += 4;
        end = (s->pos + FAT_SECTOR - 1) & ~(u32)(FAT_SECTOR - 1);
        for (u32 i = s->pos; i < end; i++) {
            s->buf[i] = 0;
        }
        out.total += s->pos;
        r = FatWrite(&fat, &out.writer, s->buf, end / FAT_SECTOR);
        s->pos = 0;
    } else {
        out.crc = Crc(out.crc, s->buf, bytes);
        r = FatWrite(&fat, &out.writer, s->buf, bytes / FAT_SECTOR);
        MoveDown(s->buf, s->buf + bytes, s->pos - bytes);
        s->pos -= bytes;
        out.total += bytes;
        DrawProgress(s->chunk, s->chunks);
    }
    out.error = r;
    return r != FAT_OK;
}

/* Writes slot SLOT of the pool as card file N. Returns 0 or an error code of fat.h or sd.h. */
static u8 beginFailed;

/* The recording's runs follow the chunks of its file (header offset 24: the game frames; 28: the runs). */
static int AppendRecording(CardStream *s) {
    for (u32 i = 0; i < recording.count;) {
        if (s->pos + RECORDING_RUN_SIZE > s->cap && FlushStream(s, 0)) {
            return 1;
        }
        RecordingRunAt(&i, s->buf + s->pos);
        s->pos += RECORDING_RUN_SIZE;
    }
    return 0;
}

static int SaveFile(int slot, int n, u32 *size) {
    StateMeta m;
    CardStream s;
    char name[16];
    u32 folder;
    u32 runs = 0;
    int r;

    StateSlotMeta(slot, &m);
    beginFailed = Begin();
    if (beginFailed) {
        return cardError;
    }
    r = FatFolder(&fat, 0, FOLDER, 1, &folder);
    if (r != FAT_OK) {
        SdClose();
        return r;
    }
    for (int i = 0; i < HEADER_SIZE; i++) {
        stage[i] = 0;
    }
    Put32(stage, MAGIC);
    stage[4] = 0;
    stage[5] = VERSION;
    stage[6] = 0;
    stage[7] = HEADER_SIZE;
    stage[8] = m.level;
    stage[10] = (u8)(m.chunks >> 8);
    stage[11] = (u8)m.chunks;
    Put32(stage + 12, (u32)m.song);
    Put32(stage + 16, m.bytes);
    Put32(stage + 20, m.layout);
    if (n == 0) {
        u8 run[RECORDING_RUN_SIZE];

        for (u32 i = 0; i < recording.count; runs++) {
            RecordingRunAt(&i, run);
        }
        Put32(stage + 24, recording.count);
        Put32(stage + 28, runs);
    }
    s.buf = stage;
    s.pos = HEADER_SIZE;
    s.cap = CARD_STAGE_SIZE - 2 * FAT_SECTOR;
    s.flush = FlushStream;
    s.chunk = 0;
    s.chunks = m.chunks;
    out.crc = 0;
    out.total = 0;
    out.error = FAT_OK;
    FileName(name, n);
    FatWriteBegin(&fat, &out.writer);
    if (StateCardExport(slot, &s) != 0 || (n == 0 && AppendRecording(&s))) {
        r = out.error != FAT_OK ? out.error : FAT_ERR_IO;
    } else {
        r = FlushStream(&s, 1) ? out.error : FAT_OK;
    }
    if (r == FAT_OK) {
        r = FatWriteEnd(&fat, &out.writer, folder, name, out.total);
    } else {
        FatWriteAbort(&fat, &out.writer);
    }
    SdClose();
    *size = out.total;
    if (r == FAT_OK) {
        if (n > 0) {
            cardHave[n - 1] = (u8)(m.level + 1);
            cardKib[n - 1] = (u16)((out.total + 1023) >> 10);
        }
        cardStatus = CARD_READY;
    }
    return r;
}

/* REC: the recording and its state (card file 0). */
static void DoSave(int rec) {
    char secs[16];
    char what[24];
    s32 pri;
    u32 t0, wait0;
    u32 size = 0;
    int n = rec ? 0 : cardSlot;
    int slot = rec ? STATE_SLOT_RECORD : stateSlot;
    int r;

    if (!StateSlotUsed(slot) || (rec && recording.count == 0)) {
        Shown(rec ? "no recording to save" : "no state in this slot to save");
        return;
    }
    Format(what, sizeof(what), rec ? "card recording" : "card %d", n);
    pri = StateHold();
    if (pri == STATE_HOLD_FAILED) {
        Shown("not saved to card: game busy");
        return;
    }
    t0 = osGetCount();
    wait0 = sdWaitTicks;
    DrawProgress(0, 1);
    r = SaveFile(slot, n, &size);
    cardLastTicks = osGetCount() - t0;
    cardLastWait = sdWaitTicks - wait0;
    cardLastBytes = size;
    cardError = r;
    DrawProgress(r == FAT_OK, 1);
    StateResume(pri);
    Seconds(secs, sizeof(secs), cardLastTicks);
    if (r == FAT_OK) {
        Format(message, sizeof(message), "saved to %s: %u KB, %s", what, (size + 1023) >> 10, secs);
    } else if (r == FAT_ERR_FULL) {
        Format(message, sizeof(message), "not saved to card: the card is full");
    } else if (r == FAT_ERR_DIRFULL) {
        Format(message, sizeof(message), "not saved to card: no room in the folder");
    } else if (beginFailed) {
        Format(message, sizeof(message), "not saved to card: %s", WhyNot());
    } else {
        cardStatus = CARD_ERROR;
        Format(message, sizeof(message), "not saved to card: card error %d", r);
    }
    HudMessage(message);
}

/* ---- Loading a state from the card ---------------------------------------------------------------------------- */

static struct {
    FatReader reader;
    u32 sectorsLeft;
    u32 base;       /* the file's offset of the stream's first byte */
    u32 payloadEnd; /* the file's offset after the last record: the file's size less the CRC */
} in;

static int FillSource(CardSource *s) {
    u32 drop = s->pos & ~3u;
    u32 n;
    int r;

    MoveDown(s->buf, s->buf + drop, s->len - drop);
    s->pos -= drop;
    s->len -= drop;
    in.base += drop;
    n = (s->cap - s->len) / FAT_SECTOR;
    if (n > in.sectorsLeft) {
        n = in.sectorsLeft;
    }
    if (n > 0) {
        r = FatRead(&fat, &in.reader, s->buf + s->len, n);
        if (r != FAT_OK) {
            cardError = r;
            return 1;
        }
        in.sectorsLeft -= n;
        s->len += n * FAT_SECTOR;
    }
    if (in.base + s->len >= in.payloadEnd) {
        s->len = in.payloadEnd - in.base;
        s->eof = 1;
    }
    /* The second half of the bar: CheckCrc filled the first. */
    DrawProgress(s->chunks + s->chunk, 2 * s->chunks);
    return 0;
}

/* Reads the whole file once and compares its CRC. */
static int CheckCrc(const FatEntry *e) {
    FatReader rd;
    u32 sectors = (e->size + FAT_SECTOR - 1) / FAT_SECTOR;
    u32 off = 0;
    u32 crc = 0;
    u8 tail[4];
    int r = FatOpen(&fat, e, &rd);

    while (r == FAT_OK && sectors > 0) {
        u32 n = sectors < CARD_STAGE_SIZE / FAT_SECTOR ? sectors : CARD_STAGE_SIZE / FAT_SECTOR;
        u32 bytes = n * FAT_SECTOR;

        r = FatRead(&fat, &rd, stage, n);
        if (r != FAT_OK) {
            break;
        }
        if (off + bytes > e->size) {
            bytes = e->size - off;
        }
        for (u32 i = 0; i < bytes; i++) {
            if (off + i >= e->size - 4) {
                tail[off + i - (e->size - 4)] = stage[i];
            }
        }
        if (off + bytes > e->size - 4) {
            bytes = off >= e->size - 4 ? 0 : e->size - 4 - off;
        }
        crc = Crc(crc, stage, bytes);
        off += n * FAT_SECTOR;
        sectors -= n;
        DrawProgress(off, e->size * 2);
    }
    if (r != FAT_OK) {
        cardError = r;
        return -1;
    }
    return crc == Get32(tail) ? 0 : 1;
}

enum {
    LOAD_OK,
    LOAD_NO_FILE,
    LOAD_BAD_FILE,   /* not a state file, damaged, or from another build */
    LOAD_FAILED,     /* the card failed */
    LOAD_NOT_LOADED, /* the state code refused (a title, the EEPROM) */
    LOAD_DAMAGED,    /* failed after the game's memory changed */
    LOAD_TOO_LONG,   /* a recording file with more than RECORDING_MAX game frames: nothing was loaded */
};

/* Reads the first sector of the file and compares its header with this build. Returns 0, 1 for a file that is not a
 * usable state, or -1 if the card failed. */
static u32 headerFrames; /* the game frames that the header of the file says it records */

static int CheckHeader(const FatEntry *e, StateMeta *m) {
    StateMeta mine;
    FatReader rd;
    u8 *h = work->sector;
    int r = FatOpen(&fat, e, &rd);

    if (r == FAT_OK) {
        r = FatRead(&fat, &rd, h, 1);
    }
    if (r != FAT_OK) {
        cardError = r;
        return -1;
    }
    StateLayout(&mine);
    m->level = h[8];
    m->chunks = Get16(h + 10);
    m->song = (s32)Get32(h + 12);
    m->bytes = Get32(h + 16);
    m->layout = Get32(h + 20);
    headerFrames = Get32(h + 24);
    return Get32(h) != MAGIC || h[5] != VERSION || h[7] != HEADER_SIZE || m->level >= NUM_LEVELS ||
                   m->chunks != mine.chunks || m->bytes != mine.bytes || m->layout != mine.layout
               ? 1
               : 0;
}

/* The runs of a recording file, after the chunks. Returns 0, or 1 if they are not all there. */
static int LoadRecording(CardSource *src, u32 frames, u32 runs) {
    recording.count = 0;
    for (u32 r = 0; r < runs; r++) {
        if (src->len - src->pos < RECORDING_RUN_SIZE && !src->eof && src->fill(src) != 0) {
            return 1;
        }
        if (src->len - src->pos < RECORDING_RUN_SIZE) {
            return 1;
        }
        RecordingAppend(src->buf + src->pos);
        src->pos += RECORDING_RUN_SIZE;
    }
    return recording.count != frames;
}

static u8 recordingBad; /* 1: the file's state loaded but its recording is not whole */

static int LoadFile(int n, StateMeta *m) {
    u32 frames, runs;
    CardSource src;
    FatEntry e;
    char name[16];
    char label[16];
    u32 folder;
    int r;

    if (Begin()) {
        return LOAD_FAILED;
    }
    FileName(name, n);
    r = FatFolder(&fat, 0, FOLDER, 0, &folder);
    if (r == FAT_OK) {
        r = FatFind(&fat, folder, name, &e);
    }
    if (r == FAT_ERR_NOTFOUND) {
        SdClose();
        return LOAD_NO_FILE;
    }
    if (r != FAT_OK || e.size < HEADER_SIZE + 4) {
        SdClose();
        cardError = r;
        return r != FAT_OK ? LOAD_FAILED : LOAD_BAD_FILE;
    }
    /* The header first: a file from another build is refused before the long pass over the CRC. */
    r = CheckHeader(&e, m);
    if (r == 0 && n == 0 && headerFrames > RECORDING_MAX) {
        SdClose();
        return LOAD_TOO_LONG;
    }
    if (r == 0) {
        r = CheckCrc(&e);
    }
    if (r != 0) {
        SdClose();
        return r < 0 ? LOAD_FAILED : LOAD_BAD_FILE;
    }
    /* The records. */
    in.base = 0;
    in.payloadEnd = e.size - 4;
    in.sectorsLeft = (e.size + FAT_SECTOR - 1) / FAT_SECTOR;
    src.buf = stage;
    src.pos = 0;
    src.len = 0;
    src.cap = CARD_STAGE_SIZE;
    src.eof = 0;
    src.fill = FillSource;
    src.chunk = 0;
    if (FatOpen(&fat, &e, &in.reader) != FAT_OK || FillSource(&src) != 0) {
        SdClose();
        return LOAD_FAILED;
    }
    src.chunks = m->chunks;
    src.pos = HEADER_SIZE;
    frames = Get32(stage + 24);
    runs = Get32(stage + 28);
    Format(label, sizeof(label), n ? "card %d" : "recording", n);
    r = StateCardLoad(m, &src, label);
    recordingBad = 0;
    if (n == 0 && r == 1) {
        recordingBad = LoadRecording(&src, frames, runs);
    }
    SdClose();
    return r == 1 ? LOAD_OK : r == 0 ? LOAD_NOT_LOADED : LOAD_DAMAGED;
}

static void DoLoad(int rec) {
    StateMeta m;
    char secs[16];
    u32 t0, wait0;
    int n = rec ? 0 : cardSlot;
    int r;

    if (!StateSaveReady()) {
        Shown("no states on a title");
        return;
    }
    t0 = osGetCount();
    wait0 = sdWaitTicks;
    DrawProgress(0, 1);
    if (rec) {
        recording.recording = recording.playing = 0;
    }
    r = LoadFile(n, &m);
    cardLastTicks = osGetCount() - t0;
    cardLastWait = sdWaitTicks - wait0;
    Seconds(secs, sizeof(secs), cardLastTicks);
    switch (r) {
        case LOAD_OK:
            cardLastBytes = in.payloadEnd + 4;
            if (rec) {
                /* The loaded state is the recording's: it goes into the recording's slot. */
                if (recordingBad || !StateSaveSlot(STATE_SLOT_RECORD)) {
                    recording.count = 0;
                    Format(message, sizeof(message), "recording not kept: the file or the pool is bad");
                } else {
                    Format(message, sizeof(message), "loaded the recording: %u frames, %s", recording.count, secs);
                }
                GhostClear();
            } else {
                Format(message, sizeof(message), "loaded card %d: %s, %s", n, levelInfo[m.level].name, secs);
            }
            break;
        case LOAD_NO_FILE:
            Format(message, sizeof(message), n ? "no card file %d" : "no recording file on the card", n);
            break;
        case LOAD_BAD_FILE:
            Format(message, sizeof(message), n ? "card file %d is not a usable state" : "the recording file is not usable", n);
            break;
        case LOAD_TOO_LONG:
            Format(message, sizeof(message), "the recording file is longer than 30 minutes: not loaded");
            break;
        case LOAD_FAILED:
            Format(message, sizeof(message), "not loaded: %s", cardStatus == CARD_READY || cardStatus == CARD_UNCHECKED ? "card error" : WhyNot());
            break;
        case LOAD_NOT_LOADED:
            return;
        default:
            Format(message, sizeof(message), "card read failed: the game's memory is damaged");
            break;
    }
    HudMessage(message);
}

/* ---- The practice best times ---------------------------------------------------------------------------------- */

/* BESTS.GRB, 72 bytes: "GPBT", the version (1) at byte 4, the 30 times as big-endian u16 from byte 8, and the CRC-32
 * of the 68 bytes before it. */
#define BESTS_NAME "BESTS.GRB"
#define BESTS_SIZE (8 + BEST_LEVELS * 2 + 4)
#define BESTS_MAGIC 0x47504254 /* "GPBT" */
#define BESTS_BAD 1 /* a result of DoBests: the file is not ours or is damaged */

static void DoBests(int save) {
    FatEntry e;
    FatReader rd;
    FatWriter w;
    u8 *b = stage;
    u32 folder;
    int r;

    if (Begin()) {
        Shown(WhyNot());
        return;
    }
    r = FatFolder(&fat, 0, FOLDER, save, &folder);
    if (save && r == FAT_OK) {
        for (int i = 0; i < FAT_SECTOR; i++) {
            b[i] = 0;
        }
        Put32(b, BESTS_MAGIC);
        b[4] = 1;
        for (int i = 0; i < BEST_LEVELS; i++) {
            b[8 + 2 * i] = (u8)(practiceBests.frames[i] >> 8);
            b[9 + 2 * i] = (u8)practiceBests.frames[i];
        }
        Put32(b + BESTS_SIZE - 4, Crc(0, b, BESTS_SIZE - 4));
        FatWriteBegin(&fat, &w);
        r = FatWrite(&fat, &w, b, 1);
        if (r == FAT_OK) {
            r = FatWriteEnd(&fat, &w, folder, BESTS_NAME, BESTS_SIZE);
        } else {
            FatWriteAbort(&fat, &w);
        }
    } else if (r == FAT_OK) {
        r = FatFind(&fat, folder, BESTS_NAME, &e);
        if (r == FAT_OK && e.size == BESTS_SIZE) {
            r = FatOpen(&fat, &e, &rd);
            if (r == FAT_OK) {
                r = FatRead(&fat, &rd, b, 1);
            }
        } else if (r == FAT_OK) {
            r = BESTS_BAD;
        }
    }
    SdClose();
    if (r == FAT_OK && !save) {
        if (Get32(b) != BESTS_MAGIC || b[4] != 1 || Crc(0, b, BESTS_SIZE - 4) != Get32(b + BESTS_SIZE - 4)) {
            r = BESTS_BAD;
        } else {
            for (int i = 0; i < BEST_LEVELS; i++) {
                practiceBests.frames[i] = (u16)Get16(b + 8 + 2 * i);
            }
        }
    }
    if (r == FAT_OK) {
        Shown(save ? "saved the best times to the card" : "loaded the best times from the card");
    } else if (r == FAT_ERR_NOTFOUND) {
        Shown("no best times file on the card");
    } else if (r == BESTS_BAD) {
        Shown("the best times file is not usable");
    } else {
        cardError = r;
        Shown("best times not done: card error");
    }
}

/* ---- Requests ------------------------------------------------------------------------------------------------- */

void CardRequest(int request) {
    cardWanted = (u8)request;
}

int CardPending(void) {
    return cardWanted != CARD_REQ_NONE;
}

void CardGameFrame(void) {
    int request = cardWanted;

    if (request == CARD_REQ_NONE) {
        return;
    }
    if ((request == CARD_REQ_SAVE || request == CARD_REQ_LOAD || request == CARD_REQ_SAVE_REC ||
         request == CARD_REQ_LOAD_REC) && !announced) {
        announced = 1;
        HudMessage(request == CARD_REQ_SAVE || request == CARD_REQ_SAVE_REC ? "saving to card..." : "loading from card...");
        return;
    }
    announced = 0;
    cardWanted = CARD_REQ_NONE;
    switch (request) {
        case CARD_REQ_CHECK:
            DoCheck();
            break;
        case CARD_REQ_SAVE:
        case CARD_REQ_SAVE_REC:
            DoSave(request == CARD_REQ_SAVE_REC);
            break;
        case CARD_REQ_LOAD:
        case CARD_REQ_LOAD_REC:
            DoLoad(request == CARD_REQ_LOAD_REC);
            break;
        case CARD_REQ_SAVE_BESTS:
        case CARD_REQ_LOAD_BESTS:
            DoBests(request == CARD_REQ_SAVE_BESTS);
            break;
        case CARD_REQ_DELETE:
            DoDelete();
            break;
    }
    cardDone++;
}

/* ---- The menu's text ------------------------------------------------------------------------------------------ */

void CardStatusText(char *buf, int size, int arg) {
    int files = 0;
    u32 kib = 0;

    (void)arg;
    for (int i = 0; i < CARD_SLOTS; i++) {
        if (cardHave[i] != 0) {
            files++;
            kib += cardKib[i];
        }
    }
    switch (cardStatus) {
        case CARD_UNCHECKED:
            Format(buf, size, "not checked");
            break;
        case CARD_NO_CART:
            Format(buf, size, "no flash cart found");
            break;
        case CARD_NO_CARD:
            Format(buf, size, "%s: no card", SdName());
            break;
        case CARD_NO_FS:
            Format(buf, size, "%s: not FAT16 or FAT32", SdName());
            break;
        case CARD_NO_FOLDER:
            Format(buf, size, "%s: no folder yet", SdName());
            break;
        case CARD_WRITEBACK:
            Format(buf, size, "%s: save writeback pending, try again", SdName());
            break;
        case CARD_READ_FAILED:
            Format(buf, size, "%s: card read failed", SdName());
            break;
        case CARD_READY:
            if (cardScanned) {
                Format(buf, size, "%s: %d file%s, %u KB", SdName(), files, files == 1 ? "" : "s", kib);
            } else {
                Format(buf, size, "%s: ready", SdName());
            }
            break;
        default:
            Format(buf, size, "%s: card error %d", SdName(), cardError);
            break;
    }
}

void CardFileText(char *buf, int size, int arg) {
    int n = cardSlot - 1;

    (void)arg;
    if (!cardScanned && cardHave[n] == 0) {
        Format(buf, size, "use Check card");
    } else if (cardHave[n] == 0) {
        Format(buf, size, "empty");
    } else if (cardHave[n] == 255) {
        Format(buf, size, "not a state file");
    } else {
        Format(buf, size, "%s, %u KB", levelInfo[cardHave[n] - 1].name, cardKib[n]);
    }
}
