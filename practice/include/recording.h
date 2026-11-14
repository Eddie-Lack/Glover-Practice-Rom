#ifndef PRACTICE_RECORDING_H
#define PRACTICE_RECORDING_H

#include "types.h"

/* Input recordings. Record saves a state and keeps the input that the game reads on each game frame after it.
 * Play loads the state and gives the game that input again. Both need full speed: frame advance, slow motion
 * or the practice menu stops them, because the controller thread then reads the controller on frames that the
 * game does not run. */
#define RECORDING_BUFFER 0x807DC000
#define RECORDING_BUFFER_SIZE 0x24000 /* 144 KB, the end of the Expansion Pak's memory */
#define RECORDING_MAX (30 * 60 * 20) /* game frames: 30 minutes at 20 a second; 4 bytes each fill 140.6 KB */

typedef struct Recording {
    u8 recording;
    u8 playing;
    u32 count; /* game frames recorded */
    u32 frame; /* while playing: the next one */
} Recording;

extern Recording recording;

/* The game thread, at the start of a game frame. */
void RecordingToggle(void);
void RecordingPlay(void);
/* The controller thread, after the practice input layer and before the game reads controller CNTLR. */
void RecordingFrame(u8 cntlr);

/* The Controller Pak (cpak.h): the recording as runs of equal input. Pack returns the length in bytes, or -1 if it
 * needs more than CAP bytes; Unpack replaces the recording and returns its game frames. */
#define RECORDING_RUN_SIZE 5
int RecordingPack(u8 *out, u32 cap, u32 *runs);
/* The same one run at a time, for the card file: RunAt packs the run that starts at game frame *I and moves *I past it;
 * Append adds a run to the recording (after the caller set recording.count to 0 for a new one). */
void RecordingRunAt(u32 *i, u8 *out);
void RecordingAppend(const u8 *in);
u32 RecordingUnpack(const u8 *in, u32 runs);

#endif
