/* Savestates: a copy of the game's own memory, saved and loaded at the start of a game frame, kept compressed in a
 * pool of slots (savestate.h). */
#include "actions.h"
#include "besttimes.h"
#include "corrupt.h"
#include "earlytitle.h"
#include "fmt.h"
#include "game.h"
#include "hud.h"
#include "lz.h"
#include "noclip.h"
#include "practice.h"
#include "safe.h"
#include "savestate.h"
#include "settings.h"
#include "state_ranges.h"

_Static_assert(STATE_POOL_END <= 0x80800000, "the pool must fit in the Expansion Pak");

/* Above the draw thread (20) and the scheduler's graphics thread (100), below the scheduler's audio thread (110):
 * the sound goes on during the copy. The controller thread (0x73) is put below the copy for its length. */
#define COPY_PRIORITY 109
#define CONTROLLER_HELD_PRIORITY 1

u8 stateSlot;
u8 stateSaved;
u32 stateLoads;

/* ---- The pool ----------------------------------------------------------------------------------------------- */

/* A block of the pool. The items lie one after the other from STATE_POOL; a freed item closes its gap. */
typedef struct Item {
    u8 *addr;
    u32 size; /* a multiple of 4; 0: not in use */
} Item;

typedef struct Base {
    Item item;
    u8 level;
} Base;

typedef struct Slot {
    Item item;
    u8 base;  /* the base it differs from */
    u8 level; /* the level it was saved in */
    s32 song; /* the song that played at the save */
    u32 kib;  /* its size */
    u32 starts; /* for the automatic slot: practice.levelStarts of the level start that it holds */
} Slot;

Base stateBases[STATE_BASES];
Slot stateSlots[STATE_SLOTS + 1]; /* the last one holds a slot's old item while a new one replaces it */
u8 *statePoolEnd = (u8 *)STATE_POOL; /* the first free byte */
_Static_assert(sizeof(Slot) == 24 && __builtin_offsetof(Slot, base) == 8 && __builtin_offsetof(Slot, level) == 9 &&
                   sizeof(Base) == 12 && __builtin_offsetof(Base, level) == 8,
               "tests/slots.py has the offsets");

/* The measurements of the last save and load, in CPU counter ticks (46.875 MHz on the console). The tests read them. */
u32 stateSaveTicks;
u32 stateLoadTicks;
u32 stateSaveBytes; /* the bytes that the last save added to the pool */
u32 stateSaves;     /* saves done, also the automatic ones */
s32 stateSavedFrame; /* frameCount at the last successful save; the tests read it */
u32 stateChanged;   /* the chunks of the last save that were not stored as "the same" */
u8 stateQuick;      /* 1 if the last save stored its changed chunks as they are */
u8 stateVerify;     /* a test sets this: each save is decoded again and compared with the game's memory */
u32 stateVerifyBad; /* words that differed, over all saves */
u32 stateVerifyOutside; /* those outside VERIFY_VOLATILE */
u32 stateVerifyList[8]; /* the addresses of the first of them in the last save */

/* The audio code's buffers, in the last range of the game's bss before the music driver's. The audio thread writes
 * there while a save is read back, and a few words may differ. */
#define VERIFY_VOLATILE_START 0x80296C38
#define VERIFY_VOLATILE_END 0x802AB010

/* A base keeps a 64-bit hash of each chunk of its state (LzHash). A chunk of the live state with the same hash as
 * the base's is stored as "the same" without decoding the base's chunk: most chunks of a state are. A load also
 * skips the base's chunk when the live chunk has its hash already. */
#define STATE_CHUNKS_MAX 160
_Static_assert(STATE_CHUNKS <= STATE_CHUNKS_MAX, "baseSums has room for STATE_CHUNKS_MAX chunks");
_Static_assert(STATE_CHUNK_BYTES == LZ_BLOCK_MAX, "tools/mkstate.py counts the chunks of LZ_BLOCK_MAX bytes");
static u32 baseSums[STATE_BASES][STATE_CHUNKS_MAX][2];
static u32 newSums[STATE_CHUNKS_MAX][2]; /* the base that WriteState(-1) is making, or the live state's */

static u16 lzWork[LZ_WORK_ENTRIES] __attribute__((aligned(8)));
#define blockA ((u8 *)STATE_SCRATCH)
#define blockB ((u8 *)(STATE_SCRATCH + LZ_BLOCK_MAX))
_Static_assert(STATE_SCRATCH + 2 * LZ_BLOCK_MAX <= 0x80700000, "the scratch blocks must stay clear of the ghosts");

static u32 autoStart;            /* practice.levelStarts at the last automatic save */

/* A chunk is a piece of the state of at most LZ_BLOCK_MAX bytes that lies in one range. */
typedef struct Chunks {
    u32 range;
    u32 done;
} Chunks;

static int NextChunk(Chunks *c, u32 **ram, u32 *n) {
    while (c->range < ARRAY_COUNT(stateRanges)) {
        u32 start = stateRanges[c->range][0];
        u32 size = stateRanges[c->range][1] - start;

        if (c->done < size) {
            *ram = (u32 *)(start + c->done);
            *n = size - c->done < LZ_BLOCK_MAX ? size - c->done : LZ_BLOCK_MAX;
            c->done += *n;
            return 1;
        }
        c->range++;
        c->done = 0;
    }
    return 0;
}

/* A stored chunk begins with 2 bytes: the mode in the top 2 bits and, for the compressed modes, the length of the
 * data in the other 14. */
enum {
    CHUNK_SAME,  /* equals the base's chunk: no data */
    CHUNK_PLAIN, /* compressed by itself */
    CHUNK_DELTA, /* compressed XOR of the chunk and the base's chunk */
    CHUNK_RAW,   /* the chunk itself, from the next word-aligned address after the header */
};

static u8 *PutHeader(u8 *p, int mode, u32 len) {
    p[0] = (u8)(mode << 6 | len >> 8);
    p[1] = (u8)len;
    return p + 2;
}

static int ChunkMode(const u8 *p) {
    return p[0] >> 6;
}

static u32 ChunkLength(const u8 *p) {
    return (p[0] & 0x3F) << 8 | p[1];
}

/* 4 words a step: every range starts and ends on a word, and most are long. */
static void Copy(u32 *dst, const u32 *src, u32 bytes) {
    u32 n = bytes / 4;

    while (n >= 4) {
        u32 a = src[0], b = src[1], c = src[2], d = src[3];
        dst[0] = a;
        dst[1] = b;
        dst[2] = c;
        dst[3] = d;
        dst += 4;
        src += 4;
        n -= 4;
    }
    while (n-- > 0) {
        *dst++ = *src++;
    }
}

static void CopyBytes(u8 *dst, const u8 *src, u32 n) {
    while (n-- > 0) {
        *dst++ = *src++;
    }
}

/* The data of a chunk of the kind RAW. */
static const u8 *RawData(const u8 *p) {
    return (const u8 *)(((u32)p + 2 + 3) & ~3u);
}

/* Decodes a chunk of a base to DST. Returns the next chunk of the base. */
static const u8 *DecodeBase(u32 *dst, u32 n, const u8 *p) {
    if (ChunkMode(p) == CHUNK_PLAIN) {
        LzDecompress((u8 *)dst, n, p + 2);
        return p + 2 + ChunkLength(p);
    }
    Copy(dst, (const u32 *)RawData(p), n);
    return RawData(p) + n;
}

static const u8 *SkipBase(u32 n, const u8 *p) {
    return ChunkMode(p) == CHUNK_PLAIN ? p + 2 + ChunkLength(p) : RawData(p) + n;
}

/* Writes a chunk of the kind RAW at OP. Returns the end. */
static u8 *PutRaw(u8 *op, const u32 *ram, u32 n) {
    op = PutHeader(op, CHUNK_RAW, 0);
    op = (u8 *)(((u32)op + 3) & ~3u);
    Copy((u32 *)op, ram, n);
    return op + n;
}

/* Closes the gap of an item that is freed. */
static void FreeItem(Item *item) {
    u8 *at = item->addr;
    u32 size = item->size;

    if (size == 0) {
        return;
    }
    Copy((u32 *)at, (u32 *)(at + size), statePoolEnd - (at + size));
    for (int i = 0; i < STATE_BASES; i++) {
        if (stateBases[i].item.size != 0 && stateBases[i].item.addr > at) {
            stateBases[i].item.addr -= size;
        }
    }
    for (int i = 0; i < STATE_SLOTS; i++) {
        if (stateSlots[i].item.size != 0 && stateSlots[i].item.addr > at) {
            stateSlots[i].item.addr -= size;
        }
    }
    statePoolEnd -= size;
    item->size = 0;
}

static int BaseUsed(int b) {
    for (int i = 0; i < STATE_SLOTS; i++) {
        if (stateSlots[i].item.size != 0 && stateSlots[i].base == b) {
            return 1;
        }
    }
    return 0;
}

static void FreeUnusedBases(void) {
    for (int b = 0; b < STATE_BASES; b++) {
        if (stateBases[b].item.size != 0 && !BaseUsed(b)) {
            FreeItem(&stateBases[b].item);
        }
    }
}

static void RefreshSaved(void) {
    stateSaved = stateSlots[stateSlot < STATE_USER_SLOTS ? stateSlot : 0].item.size != 0;
}

int StateSlotUsed(int slot) {
    return stateSlots[slot].item.size != 0;
}

static void ClearSlot(int slot) {
    FreeItem(&stateSlots[slot].item);
    FreeUnusedBases();
    RefreshSaved();
}

static u32 PoolFree(void) {
    return STATE_POOL_END - (u32)statePoolEnd;
}

/* ---- Writing and reading a state ---------------------------------------------------------------------------- */

/* How a state that differs from a base is written. WRITE_QUICK stores each chunk that differs as it is, with no
 * decoding of the base and no compression, and gives up (NULL) when more than QUICK_RAW_MAX bytes would differ. */
enum {
    WRITE_FULL,
    WRITE_QUICK,
};
#define QUICK_RAW_MAX (20 * LZ_BLOCK_MAX)

/* A chunk of N bytes that is compressed or stored as it is needs its header, up to 2 bytes to align the data and N
 * bytes of room (the compressor stops one byte short of N). A chunk of the kind SAME needs 2 bytes. */
static int Fits(const u8 *op, u32 n) {
    return (u32)op + 4 + n <= STATE_POOL_END;
}

/* Writes the live state at the end of the pool, as a base (BASE < 0) or as the difference from stateBases[BASE]. Returns
 * the end of what it wrote, or NULL if the pool is full. */
static u32 changedChunks;

static u8 *WriteState(int base, int how) {
    u8 *op = statePoolEnd;
    const u8 *bp = base >= 0 ? stateBases[base].item.addr : NULL;
    Chunks chunks = { 0, 0 };
    u32 *ram;
    u32 n;
    u32 index = 0;
    u32 raw = 0;

    changedChunks = 0;
    while (NextChunk(&chunks, &ram, &n)) {
        u8 *data = op + 2;
        u8 *end;
        u32 sum[2];

        if ((u32)op + 2 > STATE_POOL_END || index >= STATE_CHUNKS_MAX) {
            return NULL;
        }
        LzHash(ram, n / 4, sum);
        if (bp == NULL) {
            if (!Fits(op, n)) {
                return NULL;
            }
            newSums[index][0] = sum[0];
            newSums[index][1] = sum[1];
            index++;
            end = LzCompress(data, data + n - 1, (u8 *)ram, n, lzWork);
            if (end != NULL) {
                op = PutHeader(op, CHUNK_PLAIN, (u32)(end - data));
                op = end;
            } else {
                op = PutRaw(op, ram, n);
            }
            continue;
        }
        if (sum[0] == baseSums[base][index][0] && sum[1] == baseSums[base][index][1]) {
            bp = SkipBase(n, bp);
            op = PutHeader(op, CHUNK_SAME, 0);
            index++;
            continue;
        }
        index++;
        changedChunks++;
        if (how == WRITE_QUICK) {
            if (!Fits(op, n) || raw + n > QUICK_RAW_MAX) {
                return NULL;
            }
            raw += n;
            bp = SkipBase(n, bp);
            op = PutRaw(op, ram, n);
            continue;
        }
        if (!Fits(op, n)) {
            return NULL;
        }
        {
            u32 *x = (u32 *)blockA;
            u32 any = 0;

            bp = DecodeBase(x, n, bp);
            for (u32 i = 0; i < n / 4; i++) {
                x[i] ^= ram[i];
                any |= x[i];
            }
            if (any == 0) {
                op = PutHeader(op, CHUNK_SAME, 0);
                continue;
            }
            end = LzCompress(data, data + n - 1, blockA, n, lzWork);
            if (end == NULL || end - data > (s32)(n / 4)) {
                /* Much changed: the chunk may be smaller by itself. */
                u8 *plain = LzCompress(blockB, blockB + n - 1, (u8 *)ram, n, lzWork);

                if (plain != NULL && (end == NULL || plain - blockB < end - data)) {
                    op = PutHeader(op, CHUNK_PLAIN, (u32)(plain - blockB));
                    CopyBytes(op, blockB, (u32)(plain - blockB));
                    op += plain - blockB;
                    continue;
                }
            }
            if (end != NULL) {
                op = PutHeader(op, CHUNK_DELTA, (u32)(end - data));
                op = end;
            } else {
                op = PutRaw(op, ram, n);
            }
        }
    }
    return op;
}

/* Puts the state of a slot into the game's memory. A chunk of the kind SAME is left alone when the game's memory
 * has the base's hash already. With VERIFY it decodes each chunk into blockB instead, whatever its kind, and counts
 * the words that differ from the game's memory: a test of the compressor and of the hashes on the state that was
 * just saved. */
static u32 ReadState(const Slot *slot, int verify) {
    const u8 *bp = stateBases[slot->base].item.addr;
    const u8 *sp = slot->item.addr;
    Chunks chunks = { 0, 0 };
    u32 bad = 0;
    u32 index = 0;
    /* Hashing the live state costs about half of decoding it. It pays when most chunks are equal, as in a state of
     * the level that runs. */
    int skip = !verify && slot->level == gameInfo.level;
    u32 *live;
    u32 n;

    while (NextChunk(&chunks, &live, &n)) {
        int mode = ChunkMode(sp);
        u32 len = ChunkLength(sp);
        u32 *ram = verify ? (u32 *)blockB : live;

        switch (mode) {
            case CHUNK_SAME: {
                u32 sum[2] = { 0, 0 };

                if (skip) {
                    LzHash(live, n / 4, sum);
                }
                if (skip && sum[0] == baseSums[slot->base][index][0] && sum[1] == baseSums[slot->base][index][1]) {
                    bp = SkipBase(n, bp);
                } else {
                    bp = DecodeBase(ram, n, bp);
                }
                sp += 2;
                break;
            }
            case CHUNK_DELTA: {
                u32 *x = (u32 *)blockA;

                bp = DecodeBase(ram, n, bp);
                LzDecompress(blockA, n, sp + 2);
                for (u32 i = 0; i < n / 4; i++) {
                    ram[i] ^= x[i];
                }
                sp += 2 + len;
                break;
            }
            case CHUNK_PLAIN:
                bp = SkipBase(n, bp);
                LzDecompress((u8 *)ram, n, sp + 2);
                sp += 2 + len;
                break;
            default:
                bp = SkipBase(n, bp);
                Copy(ram, (const u32 *)RawData(sp), n);
                sp = RawData(sp) + n;
                break;
        }
        index++;
        if (verify) {
            for (u32 i = 0; i < n / 4; i++) {
                if (ram[i] != live[i]) {
                    /* The audio thread changes a few words while the copy runs. */
                    if (bad < ARRAY_COUNT(stateVerifyList)) {
                        stateVerifyList[bad] = (u32)&live[i];
                    }
                    if ((u32)&live[i] < VERIFY_VOLATILE_START || (u32)&live[i] >= VERIFY_VOLATILE_END) {
                        stateVerifyOutside++;
                    }
                    bad++;
                }
            }
        }
    }
    return bad;
}

/* ---- Card files ----------------------------------------------------------------------------------------------- */

/* A card file holds one state with no base: for each chunk, the record of a base item (a compressed chunk, or a raw
 * one). A slot's chunk that equals its base's is that base's record; a slot's chunk that is a plain or raw record is
 * copied; one that is the XOR of the base's chunk is decoded, XORed and compressed again. The records follow each
 * other as in the pool, with a RAW chunk's data on a word of the stream (the file's offset, which the stream's
 * buffer keeps equal to its address modulo 4). */
static u8 *EmitPlain(u8 *op, const u8 *data, u32 len) {
    op = PutHeader(op, CHUNK_PLAIN, len);
    CopyBytes(op, data, len);
    return op + len;
}

static int ExportState(const Slot *slot, CardStream *s) {
    const u8 *bp = stateBases[slot->base].item.addr;
    const u8 *sp = slot->item.addr;
    Chunks chunks = { 0, 0 };
    u32 *ram;
    u32 n;

    while (NextChunk(&chunks, &ram, &n)) {
        u8 *op;
        u32 len = ChunkLength(sp);

        if (s->cap - s->pos < CARD_MAX_RECORD && s->flush(s, 0) != 0) {
            return 1;
        }
        op = s->buf + s->pos;
        switch (ChunkMode(sp)) {
            case CHUNK_SAME:
                if (ChunkMode(bp) == CHUNK_PLAIN) {
                    op = EmitPlain(op, bp + 2, ChunkLength(bp));
                } else {
                    op = PutRaw(op, (const u32 *)RawData(bp), n);
                }
                bp = SkipBase(n, bp);
                sp += 2;
                break;
            case CHUNK_PLAIN:
                op = EmitPlain(op, sp + 2, len);
                bp = SkipBase(n, bp);
                sp += 2 + len;
                break;
            case CHUNK_DELTA: {
                u32 *x = (u32 *)blockA;
                u32 *y = (u32 *)blockB;
                u8 *end;

                bp = DecodeBase(x, n, bp);
                LzDecompress(blockB, n, sp + 2);
                for (u32 i = 0; i < n / 4; i++) {
                    x[i] ^= y[i];
                }
                end = LzCompress(op + 2, op + 2 + n - 1, blockA, n, lzWork);
                if (end != NULL) {
                    PutHeader(op, CHUNK_PLAIN, (u32)(end - (op + 2)));
                    op = end;
                } else {
                    op = PutRaw(op, x, n);
                }
                sp += 2 + len;
                break;
            }
            default:
                op = PutRaw(op, (const u32 *)RawData(sp), n);
                bp = SkipBase(n, bp);
                sp = RawData(sp) + n;
                break;
        }
        s->pos = (u32)(op - s->buf);
        s->chunk++;
    }
    return 0;
}

/* Decodes a card file's records into the game's memory. Returns 0, or 1 if the records do not fit the layout. */
static int ImportState(CardSource *src) {
    Chunks chunks = { 0, 0 };
    u32 *live;
    u32 n;

    while (NextChunk(&chunks, &live, &n)) {
        const u8 *p;

        if (src->len - src->pos < CARD_MAX_RECORD && !src->eof && src->fill(src) != 0) {
            return 1;
        }
        if (src->len - src->pos < 2) {
            return 1;
        }
        p = src->buf + src->pos;
        if (ChunkMode(p) == CHUNK_PLAIN) {
            u32 len = ChunkLength(p);

            if (src->len - src->pos < 2 + len || LzDecompress((u8 *)live, n, p + 2) != p + 2 + len) {
                return 1;
            }
            src->pos += 2 + len;
        } else if (ChunkMode(p) == CHUNK_RAW) {
            const u8 *data = RawData(p);

            if ((u32)(data - src->buf) + n > src->len) {
                return 1;
            }
            Copy(live, (const u32 *)data, n);
            src->pos = (u32)(data - src->buf) + n;
        } else {
            return 1;
        }
        src->chunk++;
    }
    return 0;
}

/* The number of the record format of a card file (the comment above ExportState). A change raises it. */
#define CARD_RECORD_FORMAT 1

void StateLayout(StateMeta *m) {
    u32 hash = 2166136261u;
    u32 chunks = 0;
    Chunks c = { 0, 0 };
    u32 *ram;
    u32 n;

    m->bytes = 0;
    for (u32 i = 0; i < ARRAY_COUNT(stateRanges); i++) {
        hash = (hash ^ stateRanges[i][0]) * 16777619u;
        hash = (hash ^ stateRanges[i][1]) * 16777619u;
        m->bytes += stateRanges[i][1] - stateRanges[i][0];
    }
    /* The formats that the records are in: a file from a build with other ones is refused. */
    hash = (hash ^ LZ_FORMAT) * 16777619u;
    hash = (hash ^ LZ_BLOCK_MAX) * 16777619u;
    hash = (hash ^ CARD_RECORD_FORMAT) * 16777619u;
    while (NextChunk(&c, &ram, &n)) {
        chunks++;
    }
    m->chunks = chunks;
    m->layout = hash;
    m->level = 0;
    m->song = 0;
}

int StateSlotMeta(int slot, StateMeta *m) {
    StateLayout(m);
    m->level = stateSlots[slot].level;
    m->song = stateSlots[slot].song;
    return stateSlots[slot].item.size != 0;
}

int StateCardExport(int slot, CardStream *s) {
    return stateSlots[slot].item.size == 0 || ExportState(&stateSlots[slot], s);
}

/* A slot that differs from its base in nothing: 2 bytes for each chunk. */
static u8 *WriteSameSlot(void) {
    u8 *op = statePoolEnd;
    Chunks chunks = { 0, 0 };
    u32 *ram;
    u32 n;

    while (NextChunk(&chunks, &ram, &n)) {
        if ((u32)op + 2 > STATE_POOL_END) {
            return NULL;
        }
        op = PutHeader(op, CHUNK_SAME, 0);
    }
    return op;
}

static int FindBase(int level) {
    for (int b = 0; b < STATE_BASES; b++) {
        if (stateBases[b].item.size != 0 && stateBases[b].level == level) {
            return b;
        }
    }
    return -1;
}

static int FreeBaseIndex(void) {
    for (int b = 0; b < STATE_BASES; b++) {
        if (stateBases[b].item.size == 0) {
            return b;
        }
    }
    return -1;
}

/* The base of another level that the live state is closest to: the one with the most bytes in chunks that equal
 * the live state's. Returns -1 if there is no base. */
static int ForeignBase(void) {
    u32 equal[STATE_BASES] = { 0 };
    Chunks chunks = { 0, 0 };
    int best = -1;
    u32 index = 0;
    u32 *ram;
    u32 n;

    while (NextChunk(&chunks, &ram, &n) && index < STATE_CHUNKS_MAX) {
        u32 sum[2];

        LzHash(ram, n / 4, sum);
        for (int b = 0; b < STATE_BASES; b++) {
            if (stateBases[b].item.size != 0 && sum[0] == baseSums[b][index][0] && sum[1] == baseSums[b][index][1]) {
                equal[b] += n;
            }
        }
        index++;
    }
    for (int b = 0; b < STATE_BASES; b++) {
        if (stateBases[b].item.size != 0 && (best < 0 || equal[b] > equal[best])) {
            best = b;
        }
    }
    return best;
}

/* Enters the item at START (the end of the pool, already written) as the state of SLOT. The new item goes into the
 * table before the old one is freed: the free closes the gap under it. */
static void Place(int slot, u8 *start, u32 size, int base, int level, s32 song) {
    stateSlots[STATE_SLOTS] = stateSlots[slot];
    stateSlots[slot].item.addr = start;
    stateSlots[slot].item.size = size;
    stateSlots[slot].base = (u8)base;
    stateSlots[slot].level = (u8)level;
    stateSlots[slot].song = song;
    statePoolEnd = start + size;
    FreeItem(&stateSlots[STATE_SLOTS].item);
}

/* The state that differs from the base: stored as it is where it differs for the automatic slot (it must load fast),
 * and compressed where that is too much. */
static u8 *WriteDifference(int base, int quick) {
    u8 *end = quick ? WriteState(base, WRITE_QUICK) : NULL;

    stateQuick = end != NULL;
    return end != NULL ? end : WriteState(base, WRITE_FULL);
}

/* Saves the live state into SLOT. Returns 1 if it did, 0 if the pool had no room (the slot is then empty). */
static int SaveInto(int slot) {
    int level = gameInfo.level;
    int quick = slot == STATE_SLOT_AUTO;
    int cleared = 0;
    u8 *start;
    u8 *end;
    int base;
    u8 *before = statePoolEnd;

    stateQuick = 0;
    for (;;) {
        start = statePoolEnd;
        end = NULL;
        base = FindBase(level);
        if (base >= 0) {
            end = WriteDifference(base, quick);
        } else {
            int fresh = FreeBaseIndex();

            if (fresh >= 0) {
                end = WriteState(-1, WRITE_FULL);
            }
            if (end != NULL) {
                /* A base of this level: the state itself, so the slot equals it. */
                stateBases[fresh].item.addr = start;
                stateBases[fresh].item.size = (u32)(end - start + 3) & ~3u;
                stateBases[fresh].level = (u8)level;
                CopyBytes((u8 *)baseSums[fresh], (u8 *)newSums, sizeof(newSums));
                statePoolEnd = start + stateBases[fresh].item.size;
                start = statePoolEnd;
                end = WriteSameSlot();
                if (end == NULL) {
                    FreeItem(&stateBases[fresh].item);
                } else {
                    base = fresh;
                }
            }
            if (end == NULL) {
                /* No room for a base of this level: a base of another level serves, with larger differences. */
                start = statePoolEnd;
                base = ForeignBase();
                if (base >= 0) {
                    end = WriteDifference(base, quick);
                }
            }
        }
        if (end != NULL) {
            break;
        }
        /* No room. The slot's own state goes first, then one more try. */
        if (cleared || stateSlots[slot].item.size == 0) {
            ClearSlot(slot);
            return 0;
        }
        ClearSlot(slot);
        cleared = 1;
    }
    Place(slot, start, (u32)(end - start + 3) & ~3u, base, level, audioCtrl.current);
    stateSaveBytes = (u32)(statePoolEnd - before);
    stateChanged = changedChunks;
    FreeUnusedBases();
    return 1;
}

/* ---- Stopping the game's other threads ---------------------------------------------------------------------- */

/* The draw thread always holds one frame that it has built and not yet sent (SendLastFrame). After a load, that
 * frame's display list points into the old heap. In another level, or after the level was loaded again, the
 * heap holds other data there, and the frame stops the RSP or RDP. So a load empties each of its tasks: a full
 * sync and the end, as each task of the game's ends. The scheduler still shows the frame buffer, which holds
 * its picture from two frames before. The drawing goes on as before, so a save and a load leave the threads in
 * the same order of frames (the game's drawing code calls Random). */
static Gfx emptyList[2] __attribute__((aligned(8)));

static void EmptyHeldFrame(void) {
    emptyList[0] = GfxSync(CMD_FULLSYNC);
    emptyList[1] = GfxEndDisplayList();
    osWritebackDCache(emptyList, sizeof(emptyList));
    for (int i = 1; i < currentTask; i++) {
        u8 *task = (u8 *)tlistp[i];
        *(Gfx **)(task + TASK_DATA_PTR) = emptyList;
        *(u32 *)(task + TASK_DATA_SIZE) = sizeof(emptyList);
    }
}

/* 1 while the draw thread and the controller thread wait for their next frame message, and the RSP and RDP have
 * no graphics task of the game's. The draw thread waits on gfx_msgQ only at the top of its loop. The controller
 * thread waits on its own queue between frames, and on controllerMsgQ while it reads the controllers. */
static int ThreadsIdle(void) {
    void *controllerQueue = *(void *volatile *)&ControllerThread.queue;

    return gfxTasks == 0 && *(volatile u16 *)&drawGraphicsThread.state == OS_STATE_WAITING &&
           *(void *volatile *)&drawGraphicsThread.queue == &gfx_msgQ &&
           *(volatile u16 *)&ControllerThread.state == OS_STATE_WAITING && controllerQueue != &controllerMsgQ;
}

/* The scheduler sends a frame message every few retraces while no graphics task is pending, and the copy keeps
 * the draw and controller threads from taking them. On the console the copy takes several frames, so several
 * messages wait in each queue afterwards, and the draw thread would build frames back to back over buffers that
 * the RSP still reads. So the messages that came during the copy are dropped: the game, the draw thread and the
 * controller thread go on from the next frame message, after a save as after a load. */
static void DropFrameMessages(void) {
    void *msg;
    NNScClient *client = *(NNScClient **)(sched + SCHED_CLIENT_LIST);

    while (osRecvMesg(&gfx_msgQ, &msg, OS_MESG_NOBLOCK) == 0) {
        /* No task ran during the copy, but a task-done message would still count. */
        if (*(s16 *)msg == 2) {
            gfxTasks--;
        }
    }
    while (osRecvMesg(&main_msgQ, &msg, OS_MESG_NOBLOCK) == 0) {
    }
    /* The controller thread's queue is a local of its own: the client whose queue is on its stack. */
    for (int i = 0; i < 8 && SafePointer(client); i++, client = client->next) {
        u8 *q = (u8 *)client->msgQ;
        if (q >= ControllerThreadStack && q < ControllerThreadStack + sizeof(ControllerThreadStack)) {
            while (osRecvMesg(client->msgQ, &msg, OS_MESG_NOBLOCK) == 0) {
            }
        }
    }
}

/* Holds the other threads still for a save or a load. The game's threads must not run during the copy: they would
 * see half of a state. The game thread runs here at the start of its frame. The draw thread can still be in the
 * middle of a frame, waiting for a DMA, and the controller thread in the middle of its read: after a load they
 * would go on with the old state. So this waits until both wait for their next frame message and no graphics
 * task is pending (the RSP and RDP read textures and vertices from the heap). It runs above the draw thread, with
 * the controller thread put below it. The test comes after the priorities change: a frame message could wake a
 * thread between a test and a change. The scheduler, its audio thread and the music driver keep running: they
 * keep the OS's timing and the sound, and use none of the state's memory. Interrupts stay on. Returns the game
 * thread's priority for Resume, or HOLD_FAILED if the threads were not idle after HOLD_TIMEOUT_TICKS. */
#define HOLD_TIMEOUT_TICKS (3 * 46875000u)
#define HOLD_FAILED (-1)
static volatile u8 held; /* 1 from Hold to Resume: the menu must not change the pool */

static OSPri Hold(void) {
    OSPri pri = osGetThreadPri(NULL);
    u32 start = osGetCount();

    for (;;) {
        osSetThreadPri(NULL, COPY_PRIORITY);
        osSetThreadPri(&ControllerThread, CONTROLLER_HELD_PRIORITY);
        if (ThreadsIdle()) {
            held = 1;
            return pri;
        }
        /* The draw and controller threads run above the game thread and finish their frame. */
        osSetThreadPri(&ControllerThread, controllerPriority);
        osSetThreadPri(NULL, pri);
        if (osGetCount() - start > HOLD_TIMEOUT_TICKS) {
            return HOLD_FAILED;
        }
    }
}

static void Resume(OSPri pri) {
    held = 0;
    DropFrameMessages();
    osSetThreadPri(&ControllerThread, controllerPriority);
    osSetThreadPri(NULL, pri);
}

s32 StateHold(void) {
    return Hold();
}

void StateResume(s32 pri) {
    Resume(pri);
}

/* The game's EEPROM requests are in its bss. A save or a load waits until none is waiting: the controller
 * thread would act on a request that the copy has taken back. */
static int EepromIdle(void) {
    return eepromMessageQueue[0] == 0 && !controllerBusy;
}

/* A state holds the game's objects, not the title's overlays and lists that DoMenu and TitleScreen set up on their own,
 * so a state from one kind of title loaded on another runs the wrong code on them. States are for levels. */
static int OnTitle(void) {
    return ActionsOnTitle() || earlyTitle.on || gameInfo.mode == GAME_MODE_LEVEL_SELECT;
}

/* ---- Save and load ------------------------------------------------------------------------------------------ */

static char message[48];

static void SlotName(char *buf, int size, int slot) {
    if (slot == STATE_SLOT_RECORD) {
        Format(buf, size, "recording");
    } else if (slot == STATE_SLOT_AUTO) {
        Format(buf, size, "level start");
    } else {
        Format(buf, size, "slot %d", slot + 1);
    }
}

enum {
    SAVE_POOL_FULL = -1, /* no room: the slot is empty now */
    SAVE_REFUSED = 0,    /* a title, the EEPROM or the other threads were busy: nothing changed */
    SAVE_OK = 1,
};

static int Save(int slot, int quiet) {
    char name[16];
    OSPri pri;
    u32 t;
    int ok;
    int had;

    if (OnTitle()) {
        if (!quiet) {
            HudMessage("no states on a title");
        }
        return SAVE_REFUSED;
    }
    if (!EepromIdle()) {
        if (!quiet) {
            HudMessage("state not saved: EEPROM busy");
        }
        return SAVE_REFUSED;
    }
    SlotName(name, sizeof(name), slot);
    NoclipBeforeSave();
    pri = Hold();
    if (pri == HOLD_FAILED) {
        NoclipAfterSave();
        if (!quiet) {
            HudMessage("state not saved: game busy");
        }
        return SAVE_REFUSED;
    }
    had = stateSlots[slot].item.size != 0;
    t = osGetCount();
    ok = SaveInto(slot);
    stateSaveTicks = osGetCount() - t;
    stateSaves++;
    if (ok && stateVerify) {
        for (u32 i = 0; i < ARRAY_COUNT(stateVerifyList); i++) {
            stateVerifyList[i] = 0;
        }
        stateVerifyBad += ReadState(&stateSlots[slot], 1);
    }
    Resume(pri);
    NoclipAfterSave();
    HudResetLag();
    RefreshSaved();
    if (ok) {
        stateSavedFrame = frameCount;
        stateSlots[slot].kib = (stateSlots[slot].item.size + 1023) >> 10;
        if (!quiet) {
            Format(message, sizeof(message), "state saved: %s, %u KB", name, stateSlots[slot].kib);
            HudMessage(message);
        }
        return SAVE_OK;
    }
    if (slot == STATE_SLOT_RECORD) {
        HudMessage("recording not started: pool full");
    } else {
        Format(message, sizeof(message), had ? "state not saved: pool full, %s emptied" : "state not saved: pool full (%s)",
               name);
        HudMessage(message);
    }
    return SAVE_POOL_FULL;
}

int StateSaveSlot(int slot) {
    return Save(slot, 0) == SAVE_OK;
}

int StateSave(void) {
    return StateSaveSlot(stateSlot);
}

int StateSaveReady(void) {
    return !OnTitle();
}

int StateLoadReady(int slot) {
    return !OnTitle() && stateSlots[slot].item.size != 0;
}

/* The game's RAM copy of the save, which a level start does not touch. The instant reset keeps it as it is. */
extern u8 hiscoreTable[];
#define HISCORE_TABLE_SIZE 48

/* The card file being loaded, or NULL: Load decodes it in place of a slot's item (savestate.h, CardSource). */
static CardSource *cardIn;
static int cardInFailed;

static int Load(Slot *s, const char *name, int instant) {
    static u8 kept[sizeof(saveSlot) + sizeof(bestTimes) + sizeof(eepromCRCS) + HISCORE_TABLE_SIZE] __attribute__((aligned(8)));
    int level = gameInfo.level;
    OSPri pri;
    u32 t;
    u8 tasks;

    if (instant) {
        CopyBytes(kept, (u8 *)saveSlot, sizeof(saveSlot));
        CopyBytes(kept + sizeof(saveSlot), (u8 *)bestTimes, sizeof(bestTimes));
        CopyBytes(kept + sizeof(saveSlot) + sizeof(bestTimes), (u8 *)eepromCRCS, sizeof(eepromCRCS));
        CopyBytes(kept + sizeof(saveSlot) + sizeof(bestTimes) + sizeof(eepromCRCS), hiscoreTable, HISCORE_TABLE_SIZE);
    }
    pri = Hold();
    if (pri == HOLD_FAILED) {
        HudMessage("state not loaded: game busy");
        return 0;
    }
    /* currentTask is in the game's bss, but it counts the tasks of the frame that the draw thread holds now. */
    tasks = currentTask;
    t = osGetCount();
    if (cardIn != NULL) {
        cardInFailed = ImportState(cardIn) != 0;
    } else {
        ReadState(s, 0);
    }
    stateLoadTicks = osGetCount() - t;
    currentTask = tasks;
    EmptyHeldFrame();
    /* The RSP reads textures and vertices from RDRAM. */
    osWritebackDCacheAll();
    Resume(pri);
    if (instant) {
        CopyBytes((u8 *)saveSlot, kept, sizeof(saveSlot));
        CopyBytes((u8 *)bestTimes, kept + sizeof(saveSlot), sizeof(bestTimes));
        CopyBytes((u8 *)eepromCRCS, kept + sizeof(saveSlot) + sizeof(bestTimes), sizeof(eepromCRCS));
        CopyBytes(hiscoreTable, kept + sizeof(saveSlot) + sizeof(bestTimes) + sizeof(eepromCRCS), HISCORE_TABLE_SIZE);
    }
    stateLoads++;
    /* The load puts back what the writes changed in the state's ranges. */
    corrupt.dirty = 0;
    NoclipAfterLoad();
    /* The music driver is not in the state, nor the game's handle to its song (audioCtrl). Sound effects
     * playing now would go on with no owner. The song of the save starts again if another one plays now. */
    MusStop(MUSFLAG_EFFECTS, 0);
    if (audioCtrl.current != s->song) {
        /* PrepareSong also sets the song's reverb and volume, so it runs only for a change of song. */
        PrepareSong(s->song);
    }
    HudResetLag();
    if (instant) {
        /* A level start for the practice code: the state is the level's first game frame. */
        practice.levelStarts++;
        autoStart = practice.levelStarts;
        s->starts = practice.levelStarts;
        HudLevelStart();
        BestTimesLevelStart();
        HudMessage("level reset (instant)");
    } else {
        BestTimesTaint(BT_STATE);
        Format(message, sizeof(message), level == gameInfo.level ? "state loaded: %s" : "state loaded: %s (other level)",
               name);
        HudMessage(message);
    }
    return 1;
}

int StateLoadSlot(int slot) {
    if (OnTitle()) {
        HudMessage("no states on a title");
        return 0;
    }
    if (stateSlots[slot].item.size == 0) {
        HudMessage("no state saved");
        return 0;
    }
    if (!EepromIdle()) {
        HudMessage("state not loaded: EEPROM busy");
        return 0;
    }
    {
        char name[16];

        SlotName(name, sizeof(name), slot);
        return Load(&stateSlots[slot], name, 0);
    }
}

int StateLoad(void) {
    return StateLoadSlot(stateSlot);
}

int StateCardLoad(const StateMeta *m, CardSource *src, const char *name) {
    static Slot cardSlot;
    int loaded;

    if (OnTitle()) {
        HudMessage("no states on a title");
        return 0;
    }
    if (!EepromIdle()) {
        HudMessage("state not loaded: EEPROM busy");
        return 0;
    }
    cardSlot.level = m->level;
    cardSlot.song = m->song;
    cardIn = src;
    cardInFailed = 0;
    loaded = Load(&cardSlot, name, 0);
    cardIn = NULL;
    return loaded && cardInFailed ? -1 : loaded;
}

int StateCopySlot(int from, int to) {
    const Slot *f = &stateSlots[from];
    u32 size = f->item.size;
    OSPri pri;
    u8 *start;

    if (from == to || size == 0) {
        return 0;
    }
    /* The pool table changes: the controller thread (the menu's Clear) must not run in the middle. */
    pri = Hold();
    if (pri == HOLD_FAILED) {
        HudMessage("state not copied: game busy");
        return 0;
    }
    if (PoolFree() < size) {
        /* The old state of TO goes first, as in a save. */
        ClearSlot(to);
        if (PoolFree() < size) {
            Resume(pri);
            return 0;
        }
    }
    start = statePoolEnd;
    Copy((u32 *)start, (const u32 *)f->item.addr, size);
    Place(to, start, size, f->base, f->level, f->song);
    stateSlots[to].kib = f->kib;
    FreeUnusedBases();
    RefreshSaved();
    Resume(pri);
    return 1;
}

/* ---- The instant reset ---------------------------------------------------------------------------------------- */

void StateGameFrame(void) {
    /* RunGame calls this for every message of the main queue; a level's first game frame has frameCount2 0. */
    if (settings.instantReset && gameInfo.mode == GAME_MODE_LEVEL && frameCount2 == 0 && ActionsInLevel() &&
        !earlyTitle.on && autoStart != practice.levelStarts) {
        int result = Save(STATE_SLOT_AUTO, 1);

        if (result == SAVE_OK) {
            autoStart = practice.levelStarts;
            stateSlots[STATE_SLOT_AUTO].starts = practice.levelStarts;
            return;
        }
        /* No state of this start: the reset must not load an older one. A save that was refused (the EEPROM was busy)
         * is tried again on the next message of this game frame. A full pool would be as full then. */
        ClearSlot(STATE_SLOT_AUTO);
        if (result == SAVE_POOL_FULL) {
            autoStart = practice.levelStarts;
        }
    }
}

int StateInstantReset(void) {
    Slot *s = &stateSlots[STATE_SLOT_AUTO];

    /* A cameo has the level's number, and a pause, a death or an exit is in the middle of something that the
     * state would cut off. */
    if (!settings.instantReset || gameInfo.mode != GAME_MODE_LEVEL || gamePaused || !ActionsInLevel() ||
        (gameInfo.exitFlags & 1) || handControl.deathTimer != 0 || s->item.size == 0 || s->level != gameInfo.level ||
        s->starts != practice.levelStarts || OnTitle() || !EepromIdle()) {
        return 0;
    }
    return Load(s, "level start", 1);
}

void StateClear(int slot) {
    if (held) {
        return;
    }
    if (slot >= STATE_SLOTS) {
        for (int i = 0; i < STATE_SLOTS; i++) {
            ClearSlot(i);
        }
    } else {
        ClearSlot(slot);
    }
}

/* ---- The menu's text ------------------------------------------------------------------------------------------ */

void StateSlotText(char *buf, int size, int arg) {
    const Slot *s = &stateSlots[stateSlot < STATE_USER_SLOTS ? stateSlot : 0];

    (void)arg;
    if (s->item.size == 0) {
        Format(buf, size, "empty");
    } else {
        Format(buf, size, "%s, %u KB", levelInfo[s->level].name, (s->item.size + 1023) >> 10);
    }
}

void StatePoolText(char *buf, int size, int arg) {
    int used = 0;

    (void)arg;
    for (int i = 0; i < STATE_USER_SLOTS; i++) {
        used += stateSlots[i].item.size != 0;
    }
    Format(buf, size, "%d of %d slots, %u KB free", used, STATE_USER_SLOTS, PoolFree() >> 10);
}

void StateAutoText(char *buf, int size, int arg) {
    const Slot *s = &stateSlots[STATE_SLOT_AUTO];

    (void)arg;
    if (s->item.size == 0) {
        Format(buf, size, "none");
    } else {
        Format(buf, size, "%s", levelInfo[s->level].name);
    }
}
