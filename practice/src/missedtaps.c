/* Show the taps that the game missed (missedtaps.h). */
#include "actions.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "missedtaps.h"

MissedTaps missedTaps;

_Static_assert(__builtin_offsetof(MissedTaps, total) == 0x0C && __builtin_offsetof(MissedTaps, count) == 0x14 &&
                   __builtin_offsetof(MissedTaps, show) == 0x4C,
               "tests/missedtaps.py has the offsets");

/* libultra, as the game links it. The timer fires its message once (the library of this game has no osStopTimer).
 * The thread arms it again after each read for as long as the switch is on. */
void osCreateThread(void *thread, s32 id, void (*entry)(void *), void *arg, void *sp, OSPri pri);
void osStartThread(void *thread);
s32 osSetTimer(void *timer, u64 countdown, u64 interval, OSMesgQueue *mq, void *msg);
s32 __osSiRawStartDma(s32 dir, void *pifRam);
extern u8 __osContPifRam[64];
void osContGetReadData(ContPad *pads);
extern u8 __osContLastCmd;

#define OS_THREAD_SIZE 0x1B0
#define OS_TIMER_SIZE 0x20
#define COUNTER_RATE 46875000u
#define READ_INTERVAL (COUNTER_RATE / 200) /* 5 ms */
#define READ_TIMEOUT (COUNTER_RATE / 100)  /* 10 ms */
#define CONT_CMD_READ_BUTTON 1
#define SI_READ 0
#define SI_STATUS_REG ((volatile u32 *)0xA4800018)
#define MI_INTR_REG ((volatile u32 *)0xA4300008)
#define MI_INTR_SI 2 /* the serial interface's interrupt is raised */
#define SI_STATUS_BUSY 3 /* DMA busy, I/O busy */
#define SHOW_READS 40    /* two seconds at 20 game frames a second */

static u8 threadMem[OS_THREAD_SIZE] __attribute__((aligned(8)));
static u8 threadStack[0x800] __attribute__((aligned(16)));
static u8 timerMem[OS_TIMER_SIZE] __attribute__((aligned(8)));
static u8 queueMem[OS_MESG_QUEUE_SIZE] __attribute__((aligned(8)));
static void *queueBuf[2];
static u8 created;
static u8 failedShown; /* the HUD message for a failed read was shown */
static volatile u8 armed; /* the timer is set */

/* Buttons seen since the last game read. Both the thread and the controller thread change them, with interrupts
 * off. */
static u16 last;     /* the buttons at the latest sample */
static u16 gameHeld; /* the buttons at the previous game read */
static u8 edges[14]; /* presses started since the previous game read */
static u8 valid;     /* gameHeld is from a game read */

static void Sample(u16 held) {
    u16 rise = held & ~last;

    for (int b = 0; b < 14; b++) {
        if ((rise & padBits[b]) && edges[b] < 255) {
            edges[b]++;
        }
    }
    last = held;
}

/* The controller thread waits for its next frame message, on the queue that is a local of its own: it is not in
 * the middle of a controller read or of EEPROM work. */
static int ControllerWaits(void) {
    u8 *q = *(u8 *volatile *)&ControllerThread.queue;

    return *(volatile u16 *)&ControllerThread.state == OS_STATE_WAITING && q >= ControllerThreadStack &&
           q < ControllerThreadStack + sizeof(ControllerThreadStack);
}

/* Starts a transfer between RDRAM and the PIF (libultra's __osSiRawStartDma) and polls the serial interface until it
 * ends. Any write to SI_STATUS_REG clears the interrupt flag, so libultra never sends a message for it. The caller
 * has checked that no serial interrupt was waiting, so the flag is this transfer's own. Returns 0 if the transfer
 * did not start or did not end in time. */
static int Transfer(int dir) {
    u32 start = osGetCount();

    if (__osSiRawStartDma(dir, __osContPifRam) != 0) {
        return 0;
    }
    while ((*SI_STATUS_REG & SI_STATUS_BUSY) && osGetCount() - start < READ_TIMEOUT) {
    }
    if (*SI_STATUS_REG & SI_STATUS_BUSY) {
        return 0;
    }
    *SI_STATUS_REG = 0;
    return 1;
}

/* One extra read: the transfer that the game's own read makes while the last serial request was a read of the buttons
 * (osContStartReadData then only reads the PIF's answer, which the PIF has polled again), but without waiting for
 * a message. Interrupts stay off from the check to the end, so no other thread can use the serial interface in
 * between. The turn is also skipped while the serial interrupt is raised: it belongs to a request of the game, which
 * libultra has not handled yet. */
static void ExtraRead(void) {
    static ContPad pads[4];
    u32 mask = __osDisableInt();
    int done = 0;

    if (run.missedTaps && ControllerWaits() && __osContLastCmd == CONT_CMD_READ_BUTTON &&
        !(*SI_STATUS_REG & SI_STATUS_BUSY) && !(*MI_INTR_REG & MI_INTR_SI)) {
        if (Transfer(SI_READ)) {
            osContGetReadData(pads);
            if (pads[0].errnum == 0) {
                Sample(pads[0].button);
            }
            missedTaps.reads++;
            done = 1;
        } else if (*SI_STATUS_REG & SI_STATUS_BUSY) {
            /* Not finished: the serial interrupt will come later and libultra will send a message for it that the game
             * does not expect. The tool turns itself off (MissedTapsRead shows a message). */
            missedTaps.failed = 1;
            run.missedTaps = 0;
        }
    }
    if (!done) {
        missedTaps.skipped++;
    }
    __osRestoreInt(mask);
}

static void SetTimer(void) {
    osSetTimer(timerMem, READ_INTERVAL, 0, (OSMesgQueue *)queueMem, NULL);
}

static void Thread(void *arg) {
    void *msg;

    (void)arg;
    for (;;) {
        u32 mask;

        osRecvMesg((OSMesgQueue *)queueMem, &msg, OS_MESG_BLOCK);
        ExtraRead();
        /* With interrupts off, the controller thread cannot turn the switch on and call Start between the test and the
         * change of armed: Start then sees either a timer that this thread sets again, or armed 0. */
        mask = __osDisableInt();
        if (run.missedTaps) {
            SetTimer();
        } else {
            armed = 0;
        }
        __osRestoreInt(mask);
    }
}

static void Start(void) {
    u32 mask;

    if (!created) {
        created = 1;
        osCreateMesgQueue((OSMesgQueue *)queueMem, queueBuf, ARRAY_COUNT(queueBuf));
        osCreateThread(threadMem, 7, Thread, NULL, threadStack + sizeof(threadStack), controllerPriority - 1);
        osStartThread(threadMem);
    }
    mask = __osDisableInt();
    valid = 0;
    missedTaps.failed = 0;
    failedShown = 0;
    if (!armed) {
        armed = 1;
        SetTimer();
    }
    __osRestoreInt(mask);
    missedTaps.active = 1;
}

void MissedTapsRead(u16 held) {
    u32 mask;

    if (!run.missedTaps) {
        missedTaps.active = 0;
        valid = 0;
        for (int b = 0; b < 14; b++) {
            missedTaps.show[b] = 0;
        }
        if (missedTaps.failed && !failedShown) {
            failedShown = 1;
            HudMessage("missed taps: read failed, off");
        }
        return;
    }
    if (!missedTaps.active) {
        Start();
    }
    mask = __osDisableInt();
    Sample(held);
    if (valid) {
        u16 missed = 0;

        for (int b = 0; b < 14; b++) {
            u16 bit = padBits[b];
            int seen = (held & bit) && !(gameHeld & bit);

            if (edges[b] > seen) {
                missed |= bit;
                missedTaps.count[b] += edges[b] - seen;
                missedTaps.total += edges[b] - seen;
                missedTaps.show[b] = SHOW_READS;
            } else if (missedTaps.show[b] > 0) {
                missedTaps.show[b]--;
            }
        }
        if (missed) {
            missedTaps.last = missed;
        }
    }
    for (int b = 0; b < 14; b++) {
        edges[b] = 0;
    }
    gameHeld = last = held;
    valid = 1;
    missedTaps.gameReads++;
    __osRestoreInt(mask);
}

int MissedTapsShown(u16 bit) {
    for (int b = 0; b < 14; b++) {
        if (padBits[b] == bit) {
            return missedTaps.show[b] > 0;
        }
    }
    return 0;
}
