/* Spawn or move actors (spawn.h). */
#include "actions.h"
#include "fmt.h"
#include "game.h"
#include "hud.h"
#include "menu.h"
#include "safe.h"
#include "spawn.h"
#include "text.h"

/* Where the actor goes: in front of the glove (its heading is rot.y: forward is (sin, cos)) and a little above. */
#define FRONT 70.0f
#define UP 25.0f
#define MAX_WALK 1024 /* the longest list walked: more than the heap has blocks for (0x500) */
#define PLATFORM_OFFSET 0x80 /* Platform.actor */
#define PLATFORM_STATIC 0x20000 /* the faces are never brought up to date (parse.c, bonus.c) */
#define MAX_ENEMIES 30 /* enemies[] holds 30 (0x802996E8 to 0x8029F0E8) */
#define PI 3.14159265f

enum {
    TYPE_CAMERA = 0,
    TYPE_HAND = 1,
    TYPE_BALL = 2,
    TYPE_FIRST_ENEMY = 5,
    TYPE_FIRST_BOSS = 35, /* the Selwyn bosses, the robots, Spank, baby Spank and the evil glove */
    TYPE_LAST_BOSS = 42,
    TYPE_NME_FROG = 58,
    TYPE_PLATFORM = 59,
    TYPE_LANDSCAPE = 60,
    TYPE_WATER = 62,
    TYPE_CAMEO = 63,
};
#define ENEMY_TYPES 0x3B /* uniqueEnemyCount[] has 0x3B entries */

/* The parts of the game's structs that the spawn code writes (collect.h, actor.h). */
typedef struct Garib {
    /* 0x00 */ PAD(00)[0x08];
    /* 0x08 */ f32 velY;
    /* 0x0C */ u8 unk0C;
    /* 0x0D */ s8 active;
    /* 0x0E */ u8 type;
    /* 0x0F */ u8 mode;
    /* 0x10 */ f32 scale;
    /* 0x14 */ f32 scaleTarget;
    /* 0x18 */ PAD(18)[0x08];
    /* 0x20 */ const u16 *tex; /* Texture: width at 0x0C, height at 0x0E */
    /* 0x24 */ Vec3 pos;
    /* 0x30 */ PAD(30)[0x05];
    /* 0x35 */ u8 r, g, b, alpha, flags;
    /* 0x3A */ s8 offsetX, offsetY;
    /* 0x3C */ PAD(3c)[0x14];
    /* 0x50 */ f32 altitude;
    /* 0x54 */ Vec3 *normal;
    /* 0x58 */ PAD(58)[0x58];
} Garib;
#define GARIB_SPRITE 0x18

typedef struct GaribGroup {
    /* 0x00 */ PAD(00)[0x08];
    /* 0x08 */ PAD(08)[0xB8]; /* the list of garibs */
    /* 0xC0 */ Vec3 pos;
    /* 0xCC */ f32 radius;
    /* 0xD0 */ u8 suspended;
    /* 0xD1 */ u8 startActive;
    /* 0xD2 */ s16 tag;
    /* 0xD4 */ PAD(d4)[0x04];
} GaribGroup;
#define GARIB_LIST 0x08

#define INSTRUCTION_SIZE 0x48
typedef struct Enemy {
    /* 0x000 */ Actor actor;
    /* 0x0E4 */ PAD(e4)[0x4C];
    /* 0x130 */ u32 *normalList[30];
    /* 0x1A8 */ u32 *conditionalList[10];
    /* 0x1D0 */ u32 *attackList[30];
    /* 0x248 */ PAD(248)[0x02];
    /* 0x24A */ s16 normalCount;
    /* 0x24C */ s16 conditionalCount;
    /* 0x24E */ s16 attackCount;
    /* 0x250 */ PAD(250)[0x4C];
    /* 0x29C */ f32 areaSizeX; /* the confine box; -1 for none */
    /* 0x2A0 */ PAD(2a0)[0x60];
} Enemy;

typedef struct EnemyStat {
    s32 flags;
    f32 scale;
    s32 score;
} EnemyStat;

/* The heap's bookkeeping (jalloc.h): blocks 0 to lastStatic are the static ones, from the top of the heap down;
 * firstDynamic to 0x4FF are the dynamic ones, from the bottom up. */
#define JALLOC_BLOCKS 0x500
typedef struct JallocBlock {
    u32 addr;
    s32 size;
    char name[11];
    u8 used;
} JallocBlock;
typedef struct JallocControl {
    JallocBlock blocks[JALLOC_BLOCKS];
    u32 heapStart;
    s32 heapSize;
    s32 lastStatic;
    s32 firstDynamic;
    s32 staticUsed;
    s32 dynamicUsed;
} JallocControl;
extern JallocControl jallocControl;

extern Enemy enemies[];
extern EnemyStat enemyStats[];

_Static_assert(sizeof(Garib) == 0xB0 && __builtin_offsetof(Garib, tex) == 0x20 &&
                   __builtin_offsetof(Garib, r) == 0x35 && __builtin_offsetof(Garib, altitude) == 0x50,
               "Garib has the game's layout");
_Static_assert(sizeof(GaribGroup) == 0xD8 && __builtin_offsetof(GaribGroup, suspended) == 0xD0,
               "GaribGroup has the game's layout");
_Static_assert(sizeof(Enemy) == 0x300 && __builtin_offsetof(Enemy, attackList) == 0x1D0 &&
                   __builtin_offsetof(Enemy, areaSizeX) == 0x29C,
               "Enemy has the game's layout");

/* The HUD's marker stays until the level changes or restarts. */
static s8 modLevel;
static s32 modFrame;
static int spawnedCount; /* spawns since then */

/* What a spawn may use. Measured in Atlantis 1: a garib or a pick-up takes 2 to 4 blocks and up to 600 bytes,
 * an enemy up to 23 blocks and 2,100 bytes; the estimates below are several times that. The out-of-memory path of JallocAlloc stops the game for good, so a spawn needs these
 * free first, besides what it estimates for itself. The bytes are the gap between the static blocks and the
 * dynamic ones: JallocAllocStatic and JallocAllocDynamic check the free bytes in all, which holes inside the
 * dynamic part make larger than that gap. */
#define MAX_SPAWNS 100
#define RESERVE_BLOCKS 32
#define RESERVE_BYTES 0x8000
#define NEED_GARIB_BLOCKS 4
#define NEED_GARIB_BYTES 0x400
#define NEED_PICKUP_BLOCKS 12
#define NEED_PICKUP_BYTES 0x2000
#define NEED_ENEMY_BLOCKS 64
#define NEED_ENEMY_BYTES 0x8000

static const char *const pickupNames[NUM_PICKUPS] = {
    "Stun",        "Death",       "Frog",        "Suction",       "Hercules", "Speed up", "Rotor blades",
    "Bowling ball", "Power ball", "Ball bearing", "Beach ball",   "Snow ball", "Crystal ball",
    "Boomerang ball", "Vanish ball",
};

int ActorMovable(const Actor *a) {
    if (!SafePointer(a) || !SafeFloats(&a->pos.x, 3)) {
        return 0;
    }
    switch (a->type) {
        case TYPE_CAMERA:
        case TYPE_HAND:
        case TYPE_BALL:
        case TYPE_LANDSCAPE: /* its faces are built when the level loads */
        case TYPE_WATER:
        case TYPE_CAMEO:
            return 0;
        default:
            return 1;
    }
}

static int InActorList(const Actor *target) {
    int n = 0;

    for (const Actor *a = actorList.head.next; SafePointer(a) && a != &actorList.head && n < MAX_WALK;
         a = a->next, n++) {
        if (a == target) {
            return 1;
        }
    }
    return 0;
}

/* The platform that owns an actor, or NULL. */
static Platform *PlatformOf(const Actor *a) {
    Platform *candidate = (Platform *)((u8 *)a - PLATFORM_OFFSET);
    int n = 0;

    for (Platform *p = platformList.head.next; SafePointer(p) && p != &platformList.head && n < MAX_WALK;
         p = p->next, n++) {
        if (p == candidate) {
            return p;
        }
    }
    return NULL;
}

/* 1 if the heap has room for a spawn that needs this much, as the notes above say. */
static int HeapRoom(int blocks, s32 bytes) {
    const JallocControl *jc = &jallocControl;
    s32 first = jc->firstDynamic, last = jc->lastStatic;
    s32 top, bottom;

    if (last < -1 || first > JALLOC_BLOCKS || last >= first) {
        return 0;
    }
    if (first - last - 1 < blocks + RESERVE_BLOCKS) {
        return 0;
    }
    top = first == JALLOC_BLOCKS ? (s32)jc->heapStart : (s32)(jc->blocks[first].addr + jc->blocks[first].size);
    bottom = last < 0 ? (s32)(jc->heapStart + jc->heapSize) : (s32)jc->blocks[last].addr;
    return bottom - top >= bytes + RESERVE_BYTES;
}

/* Where an actor made or moved now goes. 0 if the glove's numbers are not safe to use. */
static int Front(Vec3 *out) {
    f32 x, y, z, yaw;

    if (!ReadFloat(&hand.pos.x, &x) || !ReadFloat(&hand.pos.y, &y) || !ReadFloat(&hand.pos.z, &z) ||
        !ReadFloat(&hand.rot.y, &yaw)) {
        return 0;
    }
    out->x = x + FRONT * __sinf(yaw);
    out->y = y + UP;
    out->z = z + FRONT * __cosf(yaw);
    return 1;
}

void ActorsModified(void) {
    run.actorsMod = 1;
    modLevel = gameInfo.level;
    modFrame = frameCount;
}

/* Whether the level has the model of a pick-up. FindObject with vital 0 only looks. */
static int PickupLoaded(int type) {
    ObjectInstance *inst = NULL;

    if (type < 0 || type >= NUM_PICKUPS || !SafePointer(collectableFileName[type])) {
        return 0;
    }
    FindObject(&inst, UpdateCRC(collectableFileName[type]), NULL, 0);
    return inst != NULL;
}

/* An enemy of this type that the level made, whose instruction lists can be copied, or NULL. */
static Enemy *EnemyTemplate(int type) {
    int n = numberOfEnemies;

    if (type < TYPE_FIRST_ENEMY || type >= ENEMY_TYPES || type == 6 ||
        (type >= TYPE_FIRST_BOSS && type <= TYPE_LAST_BOSS) || type == TYPE_NME_FROG || n < 1 || n > MAX_ENEMIES) {
        return NULL;
    }
    for (int i = 0; i < n; i++) {
        Enemy *e = &enemies[i];

        if (e->actor.type != type || e->normalCount < 1 || e->normalCount > 30 || e->conditionalCount < 0 ||
            e->conditionalCount > 10 || e->attackCount < 0 || e->attackCount > 30) {
            continue;
        }
        int ok = 1;

        for (int j = 0; j < e->normalCount; j++) {
            ok &= SafePointer(e->normalList[j]);
        }
        for (int j = 0; j < e->conditionalCount; j++) {
            ok &= SafePointer(e->conditionalList[j]);
        }
        for (int j = 0; j < e->attackCount; j++) {
            ok &= SafePointer(e->attackList[j]);
        }
        if (ok) {
            return e;
        }
    }
    return NULL;
}

static int EnemyLoaded(int type) {
    ObjectInstance *inst = NULL;

    if (EnemyTemplate(type) == NULL || !SafePointer(nmeObjectName[type])) {
        return 0;
    }
    FindObject(&inst, UpdateCRC(nmeObjectName[type]), NULL, 0);
    return inst != NULL;
}

static int KindAvailable(int kind) {
    if (kind == SPAWN_GARIB || kind == SPAWN_LIFE) {
        return 1;
    }
    if (kind >= SPAWN_PICKUP && kind < SPAWN_ENEMY) {
        return PickupLoaded(kind - SPAWN_PICKUP);
    }
    return numberOfEnemies < MAX_ENEMIES && EnemyLoaded(kind - SPAWN_ENEMY);
}

/* Makes a garib as the game does when an enemy drops one (KillEnemy): in the group that those use. It falls from
 * the place and settles on the floor. */
static void SpawnGarib(int type, const Vec3 *p) {
    GaribGroup *group = NMEGaribGroup;
    Garib *g;
    s32 platform;

    if (group == NULL) {
        group = JallocAlloc(sizeof(GaribGroup), 1, "GaribGroup");
        AddGaribGroup(group);
        InitGaribList((u8 *)group + GARIB_LIST);
        group->startActive = 1;
        group->pos = *p;
        group->radius = 30000.0f;
        NMEGaribGroup = group;
    }
    group->suspended = 0;
    g = JallocAlloc(sizeof(Garib), 1, "Garib");
    AddGarib(group, g);
    g->type = type;
    g->pos = *p;
    g->mode = 0;
    g->altitude = Altitude(&g->pos, &g->normal, &platform, 0);
    g->active = 12;
    g->scale = g->scaleTarget = 1.0f;
    /* A SpriteAnim is 0x10 bytes. The "mad garibs" cheat (0x2000) uses the fourth. */
    InitSpriteAnimation((u8 *)g + GARIB_SPRITE, garibAnimation + 0x10 * (type == 0 && (cheatActivated & 0x2000) ? 3 : type), 0);
    g->flags = 32;
    g->r = g->g = g->b = 255;
    if (SafePointer(g->tex)) {
        g->offsetX = -(s8)(g->tex[6] / 2);
        g->offsetY = -(s8)(g->tex[7] / 2);
    }
    AddSprite((u8 *)g + GARIB_SPRITE, NULL);
    /* The level file counts each garib in numGaribs; the end of a level compares the count with it. */
    if (type != 2) {
        numGaribs++;
    }
}

static void SpawnPickup(int type, const Vec3 *p) {
    Vec3 pos = *p;

    /* No respawn. The duration is the Power-ups menu's. */
    CreateNewPickup(type, &pos, 0, type >= PICKUP_BOWLINGBALL && type <= PICKUP_CRYSTALBALL ? -1 : 1000);
}

/* Makes an enemy as the level file does (LoadEnemy), then gives it the instruction lists of an enemy of its type.
 * LoadEnemy starts the first instruction when frameCount is not 0, and the lists do not exist yet. It runs with
 * frameCount at 0 and with interrupts off, which no other thread can see. With frameCount 0 JallocAlloc takes
 * the static allocator. Spawn has checked the room for that (HeapRoom). */
static void SpawnEnemy(int type, const Vec3 *p, f32 yaw) {
    Enemy *tpl = EnemyTemplate(type);
    int index = numberOfEnemies;
    Enemy *e = &enemies[index];
    s32 frames = frameCount;
    f32 face = yaw + PI;
    u32 mask;

    if (tpl == NULL || index >= MAX_ENEMIES) {
        return;
    }
    if (face >= 2.0f * PI) {
        face -= 2.0f * PI;
    }
    mask = __osDisableInt();
    frameCount = 0;
    LoadEnemy(type, p->x, p->y, p->z, -1, face);
    frameCount = frames;
    __osRestoreInt(mask);
    for (int j = 0; j < tpl->normalCount; j++) {
        e->normalList[j] = JallocAlloc(INSTRUCTION_SIZE, 1, "Instruct");
        for (int w = 0; w < INSTRUCTION_SIZE / 4; w++) {
            e->normalList[j][w] = tpl->normalList[j][w];
        }
    }
    for (int j = 0; j < tpl->conditionalCount; j++) {
        e->conditionalList[j] = JallocAlloc(INSTRUCTION_SIZE, 1, "Instruct");
        for (int w = 0; w < INSTRUCTION_SIZE / 4; w++) {
            e->conditionalList[j][w] = tpl->conditionalList[j][w];
        }
    }
    for (int j = 0; j < tpl->attackCount; j++) {
        e->attackList[j] = JallocAlloc(INSTRUCTION_SIZE, 1, "Instruct");
        for (int w = 0; w < INSTRUCTION_SIZE / 4; w++) {
            e->attackList[j][w] = tpl->attackList[j][w];
        }
    }
    e->normalCount = tpl->normalCount;
    e->conditionalCount = tpl->conditionalCount;
    e->attackCount = tpl->attackCount;
    e->areaSizeX = -1.0f;
    if (enemyStats[type].flags & 0x40) {
        numGaribs++; /* this enemy drops a garib (parse.c) */
    }
    NMEGetNewInstruction(e, 0, 1);
}

static void Spawn(int kind) {
    Vec3 p;
    f32 yaw;
    int blocks, bytes;

    if (!KindAvailable(kind) || !Front(&p) || !ReadFloat(&hand.rot.y, &yaw)) {
        return;
    }
    if (kind >= SPAWN_ENEMY) {
        blocks = NEED_ENEMY_BLOCKS, bytes = NEED_ENEMY_BYTES;
    } else if (kind >= SPAWN_PICKUP) {
        blocks = NEED_PICKUP_BLOCKS, bytes = NEED_PICKUP_BYTES;
    } else {
        blocks = NEED_GARIB_BLOCKS, bytes = NEED_GARIB_BYTES;
    }
    if (spawnedCount >= MAX_SPAWNS) {
        HudMessage("Spawn limit for this level");
        return;
    }
    if (!HeapRoom(blocks, bytes)) {
        HudMessage("Not enough memory to spawn");
        return;
    }
    spawnedCount++;
    if (kind == SPAWN_GARIB || kind == SPAWN_LIFE) {
        SpawnGarib(kind == SPAWN_LIFE ? 2 : 0, &p);
    } else if (kind < SPAWN_ENEMY) {
        SpawnPickup(kind - SPAWN_PICKUP, &p);
    } else {
        SpawnEnemy(kind - SPAWN_ENEMY, &p, yaw);
    }
    ActorsModified();
}

static void Move(Actor *a) {
    Vec3 p;
    Platform *plat = NULL;

    if (!InActorList(a) || !ActorMovable(a) || !Front(&p)) {
        return;
    }
    if (a->type == TYPE_PLATFORM) {
        plat = PlatformOf(a);
        if (plat == NULL) {
            return;
        }
    }
    a->pos = a->prevPos = a->platPrevPos = p;
    if (plat != NULL) {
        /* The faces of a platform with no motion are made once, when the level loads. Without the flag the game
         * makes them again from the position when something comes near, as for a platform that moves. */
        plat->flags &= ~PLATFORM_STATIC;
        plat->lastUpdate = 0;
    } else {
        a->vel.x = a->vel.y = a->vel.z = 0.0f;
    }
    ActorsModified();
}

void SpawnGameFrame(void) {
    Actor *move;
    int kind;
    u32 mask;

    if (!run.actorsMod && !run.spawnKind && run.moveActor == NULL) {
        return;
    }
    if (run.actorsMod && (gameInfo.level != modLevel || frameCount < modFrame)) {
        run.actorsMod = 0;
        spawnedCount = 0;
    }
    /* The menu (the controller thread) sets the requests at any time: they are taken with interrupts off. */
    mask = __osDisableInt();
    move = run.moveActor;
    kind = run.spawnKind - 1;
    run.moveActor = NULL;
    run.spawnKind = 0;
    __osRestoreInt(mask);
    if ((move == NULL && kind < 0) || !ActionsInLevel() || gamePaused || !SafePointer(hand.obj)) {
        return;
    }
    if (move != NULL) {
        Move(move);
    }
    if (kind >= 0) {
        Spawn(kind);
    }
}

/* The Spawn actor view: up and down choose a kind, A makes it. */
#define VIEW_X 16
#define VIEW_Y 28
#define VIEW_W 288
#define ROWS 15
#define ROW_H 10
#define MAX_KINDS 40

static u8 spawnKinds[MAX_KINDS];
static int spawnKindCount;
static int spawnSelected;
static int top;

static const char *SpawnKindName(int kind) {
    if (kind == SPAWN_GARIB) {
        return "Garib";
    }
    if (kind == SPAWN_LIFE) {
        return "Garib: extra life";
    }
    if (kind < SPAWN_ENEMY) {
        return pickupNames[kind - SPAWN_PICKUP];
    }
    return ActorTypeName(kind - SPAWN_ENEMY);
}

static int spawnClosed; /* the view opened where the level's data is not safe to read */

static void Open(void) {
    spawnSelected = 0;
    top = 0;
    spawnKindCount = 0;
    spawnClosed = !ActionsInLevel() || gamePaused;
    if (spawnClosed) {
        return;
    }
    for (int k = 0; k < SPAWN_ENEMY + ENEMY_TYPES && spawnKindCount < MAX_KINDS; k++) {
        if (KindAvailable(k)) {
            spawnKinds[spawnKindCount++] = k;
        }
    }
}

static int Input(u16 pressed, u16 held) {
    (void)held;
    if (pressed & CONT_B) {
        return 0;
    }
    if ((pressed & CONT_A) && spawnKindCount > 0) {
        run.spawnKind = spawnKinds[spawnSelected] + 1;
        MenuClose();
        return 1;
    }
    if (pressed & CONT_UP) {
        spawnSelected--;
    }
    if (pressed & CONT_DOWN) {
        spawnSelected++;
    }
    spawnSelected = spawnKindCount > 0 ? (spawnSelected + spawnKindCount) % spawnKindCount : 0;
    if (spawnSelected < top) {
        top = spawnSelected;
    }
    if (spawnSelected >= top + ROWS - 2) {
        top = spawnSelected - (ROWS - 2) + 1;
    }
    return 1;
}

static void Draw(Gfx **dl) {
    char buf[48];

    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 20 + ROWS * ROW_H + 14, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    Format(buf, sizeof(buf), "Spawn actor  %d/%d", spawnKindCount > 0 ? spawnSelected + 1 : 0, spawnKindCount);
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, 0xFFE070FF, buf);
    if (spawnClosed) {
        TextPrint(dl, VIEW_X + 6, VIEW_Y + 16, 1, 0xFF9060FF, "Only while a level runs");
    }
    for (int r = 0; r < ROWS - 2 && top + r < spawnKindCount; r++) {
        int k = spawnKinds[top + r];

        Format(buf, sizeof(buf), "%-18s %s", SpawnKindName(k),
               k >= SPAWN_ENEMY ? "enemy" : k >= SPAWN_PICKUP ? "power-up" : "garib");
        TextPrint(dl, VIEW_X + 6, VIEW_Y + 16 + r * ROW_H, 1, top + r == spawnSelected ? 0xFFE070FF : 0xFFFFFFFF, buf);
    }
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 16 + (ROWS - 2) * ROW_H, 1, 0xFF9060FF, "Modified game! May crash. Edits your save");
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 16 + (ROWS - 1) * ROW_H, 1, 0x8090A8FF, "Enemies: the types this level has");
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 16 + ROWS * ROW_H, 1, 0x8090A8FF, "A make it at Glover  B back");
}

const View spawnView = { Open, Input, Draw };
