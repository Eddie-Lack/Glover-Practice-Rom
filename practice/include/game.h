#ifndef PRACTICE_GAME_H
#define PRACTICE_GAME_H

/* The parts of the game and of its libraries that the practice code uses. The names, layouts and values come from
 * gloveworld (its include/ and symbol_addrs.txt); docs/game.md has notes. Structs list only the fields used, at
 * their offsets. */

#include "types.h"
#include "rdp.h"

#define PAD(n) u8 _pad##n

/* gameInfo.mode values (RunGame in main.c). */
#define GAME_MODE_MENU 1
#define GAME_MODE_LEVEL_SELECT 2
#define GAME_MODE_LEVEL 4
#define GAME_MODE_CAMEO 5
#define GAME_MODE_TIME_TRIAL 15

typedef struct GameInfo {
    /* 0x00 */ s8 mode;
    /* 0x01 */ s8 level;
    /* 0x02 */ s8 prevLevel;
    /* 0x03 */ s8 world;
    /* 0x04 */ PAD(04)[0x01];
    /* 0x05 */ s8 worldComplete[7]; /* indexed by exitType - 5; Space is 6 */
    /* 0x0C */ s16 fade;  /* the screen fades out when fade and fade2 reach 276 or more (ScreenFadedOut) */
    /* 0x0E */ s16 fade2;
    /* 0x10 */ PAD(10)[0x01];
    /* 0x11 */ s8 exitType; /* the exit that is running (enums.h, ExitType) */
    /* 0x12 */ s16 cameoActive; /* a cameo (the level's cut scene) runs */
    /* 0x14 */ PAD(14)[0x40];
    /* 0x54 */ u8 exitFlags; /* bit 0: an exit is running; bit 1: the level was finished (ExitLevel's complete) */
    /* 0x55 */ PAD(55)[0x01];
    /* 0x56 */ s16 language;
} GameInfo;

#define EXIT_CAVE 12 /* into the cave with the fountain, where the crystals go back */

extern GameInfo gameInfo;

typedef struct Vec3 {
    f32 x, y, z;
} Vec3;

typedef struct ActorState {
    /* 0x00 */ s8 hits;
    /* 0x01 */ u8 damage;
    /* 0x02 */ PAD(02)[2];
    /* 0x04 */ s16 mode;
    /* 0x06 */ s16 prevMode;
    /* 0x08 */ u8 flying;
    /* 0x09 */ u8 waterState;
} ActorState;

/* A model's mesh (actor.h). */
typedef struct Mesh {
    /* 0x00 */ s16 numFaces;
    /* 0x02 */ s16 numVerts;
    /* 0x04 */ Vec3 *verts;
    /* 0x08 */ s16 (*faces)[3]; /* vertex numbers of each face */
} Mesh;

/* A model (actor.h, Object): its name has no terminating zero when it fills the 8 bytes. */
typedef struct Object {
    /* 0x00 */ u32 crc;
    /* 0x04 */ char name[8];
    /* 0x0C */ PAD(0c)[0x08];
    /* 0x14 */ Mesh *mesh;
} Object;

typedef struct ObjectInstance {
    /* 0x00 */ PAD(00)[0x0C];
    /* 0x0C */ Object *obj;
} ObjectInstance;

/* A collision face (actor.h, CollFace): a plane, n.p + d = 0, and its triangle's corners in world space. */
typedef struct CollFace {
    /* 0x00 */ f32 normal[3];
    /* 0x0C */ f32 d;
    /* 0x10 */ f32 corner[3][3];
    /* 0x34 */ f32 edgeDir[3][3]; /* unit vector from corner i to corner i + 1 */
    /* 0x58 */ f32 edgeLen[3];    /* -1: the edge is shared with a flat neighbour and not tested */
    /* 0x64 */ f32 proj[4];       /* the inside test's projection (CollideActorWithPlane) */
    /* 0x74 */ u8 terrain; /* a row of terrain[] */
    /* 0x75 */ u8 axis[2];  /* the two coordinates that the inside test uses */
    /* 0x77 */ PAD(77)[1];
} CollFace;

_Static_assert(sizeof(CollFace) == 0x78 && __builtin_offsetof(CollFace, edgeLen) == 0x58 &&
                   __builtin_offsetof(CollFace, axis) == 0x75,
               "CollFace has the game's layout");

/* A collision sphere (actor.h, CollSphere). flags 0x20: at the actor's position; 0x10: pos is in world space;
 * otherwise pos is turned by the actor's rotation and added to its position (CalcCollisionSpheres). */
typedef struct CollSphere {
    /* 0x00 */ f32 pos[3];
    /* 0x0C */ PAD(0c)[0x0C];
    /* 0x18 */ f32 radius;
    /* 0x1C */ PAD(1c)[0x06];
    /* 0x22 */ s16 flags;
} CollSphere;

/* An actor's collision state (actor.h, ActorObject). planes[] are the faces that it touched on the last game
 * frame (CollFace pointers). */
typedef struct ActorObject {
    /* 0x000 */ CollFace *planes[4];
    /* 0x010 */ PAD(010)[0x48];
    /* 0x058 */ CollSphere spheres[8];
    /* 0x178 */ PAD(178)[0x10];
    /* 0x188 */ u8 onGround;
    /* 0x189 */ u8 wasOnGround;
    /* 0x18A */ u8 contactFlags;
    /* 0x18B */ u8 numSpheres;
} ActorObject;

/* The terrain kinds (actor.h, Terrain). */
typedef struct Terrain {
    /* 0x00 */ f32 slip;
    /* 0x04 */ f32 bounce;
    /* 0x08 */ PAD(08)[0x08];
    /* 0x10 */ f32 gravityScale;
    /* 0x14 */ PAD(14)[0x08];
} Terrain;
#define TERRAIN_COUNT 14
#define TERRAIN_WATER 12
extern Terrain terrain[TERRAIN_COUNT];

typedef struct Actor {
    /* 0x00 */ struct Actor *next;
    /* 0x04 */ struct Actor *prev;
    /* 0x08 */ struct Actor *collNext; /* collisionList */
    /* 0x0C */ struct Actor *collPrev;
    /* 0x10 */ PAD(10)[0x10];
    /* 0x20 */ s16 type; /* enums.h, ActorType */
    /* 0x22 */ s8 active;
    /* 0x23 */ PAD(23)[1];
    /* 0x24 */ s16 lifeTimer;
    /* 0x26 */ s16 alpha;
    /* 0x28 */ s32 flags;
    /* 0x2C */ struct Actor *link;
    /* 0x30 */ f32 radius;
    /* 0x34 */ Vec3 pos;
    /* 0x40 */ Vec3 prevPos;
    /* 0x4C */ Vec3 platPrevPos;
    /* 0x58 */ Vec3 vel;
    /* 0x64 */ Vec3 prevVel; /* the velocity at the start of the frame (MainGameLoop) */
    /* 0x70 */ f32 friction; /* the ground friction that MoveBall gives the ball for the next frame (ball.c) */
    /* 0x74 */ PAD(74)[0x04];
    /* 0x78 */ Vec3 rot;
    /* 0x84 */ PAD(84)[0x18];
    /* 0x9C */ f32 rotQuat[4]; /* x, y, z, w */
    /* 0xAC */ PAD(ac)[0x10];
    /* 0xBC */ Vec3 scale;
    /* 0xC8 */ ObjectInstance *inst;
    /* 0xCC */ void *shadow; /* ActorShadow */
    /* 0xD0 */ void *phys;   /* ActorPhysics: the trainers cast it */
    /* 0xD4 */ ActorState *state;
    /* 0xD8 */ ActorObject *obj;
    /* 0xDC */ void *anim; /* AnimState */
    /* 0xE0 */ struct CollFace *faces; /* collision faces in world space; the mesh gives their number */
} Actor; /* size 0xE4 */

_Static_assert(sizeof(Actor) == 0xE4 && __builtin_offsetof(Actor, prevVel) == 0x64 &&
                   __builtin_offsetof(Actor, friction) == 0x70 && __builtin_offsetof(Actor, rot) == 0x78,
               "Actor is 0xE4 bytes in the game");

/* An actor's physics constants (actor.h, ActorPhysics). */
typedef struct ActorPhysics {
    /* 0x00 */ f32 mass;
    /* 0x04 */ f32 accel;
    /* 0x08 */ f32 maxSpeed;
    /* 0x0C */ f32 moveSpeed;
    /* 0x10 */ f32 minSpeed;
    /* 0x14 */ PAD(14)[0x04];
    /* 0x18 */ f32 friction;
    /* 0x1C */ PAD(1c)[0x04];
    /* 0x20 */ f32 bounce;
} ActorPhysics;

extern Actor hand;
extern Actor ball;
/* The camera. Its rot.y turns the stick's direction into the glove's heading (controller.c, ProcessInput). */
extern Actor camera;

/* The game's camera state (camera.h, CameraInfo), partly. actor.pos is the point the camera looks at. */
typedef struct CameraInfo {
    /* 0x000 */ f32 distance;
    /* 0x004 */ Actor actor;
    /* 0x0E8 */ PAD(0e8)[0x44];
    /* 0x12C */ s32 overrideTimer;
    /* 0x130 */ s32 destActive;
    /* 0x134 */ PAD(134)[0x18];
    /* 0x14C */ s32 orbiting;
    /* 0x150 */ PAD(150)[0x08];
    /* 0x158 */ f32 focusAngle;
    /* 0x15C */ PAD(15c)[0x18];
    /* 0x174 */ f32 followSpeed; /* how far the target point may move toward the focus in a game frame: 20 at most */
    /* 0x178 */ s16 followTimer; /* game frames before followSpeed grows again */
    /* 0x17A */ PAD(17a)[0x2A];
    /* 0x1A4 */ s32 mode; /* 1: normal */
    /* 0x1A8 */ s32 blocked;
    /* 0x1AC */ s32 angleLock;
    /* 0x1B0 */ PAD(1b0)[0x18];
    /* 0x1C8 */ s16 fallTime;
} CameraInfo;

_Static_assert(__builtin_offsetof(CameraInfo, overrideTimer) == 0x12C && __builtin_offsetof(CameraInfo, orbiting) == 0x14C &&
                   __builtin_offsetof(CameraInfo, followSpeed) == 0x174 && __builtin_offsetof(CameraInfo, mode) == 0x1A4 && __builtin_offsetof(CameraInfo, fallTime) == 0x1C8,
               "CameraInfo has the game's layout");

extern CameraInfo cameraInfo;

/* The control method (controlMethod): 1 when the player moves the glove. */
extern s32 controlMethod;
#define CONTROL_HAND 1
/* The gravity (1.2 by default) and the glove's scale in the level (0.05 by default: the ball's radius is levelHandScale * 9 / 0.05). */
extern f32 gravity;
extern f32 levelHandScale;

/* libm's sine and cosine, which the game's own maths uses (maths.c). */
f32 __sinf(f32 x);
f32 __cosf(f32 x);

/* Every actor of the level, in a circular list through next and prev; the head is not an actor. */
typedef struct ActorList {
    /* 0x00 */ Actor head;
    /* 0xE4 */ s32 count;
} ActorList;

extern ActorList actorList;
/* The actors with collision faces, in a circular list through collNext. */
extern ActorList collisionList;

struct Platform;

typedef struct HandControl {
    /* 0x000 */ PAD(00)[0x08];
    /* 0x008 */ s16 actionLock;
    /* 0x00A */ PAD(0a)[0x10];
    /* 0x01A */ s16 actionTime;
    /* 0x01C */ PAD(1c)[0x08];
    /* 0x024 */ s16 noControlTimer;
    /* 0x026 */ s16 deathTimer;
    /* 0x028 */ PAD(28)[0x03];
    /* 0x02B */ u8 releaseTimer; /* the ball counts as free of the glove for this many game frames after a release */
    /* 0x02C */ PAD(2c)[0x4E4];
    /* 0x510 */ struct Platform *checkpoint; /* the restart gate where Glover comes back */
} HandControl;

extern HandControl handControl;

/* A checkpoint ("restart gate") is a platform with a restartGate (platform.c, parse.c). Setting
 * handControl.checkpoint and restartTimer = 65 makes RestartPlayer bring Glover back there, as the pause menu's
 * checkpoint choice does. Gate 0 is the level's start. */
typedef struct PlatformRestartGate {
    /* 0x00 */ s16 unk00;
    /* 0x02 */ s16 restartTimer;
    /* 0x04 */ Vec3 cameraPos;
    /* 0x10 */ f32 shadowAlt;
    /* 0x14 */ s16 untouched;
} PlatformRestartGate;

typedef struct Platform {
    /* 0x00 */ struct Platform *next;
    /* 0x04 */ struct Platform *prev;
    /* 0x08 */ PAD(08)[0x0C];
    /* 0x14 */ s32 flags; /* 0x84: no collision */
    /* 0x18 */ PAD(18)[0x14];
    /* 0x2C */ void *motion; /* PlatformMotion */
    /* 0x30 */ PAD(30)[0x24];
    /* 0x54 */ PlatformRestartGate *restartGate;
    /* 0x58 */ PAD(58)[0x16];
    /* 0x6E */ u16 lastUpdate; /* the low half of frameCount when its faces were last brought up to date */
    /* 0x70 */ PAD(70)[0x10];
    /* 0x80 */ Actor actor;
} Platform;

_Static_assert(__builtin_offsetof(Platform, motion) == 0x2C && __builtin_offsetof(Platform, lastUpdate) == 0x6E &&
                   __builtin_offsetof(Platform, actor) == 0x80,
               "Platform has the game's layout");
_Static_assert(__builtin_offsetof(Actor, scale) == 0xBC, "Actor has the game's layout");

typedef struct PlatformList {
    /* 0x00 */ s32 count;
    /* 0x04 */ Platform head;
} PlatformList;

extern PlatformList platformList;
extern s32 cheatActivated;
void ActivateCheat(u8 num, u8 show);
/* The option volumes, 0 to 160. */
extern s32 curSoundMUSVol;
extern s32 curSoundFXVol;

typedef struct BallControl {
    /* 0x00 */ s32 loseTimer;
    /* 0x04 */ PAD(04)[0x1A];
    /* 0x1E */ s8 state;
    /* 0x1F */ s8 type;
} BallControl;

extern BallControl ballControl;
/* 1: the ball stands still (ball.c). */
extern s32 ballStatic;

typedef struct Player {
    /* 0x00 */ PAD(00)[0x08];
    /* 0x08 */ s32 score;
    /* 0x0C */ s32 garibs;
    /* 0x10 */ s32 lives;
    /* 0x14 */ s32 health;
    /* 0x18 */ s32 levelFrames; /* the in-game timer, in game frames (20 a second on NTSC) */
    /* 0x1C */ s32 powerups[15]; /* each pick-up's time left; -1: until changed */
} Player;

/* The pick-ups (enums.h, Pickup): spells 0 to 6, ball types 7 to 14. ActivateCollectable gives one, as a pick-up
 * does; type -1 gives back the rubber ball. */
#define PICKUP_BOWLINGBALL 7
#define PICKUP_CRYSTALBALL 12
#define NUM_PICKUPS 15
void ActivateCollectable(s32 type, s32 time);

/* hand.c, ball.c. ApplyAction starts a glove action (HandAction in enums.h): it sets the mode, restarts the
 * action time and does the action's setup. */
void ApplyAction(s32 action);
/* The glove's actions (enums.h, HandAction) that the trainers use. */
#define HAND_JUMP 2
#define HAND_JOINED 3
#define HAND_PREWHACK 5
#define HAND_RELEASE 9
#define HAND_THROWAIM 11
#define HAND_BOUNCE 14
#define HAND_SLAM 18
#define HAND_BALLWALK 20
#define HAND_SPIN 43
/* ballControl.type (enums.h, BallMode) */
#define BALL_MODE_BEARING 3
void BurstBall(void);
void ReleaseBall(void);
void DropBall(s16 timer);

extern Player player;

typedef struct LevelInfo {
    /* 0x00 */ char name[0x28];
    /* 0x28 */ s32 objBank;
    /* 0x2C */ s32 bit;
    /* 0x30 */ s32 sfxDist;
    /* 0x34 */ u8 unk34;
    /* 0x35 */ u8 song;
    /* 0x36 */ s16 bonusTime;
} LevelInfo; /* size 0x38 */

#define NUM_LEVELS 48
#define LEVEL_ATLANTIS1 10
#define LEVEL_SPACEBOSS3 40
#define LEVEL_WAYROOM 42
#define LEVEL_PRESENT 43 /* the logos; FLYTHRU, FLYTHRU2, INTRO and OUTRO follow */
extern LevelInfo levelInfo[NUM_LEVELS];

typedef struct SaveSlot {
    /* 0x00 */ u8 name[3];
    /* 0x03 */ u8 unk03;
    /* 0x04 */ s32 levels;    /* bit n: the level whose levelInfo bit is n was finished */
    /* 0x08 */ s32 allGaribs; /* bit n: every garib of that level was collected in one visit */
    /* 0x0C */ s32 score;
    /* 0x10 */ u8 lives;
    /* 0x11 */ u8 difficultyLevel;
    /* 0x12 */ u8 hubTarget; /* crystals won */
    /* 0x13 */ u8 hubLevel;  /* crystals returned to the fountain */
    /* 0x14 */ s32 visited;  /* bit n: that level was loaded (written by StartNewLevel, read by nothing) */
    /* 0x18 */ s16 sfxVolume;
    /* 0x1A */ s16 musicVolume;
} SaveSlot; /* size 0x1C */

#define DIFFICULTY_EASY 0
#define DIFFICULTY_MEDIUM 1
#define DIFFICULTY_HARD 2
extern SaveSlot saveSlot[7];

/* The game's best times, in whole seconds: levelInfo[].bit picks the entry. All save files share them. */
typedef struct BestTime {
    /* 0x00 */ s16 time;
    /* 0x02 */ char name[3];
    /* 0x05 */ u8 unk05;
} BestTime; /* size 0x6 */
extern BestTime bestTimes[30];
extern s16 currentSaveSlot;
/* 1 from the frame a new game's difficulty is chosen (frontend2.c) until the next menu screen clears it. */
extern s32 toStartGame;

/* Game frames since the level started; frameCount stops while paused, frameCount2 does not. */
extern s32 frameCount;
extern s32 frameCount2;
extern s32 dispFrameCount;
extern u32 rseed;
/* The countdown before the in-game timer starts, in seconds (DoBoss, CheckForExits). */
extern s16 timer321;
/* Video frames per game frame that the scheduler aims for (3 on NTSC). */
extern u8 desiredFrameRate;
extern u8 gamePaused;
extern u8 pauseMode;

/* The controller (controller.c). controllerdata is libultra's OSContPad array; pad is the game's own state. */
typedef struct ContPad {
    u16 button;
    s8 stickX;
    s8 stickY;
    u8 errnum;
    PAD(05);
} ContPad;

#define CONT_A 0x8000
#define CONT_B 0x4000
#define CONT_Z 0x2000
#define CONT_START 0x1000
#define CONT_UP 0x0800
#define CONT_DOWN 0x0400
#define CONT_LEFT 0x0200
#define CONT_RIGHT 0x0100
#define CONT_L 0x0020
#define CONT_R 0x0010
#define CONT_CUP 0x0008
#define CONT_CDOWN 0x0004
#define CONT_CLEFT 0x0002
#define CONT_CRIGHT 0x0001
#define CONT_C_ALL (CONT_CUP | CONT_CDOWN | CONT_CLEFT | CONT_CRIGHT)

extern ContPad controllerdata[4];

/* The game's processed controller state (controller.c). pad[4] is the debug controls' entry. */
typedef struct Pad {
    /* 0x000 */ s16 stickX;
    /* 0x002 */ s16 stickY;
    /* 0x004 */ f32 mag; /* the plain length of the stick, D-pad included: 111.7 on a diagonal D-pad */
    /* 0x008 */ PAD(08)[0x12];
    /* 0x01A */ u8 button[14];
    /* 0x028 */ u8 prevButton[14];
    /* 0x036 */ u8 pressed[14];
    /* 0x044 */ PAD(44)[0x1C];
    /* 0x060 */ u8 latch[14]; /* 1: the game ignores the button until it is released */
    /* 0x06E */ PAD(6e)[0x02];
    /* 0x070 */ s32 holdTime[14];
    /* 0x0A8 */ PAD(a8)[0x38];
    /* 0x0E0 */ s32 sincePress[14]; /* game frames since the last press */
    /* 0x118 */ PAD(118)[0x78];
} Pad; /* size 0x190 */

/* The indices of pad[].button[] and the other per-button arrays. */
#define PADB_A 0
#define PADB_B 1
#define PADB_Z 2
#define PADB_START 3
#define PADB_L 4
#define PADB_R 5
#define PADB_CLEFT 10
#define PADB_CRIGHT 11
#define PADB_CUP 12
#define PADB_CDOWN 13

_Static_assert(__builtin_offsetof(Pad, mag) == 0x04 && __builtin_offsetof(Pad, button) == 0x1A &&
                   __builtin_offsetof(Pad, holdTime) == 0x70 && __builtin_offsetof(Pad, sincePress) == 0xE0 &&
                   sizeof(Pad) == 0x190,
               "Pad has the game's layout");

extern Pad pad[5];
extern u8 ActiveController;
void ReadController(u8 cntlr);
void ReadControllerFromData(u8 cntlr); /* demo playback: replaces controllerdata with the recording */
extern s32 autoPlayback; /* 1 records the input, 2 plays a demo */
/* The demo playback (controller.c): the recording, the next key and its frame, the buttons now, and the game
 * frames to the end. */
extern u16 *recordKeys;
extern u16 *rkp;
extern s16 nextKeyFrame;
extern s16 exitCount;
extern u16 playbackButtons;
extern s32 cheatCount;
extern u8 cfb_16_a[];
extern u8 cfb_16_b[];
void StopDrawing(char *name);
extern f32 globalFontScale;
extern s32 doFades;
void JallocFree(void *ptrRef);
void ProcessInput(void);
void ClearText(void);
void SeedRandom(void);

/* The game's display lists and graphics tasks (main.c). dynamic is two Dynamic structs of DYNAMIC_SIZE bytes;
 * the display list is at DYNAMIC_GLIST in each. tlistPointers holds ten task pointers for each frame buffer. */
#define DYNAMIC_SIZE 0x2B230
#define DYNAMIC_GLIST 0x4100
#define DYNAMIC_CAMERA_VIEW 0x4080 /* the camera's matrix (guLookAt), multiplied into the projection */
#define TASK_DATA_PTR 0x40  /* NNScTask.list.t.data_ptr */
#define TASK_DATA_SIZE 0x44 /* NNScTask.list.t.data_size, in bytes */
extern u8 dynamic[2][DYNAMIC_SIZE];
extern u8 *dynamicp;
extern void *tlistPointers[2][10];
extern void **tlistp;
/* The tasks of the frame that the draw thread has built and sends next, plus 1 (SendLastFrame). */
extern u8 currentTask;

void InitDisplayLists(void);
void ClearZBuffer(void);
void ClearFrameBuffer(void);
void CleanupAndSendDisplayList(s32 flag, s32 flags);
void SendLastFrame(void);
void PrintTimers(void);
extern Gfx *glistp;
extern u8 draw_buffer;

/* The level's fog (parse.c, TOKEN_FOG). The drawing code tests on; the logic does not read it. */
typedef struct Fog {
    /* 0x00 */ u8 r;
    /* 0x01 */ u8 g;
    /* 0x02 */ u8 b;
    /* 0x03 */ u8 on;
    /* 0x04 */ s16 min;
    /* 0x06 */ s16 max;
} Fog;

extern Fog fog;

/* 1 while the draw thread draws the world (StartDrawing, StopDrawing); 1 when it draws only the backdrops. */
extern volatile u8 gfxIsDrawing;
extern u8 onlyDrawBackdrops;
extern u16 zbuffer[];

/* The size of RDRAM in bytes, set by the console's boot code. */
extern u32 osMemSize;

/* libultra. Message queues are opaque here. */
typedef struct OSMesgQueue OSMesgQueue;
s32 osRecvMesg(OSMesgQueue *mq, void **msg, s32 flag);
void osCreateMesgQueue(OSMesgQueue *mq, void **msgs, s32 count);
void osSetEventMesg(u32 event, OSMesgQueue *mq, void *msg);
#define OS_EVENT_SI 5
#define OS_MESG_QUEUE_SIZE 0x18
#define OS_MESG_BLOCK 1
extern OSMesgQueue main_msgQ;

/* libultra's interrupt mask: __osDisableInt turns interrupts off and returns the old state. With interrupts off,
 * no other thread can run. */
u32 __osDisableInt(void);
void __osRestoreInt(u32 mask);

typedef s32 OSPri;
OSPri osGetThreadPri(void *thread);
void osSetThreadPri(void *thread, OSPri pri);

/* The start of libultra's thread context (OSThread). A thread that waits on a message queue has state
 * OS_STATE_WAITING and queue pointing at that queue. */
typedef struct OSThread {
    /* 0x00 */ struct OSThread *next;
    /* 0x04 */ OSPri priority;
    /* 0x08 */ void *queue;
    /* 0x0C */ struct OSThread *tlnext;
    /* 0x10 */ u16 state;
} OSThread;
#define OS_STATE_WAITING 8

/* The controller thread (ControllerProc), its stack and its priority. Its own frame message queue is on its
 * stack. */
extern OSThread ControllerThread;
extern u8 ControllerThreadStack[0x400];
extern u8 controllerPriority;

/* The scheduler's clients: each gets the frame messages in its queue (nnsched.h). */
typedef struct NNScClient {
    struct NNScClient *next;
    OSMesgQueue *msgQ;
} NNScClient;
extern u8 sched[];
#define SCHED_CLIENT_LIST 0x668 /* NNSched.clientList */
#define OS_MESG_NOBLOCK 0

/* The draw thread (DrawGraphics) and its message queue. It waits on the queue only at the top of its loop,
 * between frames. */
extern OSThread drawGraphicsThread;
extern OSMesgQueue gfx_msgQ;
/* The draw thread builds each frame's display list during one frame and sends its tasks, tlistp[1] to
 * tlistp[currentTask - 1], at the start of the next (SendLastFrame). Each task's list ends with a full sync. */

void DmaRomToRam(u32 rom, void *ram, s32 size);
void osWritebackDCache(void *addr, s32 size);
void osInvalICache(void *addr, s32 size);
void osWritebackDCacheAll(void);
/* The graphics tasks sent and not yet done (DrawGraphics). */
extern volatile s32 gfxTasks;
u32 osGetCount(void);
/* libultra's lock on the Peripheral Interface: the game's DMAs from the cartridge wait while it is held. */
void *osViGetCurrentFramebuffer(void);
void __osPiGetAccess(void);
void __osPiRelAccess(void);
/* The camera (main.c, SetupViewing): its position on each frame buffer, and the projection's values. */
extern Vec3 actualCamPos[2];
extern s16 fieldOfView;
extern s16 nearPlaneDist;
extern s16 farPlaneDist;
/* libultra's matrix functions. They write only their output. */
typedef struct Mtx {
    s32 m[16];
} Mtx;
void guPerspective(Mtx *m, u16 *perspNorm, f32 fovy, f32 aspect, f32 near, f32 far, f32 scale);
void guMtxF2L(f32 mf[4][4], Mtx *m);
/* The camera's matrix, with the light directions for the highlights (SetupViewing; ShadingMode is 2 in the game). */
void guLookAtHilite(Mtx *m, void *l, void *h, f32 xEye, f32 yEye, f32 zEye, f32 xAt, f32 yAt, f32 zAt, f32 xUp,
                    f32 yUp, f32 zUp, f32 xl1, f32 yl1, f32 zl1, f32 xl2, f32 yl2, f32 zl2, s32 twidth, s32 theight);
/* Counts video interrupts (libultra's VI manager). */
extern u32 __osViIntrCount;

/* The EEPROM: the game's save uses blocks 0 to 55. The controller thread does all EEPROM work, after its read of
 * the controllers, through controllerMsgQ (ControllerProc). */
extern OSMesgQueue controllerMsgQ;
extern s16 eepromPresent;
extern u8 eepromMessageQueue[6];
extern u8 eepromMessageNum;
/* The CRCs of the six files, the hiscore table and the best times (eeprom.c), as of the last load or save. */
extern u32 eepromCRCS[8];
u32 UpdateCRCData(u8 *data, s32 len);
/* Adds a request for the controller thread (eeprom.c): 4 and 5 save and load the best times, 6 and 7 the save
 * slots, 11 the CRCs. */
void PostEepromMessage(s16 msg);
/* 1 while the controller thread does EEPROM work. */
extern volatile s16 controllerBusy;
s32 osEepromProbe(OSMesgQueue *mq);
s32 osEepromLongRead(OSMesgQueue *mq, u8 address, u8 *buffer, int length);
s32 osEepromLongWrite(OSMesgQueue *mq, u8 address, u8 *buffer, int length);

/* The music driver (libmus). */
#define MUSFLAG_EFFECTS 1
#define MUSFLAG_SONGS 2
void MusStop(s32 flags, s32 speed);

/* The game's music (sound.c). PrepareSong stops the playing song and starts SONG, unless it is playing already;
 * 0 means none. */
typedef struct AudioBanks {
    /* 0x00 */ PAD(00)[0x18];
    /* 0x18 */ s32 current; /* the song playing */
    /* 0x1C */ PAD(1c)[0x04];
} AudioBanks;
extern AudioBanks audioCtrl;
void PrepareSong(u8 song);
void InitOverlayList(void);

/* A picture drawn over the scene (overlay.c). */
typedef struct Overlay {
    /* 0x00 */ PAD(00)[0x0C];
    /* 0x0C */ f32 x;
    /* 0x10 */ f32 y;
    /* 0x14 */ PAD(14)[0x0C];
    /* 0x20 */ u8 r;
    /* 0x21 */ u8 g;
    /* 0x22 */ u8 b;
    /* 0x23 */ u8 alpha;
    /* 0x24 */ void *bitmaps; /* NULL once DestroyOverlay has freed it */
    /* 0x28 */ PAD(28)[0x4C];
} Overlay; /* size 0x74 */
_Static_assert(sizeof(Overlay) == 0x74, "Overlay has the game's size");

/* The demos' controller display (LoadLevel, PrintStats). printController is 1 while it shows; the stick's
 * picture follows the stick, divided by rcfac, around the pad's place plus rcxoff and rcyoff; a button is lit for
 * rcdelay game frames after a press. */
#define PAD_OVERLAYS 11
extern Overlay controllerOL[PAD_OVERLAYS];
extern u8 printController;
extern s16 rcdelay;
extern f32 rcfac;
extern s16 rcxoff;
extern s16 rcyoff;
void LoadTextureBank(u32 num);
void FindAndSetupOverlay(Overlay *overlay, char *name, s32 x, s32 y, s32 z, s32 attr);
void PrintOverlay(Overlay *overlay);
void PrintStats(void);
void InitScreenDebris(void);

/* Levels (game.c). */
#define EXIT_QUIT 15 /* ExitLevel: leave for the title (mode 1), as the pause menu's Quit does */
#define LEVEL_FLYTHRU 44
#define EXIT_TIMETRIAL_RESTART 24 /* ExitLevel: free the level, then start gameInfo.level again (mode 8) */
#define GAME_MODE_SUB_LEVEL 8
#define SAVE_SLOT_LEVEL_SELECT 6 /* the slot that LevelSelectScreen and TimeTrialScreen use */
void ExitLevel(s32 type, s32 pause, s32 complete);
void FreeLevel(void);
void FreeBackdropList(void);
void StartNewGame(void);
void StartNewLevel(s32 num);
void memclear(void *ptr, s32 size);

/* The title (frontend.c, game.c). RunGame calls DoMenu while gameInfo.mode is 1. TitleScreen is the early title
 * menu that nothing calls (earlytitle.h). */
void DoMenu(void);
void TitleScreen(void);
void ClearAllText(void);
void KillTitle(void); /* frees what DoMenu made and sets mode 4 */
extern u8 onlyDrawBackdrops;
extern s32 levelSelectCount;
extern s32 currentScreen;
extern s32 booting;
extern u8 disablePause;

/* Creating actors at run time (spawn.h). FindObject with vital 0 only looks: with vital 1 a missing model stops the
 * level ("LEVEL UNPLAYABLE"). */
u32 UpdateCRC(char *ptr);
void FindObject(ObjectInstance **objCPtr, u32 objID, char *name, s32 vital);
void *JallocAlloc(s32 size, s32 clear, char *name);
void LoadEnemy(s32 enemyType, f32 x, f32 y, f32 z, s32 tag, f32 initYRot);
void NMEGetNewInstruction(void *enemy, s32 instructionNum, s32 overrideConditions);
void *CreateNewPickup(s32 type, Vec3 *pos, s32 time, s32 duration);
void AddGaribGroup(void *group);
void InitGaribList(void *list);
void AddGarib(void *group, void *garib);
void InitSpriteAnimation(void *sprite, void *anim, s32 lifespan);
void AddSprite(void *sprite, void *after);
f32 Altitude(Vec3 *pos, Vec3 **floor, s32 *plat, s32 coll);
extern char *collectableFileName[NUM_PICKUPS];
extern char *nmeObjectName[];
extern u8 garibAnimation[];
extern void *NMEGaribGroup;
extern s32 numberOfEnemies;
extern s32 numGaribs;

extern s32 ballLeftBehind;
extern s16 levelExitTimer;
extern s16 newLevExitTimer;
extern s16 worldExitTimer;

/* The level's records and the Hi Scores screen (game.c, credits.c). */
s32 CheckForLevelCompleted(void);
void printHiscores(char *name, s32 start);
struct Text *NEW_TEXT(s16 x, s16 y, u8 r, u8 g, u8 b, u8 a, f32 sx, f32 sy, u8 oneShot, char *str, u8 centre);
void SetFont(u8 type);
/* The Hi Scores screen's texts for the worlds and the five rows (index 6 to 10), for each language. */
extern char *hsLevNames[][3];
/* The slide-in offset of the Hi Scores rows: 250 at the start, 0 when they have arrived. */
extern f32 Xos1;

#endif
