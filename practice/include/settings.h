#ifndef PRACTICE_SETTINGS_H
#define PRACTICE_SETTINGS_H

#include "types.h"

/* The HUD items. Each can be on or off, and placed anywhere. */
enum HudItem {
    HUD_TIMER,  /* the in-game timer */
    HUD_INPUT,  /* the input display */
    HUD_GLOVE,  /* the glove's position and speed */
    HUD_BALL,   /* the ball's position and speed */
    HUD_ACTION, /* the glove's action and how long it has lasted */
    HUD_FRAMES, /* the game frame counter */
    HUD_LAG,    /* the lag counter */
    HUD_RNG,    /* the random number seed */
    HUD_LAST,   /* the in-game time of the last level played */
    HUD_GAME,   /* the full-game timer and the last split */
    HUD_WATCH,  /* the memory watch list */
    HUD_COUNT
};

/* The number of HUD items that the saved settings have room for. */
#define HUD_SLOTS 13
/* The number of actions that a binding can hold. */
#define BIND_SLOTS 12
/* The menu opacity steps (clear to solid) and the default step; the position choices are 3 by 3, the default 2, 2. */
#define MENU_OPACITY_STEPS 6
#define MENU_OPACITY_DEFAULT 5
/* The trainer log's length and the result hold (trainers.h) are stored from 1 like the menu look. */
#define TRAINER_LOG_STEPS 5
#define TRAINER_LOG_DEFAULT 2  /* 5 lines */
#define TRAINER_HOLD_STEPS 4
#define TRAINER_HOLD_DEFAULT 2 /* 1 second */

typedef struct HudPlace {
    u8 on;
    u8 x; /* in units of 2 pixels */
    u8 y; /* in units of 2 pixels */
} HudPlace;

/* The practice settings in RAM. The tests write some of these fields at their offsets (tests/layout.py). */
typedef struct Settings {
    /* 0x00 */ u32 magic;
    /* 0x04 */ u8 version;
    /* 0x05 */ u8 ballNormal;   /* 1: the retail ball controls, reversed on Medium and Hard */
    /* 0x06 */ u8 menuPauses;   /* 1: the game stops while the practice menu is open */
    /* 0x07 */ u8 hudShadow;    /* 1: text has a shadow */
    /* 0x08 */ u8 timerFrames;  /* 1: the timer also shows game frames */
    /* 0x09 */ u8 inputHistory; /* 1: the input display also lists the last presses */
    /* 0x0A */ u8 debugPad;     /* 1: controller 2 drives the game's debug controls (pad[4]) */
    /* 0x0B */ u8 splitByCrystal; /* 1: the full-game timer splits at each crystal, not at each level */
    /* 0x0C */ u16 bindButtons[BIND_SLOTS]; /* the buttons of each action's binding (actions.h); 0: none */
    /* 0x24 */ u8 bindWhere[BIND_SLOTS / 2]; /* 4 bits an action, low half first: bit 0 controller 2, bits 1 to 3
                                              * the stick direction (bindings.h) */
    /* 0x2A */ HudPlace hud[HUD_COUNT];
    /* 0x4B */ u8 menuOpacity;  /* 1 to MENU_OPACITY_STEPS: how solid the menu's panels are (menu.c) */
    /* 0x4C */ u8 menuAcross;   /* 1 to 3: the menu at the left, in the centre or at the right */
    /* 0x4D */ u8 menuDown;     /* 1 to 3: the menu at the top, in the middle or low */
    /* 0x4E */ u8 trainerLog;   /* 1 to TRAINER_LOG_STEPS: how many results the trainer log shows (off, 5, 6, 8, 10; trainers.h) */
    /* 0x4F */ u8 trainerHold;  /* 1 to TRAINER_HOLD_STEPS: how long a result holds (off, 1, 2, 3 seconds) */
    /* 0x50 */ u8 unused;       /* the HUD slots that no item uses (HUD_SLOTS) were the room for these */
    /* 0x51 */ u8 debugPadL;    /* 1: with debugPad, L on controller 2 marks every level and garib as done */
    /* 0x52 */ u8 instantReset; /* 1: Reset loads the state saved on the level's first game frame (savestate.h) */
} Settings;

/* The settings as saved in EEPROM blocks 56 to 63, which the game never uses: 64 bytes. */
typedef struct SavedSettings {
    /* 0x00 */ u16 magic;
    /* 0x02 */ u8 version;
    /* 0x03 */ u8 flags; /* one bit for each of ballNormal to splitByCrystal, in that order, then debugPadL */
    /* 0x04 */ u16 hudOn; /* bit n: HUD item n is on; bit 15: instantReset (bits 13 to 15 have no HUD item) */
    /* 0x06 */ u8 hudPlace[HUD_SLOTS][2]; /* slots HUD_COUNT and the next hold menuOpacity, menuAcross, menuDown, and
                                         * in the last byte trainerLog (low nibble) and trainerHold (high nibble) */
    /* 0x20 */ u16 bindButtons[BIND_SLOTS];
    /* 0x38 */ u8 bindWhere[BIND_SLOTS / 2];
    /* 0x3E */ u16 checksum;
} SavedSettings;

#define SETTINGS_MAGIC 0x47505253 /* "GPRS" */
#define SAVED_MAGIC 0x4750        /* "GP" */
#define SETTINGS_VERSION 5
#define SETTINGS_EEPROM_BLOCK 56

extern Settings settings;

void SettingsDefault(void);
/* Gives the menu look and the trainer log's settings their default where a value is zero (a block saved before
 * them) or out of range. */
void SettingsFixMenuLook(void);
void SettingsLoadAtBoot(void);
/* Asks the controller thread to save or load the settings. The result shows in the menu. */
void SettingsRequestSave(void);
void SettingsRequestLoad(void);
/* Called by the controller thread after its controller read. */
void SettingsService(void);
/* The last result, for the menu: 0 nothing yet, 1 saved, 2 loaded, 3 no EEPROM, 4 nothing saved yet. */
extern u8 settingsStatus;

#endif
