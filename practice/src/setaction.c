/* Set action (setaction.h). */
#include "actions.h"
#include "game.h"
#include "hud.h"
#include "safe.h"
#include "setaction.h"

/* The glove actions of enums.h (HandAction) in the list, with the names the HUD shows. */
#define GLOVE_ACTIONS(X)                                                                                         \
    X("idle", 0) X("walk", 1) X("jump", 2) X("joined", 3) X("throw", 4) X("prewhack", 5) X("whack", 6)         \
    X("skidding", 7) X("duck", 8) X("release", 9) X("getup", 10) X("throwaim", 11) X("throw2cup", 12)           \
    X("falling", 13) X("bounce", 14) X("bounce2cup", 15) X("airwhack", 16) X("throw2land", 17) X("slam", 18)    \
    X("slam2st", 19) X("ballwalk", 20) X("crawl", 21) X("cartwheel", 22) X("climbup", 23) X("climbdown", 24)    \
    X("cling", 25) X("clingup", 26) X("dropdown", 27) X("hurt", 28) X("push", 29) X("lob", 30) X("catch", 31)   \
    X("rotor", 33) X("snatch", 34) X("sliding", 35) X("frog idle", 37) X("frog jump", 38) X("frog walk", 39)    \
    X("swish", 40) X("locate", 42) X("spin", 43)

#define NAME(n, m) n,
#define MODE(n, m) m,

const char *const setActionGloveNames[SETACTION_GLOVE_COUNT] = { GLOVE_ACTIONS(NAME) };
const u8 setActionGloveModes[SETACTION_GLOVE_COUNT] = { GLOVE_ACTIONS(MODE) };

const char *const setActionBallNames[SETACTION_BALL_COUNT] = { "burst", "release", "normal" };

/* The HUD's marker stays for 3 seconds of drawn frames (20 a second). */
#define MARKER_FRAMES 60

void SetActionRequest(int arg) {
    if (arg == 0) {
        run.setRequest = run.setGlove + 1;
    } else {
        run.setBallRequest = run.setBall + 1;
    }
}

/* The pointers that ApplyAction and the ball's functions follow. A level that is not running, a pause or a
 * level change leaves some of them empty. */
static int Ready(void) {
    return ActionsInLevel() && !gamePaused && SafePointer(hand.state) && SafePointer(hand.obj) &&
           SafePointer(hand.shadow) && SafePointer(hand.phys) && SafePointer(hand.anim) && SafePointer(ball.obj) &&
           SafePointer(ball.phys) && SafePointer(ball.state) && SafeFloats(&hand.pos.x, 3) &&
           SafeFloats(&ball.pos.x, 3);
}

void SetActionGameFrame(void) {
    if (!(run.setRequest | run.setBallRequest)) {
        return;
    }
    /* The menu (the controller thread) sets the requests at any time: they are taken with interrupts off. */
    u32 mask = __osDisableInt();
    int glove = run.setRequest - 1;
    int state = run.setBallRequest - 1;

    run.setRequest = 0;
    run.setBallRequest = 0;
    __osRestoreInt(mask);
    if (!Ready()) {
        return;
    }
    /* ApplyAction does nothing while an action lock and a cameo hold the glove. */
    if (glove >= 0 && glove < SETACTION_GLOVE_COUNT && !(handControl.actionLock && gameInfo.cameoActive)) {
        ApplyAction(setActionGloveModes[glove]);
        run.setMarker = MARKER_FRAMES;
    }
    if (state >= 0 && state < SETACTION_BALL_COUNT) {
        switch (state) {
            case 0:
                BurstBall();
                break;
            case 1:
                ReleaseBall();
                break;
            default:
                DropBall(0);
                break;
        }
        run.setMarker = MARKER_FRAMES;
    }
}
