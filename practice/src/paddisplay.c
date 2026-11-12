/* The game's own controller display. The game draws it only in a demo: LoadLevel loads the texture bank RCDEMO
 * (18) and sets up eleven overlays, and PrintStats draws them in place of the hearts and the lives. This draws the
 * same overlays during play, next to the game's own HUD, for controller 1 or 2. The textures are loaded at the level
 * start, so a change takes effect at the next level start or reset. */
#include "actions.h"
#include "game.h"
#include "hide.h"
#include "input.h"
#include "paddisplay.h"

/* The display sits this far above its place in the demos, clear of the hearts. */
#define SHIFT_Y (-40)

/* The overlays' textures, places and buttons, as LoadLevel and PrintStats have them. */
static const struct {
    const char *name;
    s16 x, y;
    s8 button; /* the pad[].button index that lights it, or -1 */
} parts[PAD_OVERLAYS] = {
    { "padnobut1.bmp", 46, 182, -1 },
    { "padnobut2.bmp", 94, 182, -1 },
    { "a_button.bmp", 92, 171, PADB_A },
    { "b_button.bmp", 85, 164, PADB_B },
    { "z_button.bmp", 70, 200, PADB_Z },
    { "cu_button.bmp", 99, 152, PADB_CUP },
    { "cd_button.bmp", 99, 164, PADB_CDOWN },
    { "cl_button.bmp", 93, 158, PADB_CLEFT },
    { "cr_button.bmp", 105, 158, PADB_CRIGHT },
    { "stick.bmp", 160, 150, -1 },
    { "r_button.bmp", 103, 142, PADB_R },
};
#define STICK 9

/* Controller 2 has no entry in pad[] (pad[4] holds it only for the debug controls). */
static struct {
    s16 stickX, stickY;
    u8 button[14];
    s32 sincePress[14];
} pad2;

void PadDisplayRead(u16 held, s8 stickX, s8 stickY) {
    for (int b = 0; b < 14; b++) {
        u8 down = (held & padBits[b]) != 0;
        pad2.sincePress[b] = down && !pad2.button[b] ? 0 : pad2.sincePress[b] + 1;
        pad2.button[b] = down;
    }
    pad2.stickX = stickX;
    pad2.stickY = stickY;
}

void PadDisplayLevelStart(void) {
    /* A demo has the game's own display; the logos, the title flythrough and the cameos have no HUD. */
    if (!run.padDisplay || autoPlayback == 2 || gameInfo.level >= LEVEL_PRESENT) {
        return;
    }
    LoadTextureBank(18);
    for (int i = 0; i < PAD_OVERLAYS; i++) {
        FindAndSetupOverlay(&controllerOL[i], (char *)parts[i].name, parts[i].x, parts[i].y + SHIFT_Y, 0, 1);
    }
}

static f32 Follow(f32 at, f32 centre, s16 stick) {
    f32 d = at - centre;
    if (stick == 0 && d > -2 && d < 2) {
        return centre;
    }
    return at + (centre + stick / rcfac - at) / 2.0f;
}

/* PrintStats draws the HUD; overlay.c calls this in its place. */
void PracticePrintStats(void) {
    Overlay *ol = controllerOL;
    const u8 *button;
    const s32 *since;
    s16 sx, sy;

    HidePrintStats();
    /* FreeLevel2 frees the overlays' bitmaps at each level's end: a level started without the display (or a state
     * saved in one) has none. The first overlay tells; the game never frees the last one's bitmaps. */
    if (!run.padDisplay || run.hudHidden || printController || frameCount <= 0 || ol[0].bitmaps == NULL) {
        return;
    }
    if (run.padDisplay == 1) {
        Pad *p = &pad[ActiveController];
        button = p->button;
        since = p->sincePress;
        sx = p->stickX;
        sy = p->stickY;
    } else {
        button = pad2.button;
        since = pad2.sincePress;
        sx = pad2.stickX;
        sy = pad2.stickY;
    }
    ol[STICK].x = Follow(ol[STICK].x, ol[0].x + rcxoff, sx);
    ol[STICK].y = Follow(ol[STICK].y, ol[0].y + rcyoff, -sy);
    for (int i = 0; i < PAD_OVERLAYS; i++) {
        int b = parts[i].button;
        if (b >= 0) {
            ol[i].r = ol[i].g = ol[i].b = button[b] || rcdelay >= since[b] ? 255 : 100;
        }
        PrintOverlay(&ol[i]);
    }
}
