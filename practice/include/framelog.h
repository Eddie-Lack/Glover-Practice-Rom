#ifndef PRACTICE_FRAMELOG_H
#define PRACTICE_FRAMELOG_H

#include "types.h"

/* The frame log: for each game frame in which the glove moves (MoveHand), what the game read from controller 1
 * and the glove's state at the start of that frame. The next entry shows the result. It records while
 * Run.frameLog is on. */
#define FRAME_LOG_SIZE 64

typedef struct FrameLogEntry {
    u32 frame;      /* game frames of the glove since the log started */
    u16 held;       /* pad[0].button as CONT_ bits */
    u16 pressed;    /* pad[0].pressed */
    u16 latched;    /* pad[0].latch: held buttons that the game ignores until they are released */
    s16 mode;       /* hand.state->mode at the start of the frame (enums.h, HandAction) */
    s16 actionTime;
    s16 actionLock; /* while it is not 0, no new action starts */
    s16 noControl;  /* while it is not 0, the game reads no buttons but Start */
    u8 reads;       /* controller reads since the last entry: 1, unless the read and the game fell out of step */
    u8 pad;
} FrameLogEntry;

typedef struct FrameLog {
    FrameLogEntry entries[FRAME_LOG_SIZE];
    u32 count; /* entries written; the newest is entries[(count - 1) % FRAME_LOG_SIZE] */
} FrameLog;

extern FrameLog frameLog;
/* Controller reads since the last entry (the controller thread). */
extern volatile u8 frameLogReads;

/* The game thread, before the glove's frame (PracticeProcessInput), while the log records. */
void FrameLogTick(void);
void FrameLogClear(void);

#endif
