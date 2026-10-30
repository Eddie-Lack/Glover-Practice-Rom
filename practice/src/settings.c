#include "game.h"
#include "bindings.h"
#include "settings.h"

Settings settings;
u8 settingsStatus;

static u8 pending; /* 1 save, 2 load */

_Static_assert(sizeof(SavedSettings) == 64, "the saved settings must fill the 8 free EEPROM blocks");
_Static_assert(HUD_COUNT + 2 <= HUD_SLOTS, "the menu look fills the two free HUD slots of the saved settings");
_Static_assert(HUD_COUNT <= HUD_SLOTS, "every HUD item needs a place in the saved settings");
_Static_assert(__builtin_offsetof(Settings, bindButtons) == 0x0C && __builtin_offsetof(Settings, hud) == 0x2A &&
                   __builtin_offsetof(Settings, menuOpacity) == 0x4B && __builtin_offsetof(Settings, trainerLog) == 0x4E && __builtin_offsetof(Settings, debugPadL) == 0x51 && __builtin_offsetof(Settings, instantReset) == 0x52,
               "tests/layout.py has the offsets");

/* The saved settings have no free flag bit and no free byte. hudOn has 16 bits for 13 HUD slots, so its top bit holds
 * instantReset. Settings saved before it was added have the bit clear, which is the default, so the version stays. */
#define SAVED_INSTANT_BIT 15
_Static_assert(HUD_SLOTS <= SAVED_INSTANT_BIT, "the instant reset's bit must be free");

/* Places in units of 2 pixels, clear of the game's own HUD: its icons at the top left, the health at the bottom
 * left and the lives at the bottom right. */
static const HudPlace defaultHud[HUD_COUNT] = {
    [HUD_TIMER] = { 1, 62, 100 },
    [HUD_INPUT] = { 0, 12, 110 },
    [HUD_GLOVE] = { 0, 12, 22 },
    [HUD_BALL] = { 0, 12, 27 },
    [HUD_ACTION] = { 0, 12, 32 },
    [HUD_FRAMES] = { 0, 122, 4 },
    [HUD_LAG] = { 0, 122, 9 },
    [HUD_RNG] = { 0, 122, 14 },
    [HUD_LAST] = { 0, 62, 94 },
    [HUD_GAME] = { 0, 122, 20 },
    [HUD_WATCH] = { 0, 12, 40 },
};

/* The settings that are saved as one bit each, in this order. */
static u8 *const flagSettings[] = {
    &settings.ballNormal, &settings.menuPauses, &settings.hudShadow,
    &settings.timerFrames, &settings.inputHistory, &settings.debugPad, &settings.splitByCrystal,
    &settings.debugPadL,
};
_Static_assert(ARRAY_COUNT(flagSettings) <= 8, "the flags byte holds 8 settings");

void SettingsDefault(void) {
    u8 *p = (u8 *)&settings;

    for (u32 i = 0; i < sizeof(settings); i++) {
        p[i] = 0;
    }
    settings.magic = SETTINGS_MAGIC;
    settings.version = SETTINGS_VERSION;
    settings.menuPauses = 1;
    settings.hudShadow = 1;
    settings.timerFrames = 1;
    for (int i = 0; i < HUD_COUNT; i++) {
        settings.hud[i] = defaultHud[i];
    }
    SettingsFixMenuLook();
    BindingsDefault();
}

/* The menu look is stored from 1: blocks and notes saved before it hold zero there and get the defaults. */
void SettingsFixMenuLook(void) {
    if (settings.menuOpacity - 1u >= MENU_OPACITY_STEPS) {
        settings.menuOpacity = MENU_OPACITY_DEFAULT;
    }
    if (settings.menuAcross - 1u >= 3) {
        settings.menuAcross = 2;
    }
    if (settings.menuDown - 1u >= 3) {
        settings.menuDown = 2;
    }
    if (settings.trainerLog - 1u >= TRAINER_LOG_STEPS) {
        settings.trainerLog = TRAINER_LOG_DEFAULT;
    }
    if (settings.trainerHold - 1u >= TRAINER_HOLD_STEPS) {
        settings.trainerHold = TRAINER_HOLD_DEFAULT;
    }
}

static u16 Checksum(const SavedSettings *s) {
    const u8 *p = (const u8 *)s;
    u16 sum = 0x1234;

    for (u32 i = 0; i < sizeof(SavedSettings) - sizeof(s->checksum); i++) {
        sum = (u16)((sum << 1 | sum >> 15) ^ p[i]);
    }
    return sum;
}

static void Pack(SavedSettings *s) {
    s->magic = SAVED_MAGIC;
    s->version = SETTINGS_VERSION;
    s->flags = 0;
    for (u32 i = 0; i < ARRAY_COUNT(flagSettings); i++) {
        s->flags |= (*flagSettings[i] & 1) << i;
    }
    s->hudOn = 0;
    for (int i = 0; i < HUD_COUNT; i++) {
        s->hudOn |= (settings.hud[i].on & 1) << i;
        s->hudPlace[i][0] = settings.hud[i].x;
        s->hudPlace[i][1] = settings.hud[i].y;
    }
    s->hudPlace[HUD_COUNT][0] = settings.menuOpacity;
    s->hudPlace[HUD_COUNT][1] = settings.menuAcross;
    s->hudPlace[HUD_COUNT + 1][0] = settings.menuDown;
    s->hudPlace[HUD_COUNT + 1][1] = (u8)((settings.trainerLog & 15) | (settings.trainerHold << 4));
    s->hudOn |= (settings.instantReset & 1) << SAVED_INSTANT_BIT;
    for (int i = 0; i < BIND_SLOTS; i++) {
        s->bindButtons[i] = settings.bindButtons[i];
    }
    for (int i = 0; i < BIND_SLOTS / 2; i++) {
        s->bindWhere[i] = settings.bindWhere[i];
    }
    s->checksum = Checksum(s);
}

/* Takes saved settings if they are valid. Returns 1 if it did. */
static int Unpack(const SavedSettings *s) {
    if (s->magic != SAVED_MAGIC || s->version != SETTINGS_VERSION || s->checksum != Checksum(s)) {
        return 0;
    }
    for (u32 i = 0; i < ARRAY_COUNT(flagSettings); i++) {
        *flagSettings[i] = (s->flags >> i) & 1;
    }
    for (int i = 0; i < HUD_COUNT; i++) {
        settings.hud[i].on = (s->hudOn >> i) & 1;
        settings.hud[i].x = s->hudPlace[i][0];
        settings.hud[i].y = s->hudPlace[i][1];
    }
    settings.menuOpacity = s->hudPlace[HUD_COUNT][0];
    settings.menuAcross = s->hudPlace[HUD_COUNT][1];
    settings.menuDown = s->hudPlace[HUD_COUNT + 1][0];
    settings.trainerLog = s->hudPlace[HUD_COUNT + 1][1] & 15;
    settings.trainerHold = s->hudPlace[HUD_COUNT + 1][1] >> 4;
    SettingsFixMenuLook();
    settings.instantReset = (s->hudOn >> SAVED_INSTANT_BIT) & 1;
    for (int i = 0; i < BIND_SLOTS; i++) {
        settings.bindButtons[i] = s->bindButtons[i];
    }
    for (int i = 0; i < BIND_SLOTS / 2; i++) {
        settings.bindWhere[i] = s->bindWhere[i];
    }
    return 1;
}

/* Reads the saved settings at boot, from PracticeInit. The game has not set up the serial interface yet, so this
 * uses its own message queue for the serial interrupt; the game's osContInit replaces it later. A read here,
 * before MeasureFrameTicks waits for its video frames, does not change the game's timing. A read during the
 * game did (docs/testing.md). */
void SettingsLoadAtBoot(void) {
    static u8 queue[OS_MESG_QUEUE_SIZE] __attribute__((aligned(8)));
    static void *message;
    static SavedSettings buf __attribute__((aligned(8)));
    OSMesgQueue *mq = (OSMesgQueue *)queue;

    osCreateMesgQueue(mq, &message, 1);
    osSetEventMesg(OS_EVENT_SI, mq, NULL);
    if (!osEepromProbe(mq)) {
        settingsStatus = 3;
        return;
    }
    osEepromLongRead(mq, SETTINGS_EEPROM_BLOCK, (u8 *)&buf, sizeof(buf));
    if (Unpack(&buf)) {
        settingsStatus = 2;
    }
}

void SettingsRequestSave(void) {
    pending = 1;
}

void SettingsRequestLoad(void) {
    pending = 2;
}

/* The controller thread calls this after its read of the controllers, as the game does its own EEPROM work. It
 * waits while the game has an EEPROM request of its own. */
void SettingsService(void) {
    static SavedSettings buf __attribute__((aligned(8)));

    if (!pending || eepromMessageQueue[0] != 0) {
        return;
    }
    if (!eepromPresent) {
        settingsStatus = 3;
    } else if (pending == 1) {
        Pack(&buf);
        osEepromLongWrite(&controllerMsgQ, SETTINGS_EEPROM_BLOCK, (u8 *)&buf, sizeof(buf));
        settingsStatus = 1;
    } else {
        osEepromLongRead(&controllerMsgQ, SETTINGS_EEPROM_BLOCK, (u8 *)&buf, sizeof(buf));
        if (Unpack(&buf)) {
            settingsStatus = 2;
        } else {
            settingsStatus = 4;
        }
    }
    pending = 0;
}
