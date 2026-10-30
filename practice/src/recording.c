/* Input recordings, on top of the savestate. */
#include "actions.h"
#include "game.h"
#include "ghost.h"
#include "hud.h"
#include "menu.h"
#include "recording.h"
#include "savestate.h"

/* The recording keeps its own state, in the pool slot STATE_SLOT_RECORD. The slot that the player chose is not touched. */

/* One game frame of input: the buttons and the stick, as in libultra's OSContPad. */
typedef struct RecordedInput {
    u16 button;
    s8 stickX;
    s8 stickY;
} RecordedInput;

_Static_assert(RECORDING_BUFFER >= STATE_POOL_END && RECORDING_BUFFER + RECORDING_MAX * 4 <= 0x80800000,
               "the recording must not overlap the savestate pool");

Recording recording;

static RecordedInput *const inputs = (RecordedInput *)RECORDING_BUFFER;

void RecordingToggle(void) {
    if (recording.recording) {
        recording.recording = 0;
        HudMessage("recording stopped");
        return;
    }
    recording.playing = 0;
    if (StateSaveSlot(STATE_SLOT_RECORD)) {
        recording.count = 0;
        recording.recording = 1;
        GhostRecordStart();
        HudMessage("recording");
    } else if (!StateSlotUsed(STATE_SLOT_RECORD)) {
        /* The save emptied the slot: the old recording has no state to start from. */
        recording.count = 0;
    }
}

void RecordingPlay(void) {
    recording.recording = 0;
    if (recording.count == 0) {
        HudMessage("nothing recorded");
        return;
    }
    if (StateLoadSlot(STATE_SLOT_RECORD)) {
        recording.frame = 0;
        recording.playing = 1;
        HudMessage("playing");
    }
}

static int FullSpeed(void) {
    return !menuOpen && !run.frameAdvance && !run.slowMotion;
}

void RecordingFrame(u8 cntlr) {
    ContPad *c = &controllerdata[cntlr];

    if (recording.playing) {
        if (!FullSpeed()) {
            recording.playing = 0;
            HudMessage("playback stopped");
            return;
        }
        c->button = inputs[recording.frame].button;
        c->stickX = inputs[recording.frame].stickX;
        c->stickY = inputs[recording.frame].stickY;
        if (++recording.frame >= recording.count) {
            recording.playing = 0;
            HudMessage("playback done");
        }
    } else if (recording.recording) {
        if (!FullSpeed() || recording.count >= RECORDING_MAX) {
            recording.recording = 0;
            HudMessage("recording stopped");
            return;
        }
        inputs[recording.count].button = c->button;
        inputs[recording.count].stickX = c->stickX;
        inputs[recording.count].stickY = c->stickY;
        recording.count++;
    }
}

/* The Controller Pak stores a recording as runs of 5 bytes (cpak.h): u16 buttons, s8 stick x, s8 stick y, and the
 * number of game frames (1 to 255) with this input. */

/* Packs the run of equal input that starts at game frame *I into OUT (RECORDING_RUN_SIZE bytes) and moves *I past
 * it. */
void RecordingRunAt(u32 *i, u8 *out) {
    u32 j = *i + 1;

    while (j < recording.count && j - *i < 255 && inputs[j].button == inputs[*i].button &&
           inputs[j].stickX == inputs[*i].stickX && inputs[j].stickY == inputs[*i].stickY) {
        j++;
    }
    out[0] = (u8)(inputs[*i].button >> 8);
    out[1] = (u8)inputs[*i].button;
    out[2] = (u8)inputs[*i].stickX;
    out[3] = (u8)inputs[*i].stickY;
    out[4] = (u8)(j - *i);
    *i = j;
}

/* Packs the recording into OUT. Returns the length in bytes, or -1 if it needs more than CAP bytes. RUNS gets the
 * number of runs. */
int RecordingPack(u8 *out, u32 cap, u32 *runs) {
    u32 n = 0;
    u32 length = 0;

    for (u32 i = 0; i < recording.count; n++, length += RECORDING_RUN_SIZE) {
        if (length + RECORDING_RUN_SIZE > cap) {
            return -1;
        }
        RecordingRunAt(&i, out + length);
    }
    *runs = n;
    return (int)length;
}

/* Adds the run at IN to the end of the recording. */
void RecordingAppend(const u8 *in) {
    for (u32 k = 0; k < in[4] && recording.count < RECORDING_MAX; k++, recording.count++) {
        inputs[recording.count].button = (u16)(in[0] << 8 | in[1]);
        inputs[recording.count].stickX = (s8)in[2];
        inputs[recording.count].stickY = (s8)in[3];
    }
}

/* Takes a recording from RUNS runs at IN and returns its game frames. The caller checked that the data is there. */
u32 RecordingUnpack(const u8 *in, u32 runs) {
    recording.count = 0;
    for (u32 r = 0; r < runs; r++, in += RECORDING_RUN_SIZE) {
        RecordingAppend(in);
    }
    recording.frame = 0;
    return recording.count;
}
