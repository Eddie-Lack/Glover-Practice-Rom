/* Memory corruption tools (corrupt.h). */
#include "actions.h"
#include "besttimes.h"
#include "corrupt.h"
#include "game.h"
#include "memview.h"
#include "safe.h"

Corrupt corrupt;

const u8 pokePeriods[6] = { 1, 2, 5, 10, 30, 60 };
const u32 corruptRangeSizes[4] = { 16, 256, 4096, 65536 };

_Static_assert(sizeof(Poke) == 0x10 && __builtin_offsetof(Corrupt, rng) == 0x80 &&
                   __builtin_offsetof(Corrupt, requests) == 0x93 && __builtin_offsetof(Corrupt, active) == 0x94,
               "tests/corrupt.py has the offsets");

/* The end of the practice segment's data: the linker script's symbol. */
extern u8 practice_BSS_END[];

#define RDRAM_FIRST 0x80000000u
#define RDRAM_END 0x80800000u
#define PRACTICE_FIRST 0x80400000u
#define MAX_ACTORS 400
#define ACTOR_SIZE 0xE4
/* CreateObjects (the draw thread) builds the actors' distance list each video frame: it writes distNext, distPrev
 * and distSq (0x10 to 0x1B) of every actor. A flipped bit there is gone at once, so a flip leaves those bytes out. */
#define ACTOR_DRAWN_FIRST 0x10
#define ACTOR_DRAWN_SIZE 12

/* xorshift32, as in chaos.c. */
static u32 Next(void) {
    u32 x = corrupt.rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return corrupt.rng = x;
}

int PokeAllowed(u32 address, u32 size) {
    u32 end = address + size;

    if (address < RDRAM_FIRST || end > RDRAM_END || end < address) {
        return 0;
    }
    /* Not the practice segment: its code, its data and its bss. */
    return !(address < (u32)practice_BSS_END && end > PRACTICE_FIRST);
}

static u32 SizeMask(int size) {
    return size >= 4 ? 0xFFFFFFFFu : (1u << (size * 8)) - 1;
}

static u32 BitsMask(int bits) {
    return bits >= 32 ? 0xFFFFFFFFu : (1u << bits) - 1;
}

static u32 Read(u32 address, int size) {
    switch (size) {
        case 1:
            return *(volatile u8 *)address;
        case 2:
            return *(volatile u16 *)address;
        default:
            return *(volatile u32 *)address;
    }
}

/* Returns 1 when it wrote. The data cache is written back, which matters for data that the RSP and the RDP read. The
 * instruction cache is cleared for the bytes, which matters for a write into code. */
static int Write(u32 address, u32 value, int size) {
    if (size != 1 && size != 2 && size != 4) {
        return 0;
    }
    address &= ~(u32)(size - 1);
    if (!PokeAllowed(address, size)) {
        return 0;
    }
    switch (size) {
        case 1:
            *(volatile u8 *)address = value;
            break;
        case 2:
            *(volatile u16 *)address = value;
            break;
        default:
            *(volatile u32 *)address = value;
            break;
    }
    osWritebackDCache((void *)address, size);
    osInvalICache((void *)address, size);
    return 1;
}

/* The taint is set at the write: the menu can clear the dirty flag before the next game frame samples it. */
static void Mark(void) {
    corrupt.dirty = 1;
    BestTimesTaint(BT_POKES);
}

void PokeFix(Poke *p) {
    if (p->size != 1 && p->size != 2) {
        p->size = 4;
    }
    p->address &= ~(u32)(p->size - 1);
    p->value &= SizeMask(p->size);
    if (p->bits < 1 || p->bits > p->size * 8) {
        p->bits = p->size * 8;
    }
    if (p->mode >= POKE_MODES) {
        p->mode = POKE_OFF;
    }
    if (p->period >= sizeof(pokePeriods)) {
        p->period = 0;
    }
}

void PokeRecount(void) {
    int n = 0;

    for (int i = 0; i < POKE_COUNT; i++) {
        const Poke *p = &corrupt.pokes[i];

        n += p->address != 0 && p->mode >= POKE_FREEZE;
    }
    corrupt.active = n;
}

void PokeClear(Poke *p) {
    p->address = p->value = p->count = 0;
    p->mode = POKE_OFF;
    p->size = 4;
    p->bits = 32;
    p->period = 0;
    PokeRecount();
}

void PokesClearAll(void) {
    for (int i = 0; i < POKE_COUNT; i++) {
        PokeClear(&corrupt.pokes[i]);
    }
    /* The writes stay, but the user has said that the tools are done. */
    corrupt.dirty = 0;
}

void PokeMark(u32 address, u32 value) {
    Poke *p = NULL;

    address &= ~3u;
    for (int i = 0; i < POKE_COUNT && p == NULL; i++) {
        if (corrupt.pokes[i].address == address) {
            p = &corrupt.pokes[i];
        }
    }
    for (int i = 0; i < POKE_COUNT && p == NULL; i++) {
        if (corrupt.pokes[i].address == 0) {
            p = &corrupt.pokes[i];
        }
    }
    if (p == NULL) {
        /* The list is full: the oldest poke goes, as in the watch list. */
        for (int i = 0; i < POKE_COUNT - 1; i++) {
            corrupt.pokes[i] = corrupt.pokes[i + 1];
        }
        p = &corrupt.pokes[POKE_COUNT - 1];
    }
    if (p->address != address) {
        PokeClear(p);
        p->mode = POKE_ONCE;
    }
    p->address = address;
    p->value = value;
    p->size = 4;
    if (p->bits < 1 || p->bits > 32) {
        p->bits = 32;
    }
    PokeFix(p);
    PokeRecount();
}

static void Seed(void) {
    if (!corrupt.seeded) {
        corrupt.rng = osGetCount() | 1;
        corrupt.seeded = 1;
    }
}

/* One step of a random or flip poke, or the write of a once or freeze poke. */
void PokeApply(Poke *p) {
    u32 value = p->value;

    Seed();
    PokeFix(p);
    if (p->address == 0 || p->mode == POKE_OFF) {
        return;
    }
    if (p->mode == POKE_RANDOM) {
        u32 mask = BitsMask(p->bits);

        value = (p->value & ~mask) | (Next() & mask);
    } else if (p->mode == POKE_FLIP) {
        value = Read(p->address, p->size) ^ (1u << (Next() % p->bits));
    }
    if (Write(p->address, value, p->size)) {
        p->count++;
        Mark();
    }
}

/* Flips one random bit among the BYTES bytes from BASE, leaving out SKIP_BYTES bytes from byte SKIP_FIRST on. */
static void FlipIn(u32 base, u32 bytes, u32 skipFirst, u32 skipBytes) {
    u32 offset;
    u32 address;

    if (bytes <= skipBytes) {
        return;
    }
    offset = Next() % ((bytes - skipBytes) * 8);
    address = base + offset / 8;
    if (offset / 8 >= skipFirst) {
        address += skipBytes;
    }
    if (!PokeAllowed(address, 1)) {
        return;
    }
    Write(address, Read(address, 1) ^ (1u << (offset % 8)), 1);
    corrupt.flips++;
    corrupt.lastAddress = address;
    corrupt.lastBit = offset % 8;
    Mark();
}

static void Flip(void) {
    Actor *a;
    int count = 0;
    int pick;

    switch (corrupt.region) {
        case REGION_GLOVE:
            FlipIn((u32)&hand, ACTOR_SIZE, ACTOR_DRAWN_FIRST, ACTOR_DRAWN_SIZE);
            break;
        case REGION_BALL:
            FlipIn((u32)&ball, ACTOR_SIZE, ACTOR_DRAWN_FIRST, ACTOR_DRAWN_SIZE);
            break;
        case REGION_ACTORS:
            for (a = actorList.head.next; SafePointer(a) && a != &actorList.head && count < MAX_ACTORS; a = a->next) {
                count++;
            }
            if (count == 0) {
                return;
            }
            pick = Next() % count;
            for (a = actorList.head.next; pick > 0 && SafePointer(a) && a != &actorList.head; a = a->next) {
                pick--;
            }
            if (SafePointer(a) && a != &actorList.head) {
                FlipIn((u32)a, ACTOR_SIZE, ACTOR_DRAWN_FIRST, ACTOR_DRAWN_SIZE);
            }
            break;
        default: {
            u32 size = corruptRangeSizes[corrupt.rangeSize & 3];

            FlipIn(MemViewAddress() & ~(size - 1), size, 0, 0);
            break;
        }
    }
}

void CorruptRequest(int what) {
    u32 mask = __osDisableInt();

    corrupt.requests |= what;
    __osRestoreInt(mask);
}

void CorruptGameFrame(void) {
    u32 mask;
    int requests;

    Seed();
    /* The menu (the controller thread) sets the requests at any time. */
    mask = __osDisableInt();
    requests = corrupt.requests;
    corrupt.requests = 0;
    __osRestoreInt(mask);
    if (requests & CORRUPT_APPLY) {
        for (int i = 0; i < POKE_COUNT; i++) {
            if (corrupt.pokes[i].mode == POKE_ONCE) {
                PokeApply(&corrupt.pokes[i]);
            }
        }
    }
    if (requests & CORRUPT_FLIP) {
        Flip();
    }
    /* RunGame calls ActionsGameFrame for every message of the main queue: the running pokes act once a game frame. */
    if (!corrupt.active || frameCount2 == corrupt.lastFrame || frameCount2 == 0) {
        return;
    }
    corrupt.lastFrame = frameCount2;
    corrupt.frames++;
    for (int i = 0; i < POKE_COUNT; i++) {
        Poke *p = &corrupt.pokes[i];

        if (p->address == 0 || p->mode < POKE_FREEZE) {
            continue;
        }
        if (p->mode != POKE_FREEZE && corrupt.frames % pokePeriods[p->period % sizeof(pokePeriods)] != 0) {
            continue;
        }
        PokeApply(p);
    }
}
