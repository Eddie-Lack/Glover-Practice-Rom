#ifndef PRACTICE_ACTIONS_H
#define PRACTICE_ACTIONS_H

#include "game.h"
#include "types.h"

/* The random number seed that SeedRandom sets at the start of every level (maths.c). */
#define RNG_LEVEL_START 0x6151F

/* The quick actions. A binding (bindings.h) or a menu item asks for one; the game thread carries out the ones
 * that change the game, at the start of its next game frame. */
enum Action {
    ACTION_RESET,         /* start the current level again */
    ACTION_WARP,          /* start the level chosen in the Warp menu */
    ACTION_FRAME_ADVANCE, /* stop or go on */
    ACTION_FRAME_STEP,    /* while stopped: one game frame */
    ACTION_SLOW_MOTION,   /* full speed, 1/2, 1/3, 1/4 */
    ACTION_HUD,           /* hide or show the whole HUD */
    ACTION_CHECKPOINT,    /* back to the last checkpoint, as after losing a life, but without losing it */
    ACTION_GOTO_CHECKPOINT, /* to the checkpoint chosen in the menu */
    ACTION_SAVE_STATE,    /* save the game's state (savestate.h) */
    ACTION_LOAD_STATE,    /* load it */
    ACTION_RECORD,        /* save a state and record the input from there, or stop recording (recording.h) */
    ACTION_PLAY,          /* load the state and play the recording */
    ACTION_COUNT
};

extern const char *const actionNames[ACTION_COUNT];

/* The practice code's state that is not saved. */
typedef struct Run {
    u8 frameAdvance; /* 1: the game waits for steps */
    u8 steps;        /* game frames to run while frameAdvance is on */
    u8 slowMotion;   /* 0: full speed; n: the game runs one frame in n + 1 */
    u8 hudHidden;
    u8 warpLevel;    /* the Warp menu's choices */
    u8 warpDifficulty;
    u8 checkpoint;   /* the Checkpoint menu's choice */
    u8 cheat;        /* a cheat to toggle on the next game frame, plus 1 */
    u8 pickup;       /* the Power-ups menu's choice: 0 the rubber ball, then the game's pick-ups */
    u8 givePickup;   /* 1: give that pick-up on the next game frame */
    u8 rngFrozen;    /* 1: the random number seed is set back to rngValue at the start of each game frame */
    u32 rngValue;
    u8 teleport;     /* 1: move Glover to teleportPos on the next game frame */
    Vec3 teleportPos;
    u8 musicMuted;
    u8 sfxMuted;
    s32 musicVolume; /* the game's volumes from before the mute */
    s32 sfxVolume;
    u8 collView;     /* the collision view (collview.h, CollViewMode) */
    u8 collRadius;   /* its radius: 500, 250 or 1000 units */
    u8 collSpheres;  /* 1: also the collision spheres of the glove and the ball */
    u8 rngCalls;     /* steps of the random number seed to make on the next game frame */
    u8 rngReset;     /* 1: set the seed to its value at a level's start on the next game frame */
    u8 frameLog;     /* 1: the frame log records each game frame of the glove (framelog.h) */
    u8 fogOff;       /* 1: the draw thread draws the level without its fog (practice.c, PracticeSendLastFrame) */
    u8 moonJump;     /* 1: the glove rises while the moon jump button is held (noclip.h) */
    u8 noclip;       /* 1: the glove flies free of collision and gravity (noclip.h) */
    u8 noclipSpeed;  /* the noclip speed: 2 << noclipSpeed units a game frame */
    u8 setGlove;     /* the Set action menu's glove action (an index in setaction.h's list) */
    u8 setBall;      /* its ball state */
    u8 setRequest;   /* the glove action to start on the next game frame, plus 1 */
    u8 setBallRequest; /* the ball state to start on the next game frame, plus 1 */
    u8 setMarker;    /* drawn frames left of the HUD's MODIFIED marker for a set action */
    u8 collEnemies;  /* 1: the collision view also draws the collision spheres of the enemies */
    u8 collFresh;    /* 1: it brings the faces of platforms up to date from their mesh, position and rotation */
    u8 collEdges;    /* 1: it marks the edges that belong to one face only */
    u8 spawnKind;    /* the kind to make on the next game frame, plus 1 (spawn.h) */
    u8 actorsMod;    /* 1: an actor was made or moved in this level; the HUD shows the MODIFIED marker */
    struct Actor *moveActor; /* the actor to bring to Glover on the next game frame, or NULL */
    u8 missedTaps;   /* 1: extra controller reads show the taps that the game missed (missedtaps.h) */
    u8 chaos;        /* chaos inputs: 0 off, 1 rare, 2 sometimes, 3 often (chaos.h) */
    u8 freeCam;      /* the free camera: 0 off; n: it flies 2^n units a controller read, which is once a game frame (freecam.h) */
    u8 ghost;        /* 1: the draw thread draws the ghost of the level's recorded run (ghost.h) */
    u8 trainers;     /* the trick trainers that are on, one bit each (trainers.h) */
    u8 warpLives;    /* the Warp menu's lives: 0 keeps them, n sets n - 1 */
    u8 warpGaribs;   /* the garibs the warped-to level starts with */
    u8 warpPower;    /* the Warp menu's power-up: 0 none, then as pickup (the rubber ball first) */
    u8 collHoles;    /* 1: the collision view also marks the holes where a falling glove touches no face (moonaim.h) */
    u8 cameraSpeed;  /* 0 normal, 1 fast, 2 faster: how fast the camera follows (camspeed.h) */
    u8 padDisplay;   /* the game's controller display: 0 off, 1 controller 1, 2 controller 2 (paddisplay.h) */
} Run;

extern Run run;

/* Asks for an action. Safe from any thread. */
void ActionRequest(int action);
/* The game thread, at the start of each game frame (PracticeClearText). Returns after any waiting that frame
 * advance or slow motion asks for. */
void ActionsGameFrame(void);
/* The number of save, load, record and play requests that waited one frame to show "saving..." or "loading...". */
extern u32 actionsAnnounced;
/* Mutes the music (arg 0) or the sound effects (arg 1), or turns them back on. */
void ActionsToggleMute(int arg);
/* Leaves the level through the Time Trial's restart exit to start LEVEL, or starts LEVEL from the title.
 * Returns 1 if a start is under way. */
int ActionsRestart(int level);
/* Whether a level that can be reset or left is running. */
int ActionsInLevel(void);
/* Whether the title (the menu, or the title flythrough) is showing. */
int ActionsOnTitle(void);
/* The number of checkpoints in the level, and checkpoint i (0 is the start), or NULL. */
int CheckpointCount(void);
struct Platform *CheckpointAt(int i);

#endif
