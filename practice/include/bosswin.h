#ifndef PRACTICE_BOSSWIN_H
#define PRACTICE_BOSSWIN_H

#include "rdp.h"
#include "types.h"

/* The boss window trainer (trainers.h, TRAINER_BOSS; bosswin.c): in the Atlantis boss, the Prehistoric boss and the
 * last Space fight (SPACEBOSS3) it shows when a hit counts, how late each hit came, the touches that were wasted
 * and why, and the boss's own timers. It reads the game and writes nothing to it, calls no Random and changes no input.
 * docs/game.md ("The bosses") has the numbers.
 *
 * Pass numbers. "Pass N" is the run of MainGameLoop in which frameCount is N. The hook (BossRead) runs at the start of
 * the message for pass N and sees the state that pass N - 1 and its draw left. A touch that the hook sees is judged
 * by the boss in pass N (Atlantis, Space) or was judged in pass N - 1 (Prehistoric). A hit that the hook sees went
 * down in pass N - 1. */

enum BossId { BOSS_NONE, BOSS_ATLANTIS, BOSS_PREHIST, BOSS_SPACE };

/* The parts that have a window of their own. */
enum BossChan { BCH_WHALE, BCH_CRAB, BCH_FISH, BCH_PREHIST, BCH_LEFT, BCH_RIGHT, BCH_HEAD, BCH_COUNT };

enum BossEventKind {
    BEK_NONE,
    BEK_HIT,    /* a hit: gap since the part's last hit, late against its window */
    BEK_SPLIT,  /* the whale's third hit, which splits it */
    BEK_KILL,   /* the crab or the fish dies */
    BEK_EARLY,  /* a touch before the window opened; nothing was lost (the whale) */
    BEK_LOST    /* a touch that the boss did not count, and the shot or snowball is gone */
};

/* Why a touch was wasted (BossEvent.why). */
enum BossWhy { BWY_NONE, BWY_EARLY, BWY_HURT, BWY_SLOW, BWY_SMALL, BWY_UNKNOWN };

/* A part's window (BossWin.state). */
enum BossWinState {
    BWS_NONE,    /* the part cannot be hit now (dead, not the phase, or it has no window) */
    BWS_READY,   /* it can be hit and no hit has been seen yet: there is no time to count */
    BWS_CLOSED,  /* n passes until a touch counts */
    BWS_OPEN     /* a touch counts, and has counted for n passes */
};

#define BOSS_EVENTS 8

typedef struct BossEvent {
    /* 0x00 */ u8 kind;   /* BossEventKind */
    /* 0x01 */ u8 chan;   /* BossChan */
    /* 0x02 */ u8 why;    /* BossWhy */
    /* 0x03 */ s8 late;   /* passes after the window opened (HIT); -1: there was no window to judge against */
    /* 0x04 */ s16 gap;   /* passes since the part's last hit (0: none seen) */
    /* 0x06 */ s16 n;     /* passes early (EARLY, LOST) or passes of the window left (BWY_HURT) */
} BossEvent;

typedef struct BossWin {
    /* 0x00 */ u8 state;  /* BossWinState */
    /* 0x01 */ u8 pad01;
    /* 0x02 */ s16 n;
} BossWin;

/* BossView.flags */
#define BWF_SNOW_READY 0x01  /* Prehistoric: the ball is a snowball and fast enough */
#define BWF_TOUCH_GROUND 0x02 /* Atlantis: the ball touches the whale but is on the ground or held, so it does not count */
#define BWF_TOUCH_NOW 0x04   /* Atlantis: the ball touches the whale now */
#define BWF_LEAVE 0x08       /* Space: the boss leaves after its next laser */

typedef struct BossView {
    /* 0x00 */ u8 live;       /* 1: the fight is on and the rest is valid */
    /* 0x01 */ u8 boss;       /* BossId */
    /* 0x02 */ u8 hits;       /* enemies[0]'s hits */
    /* 0x03 */ u8 maxHits;
    /* 0x04 */ u8 part[3];    /* Space: the left shoulder's, the right shoulder's and the head's health */
    /* 0x07 */ u8 nEvents;    /* entries of events[], newest first */
    /* 0x08 */ u8 flags;      /* BWF_ */
    /* 0x09 */ u8 phase;      /* Atlantis: 0 whale, 1 crab, 2 fish, 3 done */
    /* 0x0A */ u8 shots;      /* Space: shots in the air */
    /* 0x0B */ u8 nWin;       /* entries of win[] that count */
    /* 0x0C */ u32 frame2;    /* frameCount2 of the game frame that made this view */
    /* 0x10 */ u32 lastFrame2; /* frameCount2 when events[0] happened */
    /* 0x14 */ BossWin win[3]; /* the focus part first; Space: left, right, head */
    /* 0x20 */ s16 lasers;    /* Space, head phase: lasers left before the boss leaves; -1 when not shown */
    /* 0x22 */ s16 away;      /* Space: passes the boss is out of reach; 0 when it is not */
    /* 0x24 */ s16 reload;    /* Space: passes until the next shot can be fired */
    /* 0x26 */ s16 crab;      /* Atlantis: passes left to kill the crab; -1 when it is not on its back, 0 when it gets up */
    /* 0x28 */ s16 fish;      /* Atlantis: passes of stun left; -1 when it is not stunned */
    /* 0x2A */ s16 pad2A;
    /* 0x2C */ f32 ballRadius; /* Prehistoric */
    /* 0x30 */ f32 ballSpeed;
    /* 0x34 */ BossEvent events[BOSS_EVENTS];
} BossView;

_Static_assert(sizeof(BossEvent) == 8 && sizeof(BossView) == 0x74 && __builtin_offsetof(BossView, events) == 0x34,
               "tests/bosstrainer.py has the offsets");

/* The game thread, at the hook (TrainersGameFrame), only when the trainer is on. Reads the fight into a snapshot.
 * Returns 1 when a fight is on and readable. */
int BossRead(void);
/* The game thread, once a game frame (TrainersProcessInput), after a BossRead that returned 1 in this game frame: works
 * out the window, the hits and the touches. */
void BossStep(void);
/* Forgets everything and empties the view (a level start, a savestate load, the switch). */
void BossReset(void);
/* Copies the view. */
void BossFillView(BossView *out);
/* The draw thread. */
void BossDraw(Gfx **dl, const BossView *v);

#endif
