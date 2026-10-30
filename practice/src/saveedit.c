/* The save file editor. A view of the Settings menu. It edits a copy of the game's save (the six files: lives,
 * crystals, difficulty, the levels, and the best times) that lives in the practice segment. The game's own copy
 * in RAM does not change until "Write to the cartridge". Write merges the edits into the game's copy and posts the
 * game's own EEPROM requests (docs/game.md, "The EEPROM"). "Reload from the cartridge" reads the cartridge's data
 * into buffers of its own and checks the CRCs as the game does at boot. The game's copy takes the data only when
 * every CRC is right.
 *
 * Up and down choose a row. Left and right change a value (Z: steps of 10, R: steps of 60 for times). A on the
 * file page opens the levels or asks for the write or the reload; on the levels page it chooses the next column.
 * B goes back. */
#include "fmt.h"
#include "game.h"
#include "hud.h"
#include "menu.h"
#include "saveedit.h"
#include "text.h"

#define VIEW_X 16
#define VIEW_Y 28
#define VIEW_W 288
#define ROW_H 10
#define ROWS 12
#define SAVE_FILES 6
#define LEVEL_BITS 30 /* five levels in each of six worlds (levelInfo[].bit) */
#define MAX_TIME 5999 /* seconds */
#define MIN_LIVES 3   /* ContinueGame gives a file with fewer lives 3 */
#define MAX_LIVES 99  /* the HUD shows two digits */
#define BEST_TIMES_CRC_LEN 0xC0 /* the game's CRC of the best times covers 12 bytes of padding after the table */
#define EEPROM_TIMES_BLOCK 0x07
#define EEPROM_SLOTS_BLOCK 0x1F
#define EEPROM_CRC_BLOCK 0x34
enum { CONFIRM_NONE, CONFIRM_WRITE, CONFIRM_RELOAD };
enum { REQUEST_NONE, REQUEST_WRITE, REQUEST_RELOAD };
enum { STATUS_NONE, STATUS_WRITTEN, STATUS_RELOADED, STATUS_NO_EEPROM, STATUS_BAD_CRC };

enum { ROW_SLOT, ROW_LIVES, ROW_WON, ROW_RETURNED, ROW_DIFFICULTY, ROW_LEVELS, ROW_WRITE, ROW_RELOAD, ROW_COUNT };
enum { COL_DONE, COL_GARIBS, COL_TIME, COL_COUNT };

SaveEdit saveEdit;
/* The edited copy. The unedited copy (as of the last refresh) tells which fields the edits changed. */
SaveSlot saveEditSlots[SAVE_FILES];
BestTime saveEditBests[LEVEL_BITS];
static SaveSlot originalSlots[SAVE_FILES];
static BestTime originalBests[LEVEL_BITS];
/* What a reload reads from the cartridge. The game's copy is not touched until the CRCs are right. */
static SaveSlot loadedSlots[SAVE_FILES] __attribute__((aligned(8)));
static u8 loadedBests[BEST_TIMES_CRC_LEN] __attribute__((aligned(8)));
static u32 loadedCRCs[8] __attribute__((aligned(8)));

static u8 holdFrames;
static u16 lastHeld;

static const char *const difficultyNames[] = { "Easy", "Medium", "Hard" };

static SaveSlot *Slot(void) {
    return &saveEditSlots[saveEdit.slot];
}

static void Copy(void *to, const void *from, int size) {
    u8 *d = to;
    const u8 *f = from;

    while (size-- > 0) {
        *d++ = *f++;
    }
}

static int Differ(const void *a, const void *b, int size) {
    const u8 *x = a;
    const u8 *y = b;

    while (size-- > 0) {
        if (*x++ != *y++) {
            return 1;
        }
    }
    return 0;
}

/* The edited copy and the unedited copy become the game's copy. Interrupts must be off. */
static void SnapshotLocked(void) {
    Copy(originalSlots, saveSlot, sizeof(originalSlots));
    Copy(saveEditSlots, saveSlot, sizeof(saveEditSlots));
    Copy(originalBests, bestTimes, sizeof(originalBests));
    Copy(saveEditBests, bestTimes, sizeof(saveEditBests));
    saveEdit.bestsEdited = 0;
}

static int Wrap(int v, int lo, int hi) {
    return v > hi ? lo : v < lo ? hi : v;
}

static int Clamp(int v, int lo, int hi) {
    return v > hi ? hi : v < lo ? lo : v;
}

/* Bits of a word: those that the edits changed (edited differs from original) take the edited value; the others
 * stay as the game has them now. */
static u32 MergeBits(u32 game, u32 edited, u32 original) {
    u32 d = edited ^ original;

    return (game & ~d) | (edited & d);
}

#define MERGE_FIELD(g, e, o) ((e) != (o) ? (e) : (g))

/* The fields that the editor shows, taken from the game's copy G unless the edits changed them. Every other field
 * of the file (name, score, volumes) is the game's. */
static void MergeSlot(SaveSlot *out, const SaveSlot *g, const SaveSlot *edited, const SaveSlot *original) {
    *out = *g;
    out->levels = MergeBits(g->levels, edited->levels, original->levels);
    out->allGaribs = MergeBits(g->allGaribs, edited->allGaribs, original->allGaribs);
    out->lives = MERGE_FIELD(g->lives, edited->lives, original->lives);
    out->difficultyLevel = MERGE_FIELD(g->difficultyLevel, edited->difficultyLevel, original->difficultyLevel);
    out->hubTarget = MERGE_FIELD(g->hubTarget, edited->hubTarget, original->hubTarget);
    out->hubLevel = MERGE_FIELD(g->hubLevel, edited->hubLevel, original->hubLevel);
    /* The game may have changed either count since the snapshot. The fountain holds at most the crystals won. */
    out->hubTarget = (u8)Clamp(out->hubTarget, 0, 7);
    out->hubLevel = (u8)Clamp(out->hubLevel, 0, out->hubTarget);
}

static const BestTime *MergeBest(const BestTime *g, const BestTime *edited, const BestTime *original) {
    return Differ(edited, original, sizeof(BestTime)) ? edited : g;
}

/* The fields that the edits did not change follow the game's copy; the edited ones stay in the editor's copy.
 * The unedited copy becomes the game's. Interrupts must be off. */
static void RefreshLocked(void) {
    SaveSlot merged;
    BestTime best;

    for (int i = 0; i < SAVE_FILES; i++) {
        MergeSlot(&merged, &saveSlot[i], &saveEditSlots[i], &originalSlots[i]);
        if (i == currentSaveSlot && difficultySwapped) {
            /* The game holds Easy in RAM for a moment: the real value is the one that was seen before. */
            merged.difficultyLevel = originalSlots[i].difficultyLevel;
            originalSlots[i] = saveSlot[i];
            originalSlots[i].difficultyLevel = merged.difficultyLevel;
        } else {
            originalSlots[i] = saveSlot[i];
        }
        saveEditSlots[i] = merged;
    }
    saveEdit.bestsEdited = 0;
    for (int i = 0; i < LEVEL_BITS; i++) {
        best = *MergeBest(&bestTimes[i], &saveEditBests[i], &originalBests[i]);
        originalBests[i] = bestTimes[i];
        saveEditBests[i] = best;
        saveEdit.bestsEdited |= Differ(&best, &originalBests[i], sizeof(BestTime));
    }
}

/* The level that the game's bit n belongs to, for its name. Space Boss 1 and 2 share bit 28: the first. */
static const char *LevelName(int bit) {
    for (int i = 0; i < NUM_LEVELS; i++) {
        if (levelInfo[i].bit == bit) {
            return levelInfo[i].name;
        }
    }
    return "?";
}

/* A blank file's name is not text (its first byte is 13). */
static int NamePrintable(const SaveSlot *s) {
    return s->name[0] >= 0x20 && s->name[0] < 0x7F;
}

static void SlotName(char *buf, const SaveSlot *s) {
    if (!NamePrintable(s)) {
        Format(buf, 8, "empty");
        return;
    }
    for (int i = 0; i < 3; i++) {
        buf[i] = s->name[i] >= 0x20 && s->name[i] < 0x7F ? (char)s->name[i] : '-';
    }
    buf[3] = '\0';
}

static void Open(void) {
    /* Edits that are not written stay while the editor is closed. Every other field follows the game's copy. */
    u32 mask = __osDisableInt();

    RefreshLocked();
    __osRestoreInt(mask);
    saveEdit.page = 0;
    saveEdit.cursor = 0;
    saveEdit.column = 0;
    saveEdit.confirm = CONFIRM_NONE;
    holdFrames = 0;
}

static void SetTime(int bit, int seconds) {
    BestTime *t = &saveEditBests[bit];

    t->time = (s16)Clamp(seconds, 0, MAX_TIME);
    if (t->name[0] == 0) {
        /* The Hi Scores screen prints a name. A time with none gets the file's name. */
        for (int i = 0; i < 3; i++) {
            t->name[i] = NamePrintable(Slot()) ? (char)Slot()->name[i] : 'A';
        }
    }
    saveEdit.bestsEdited = 1;
}

static void ChangeFile(int step) {
    SaveSlot *s = Slot();

    switch (saveEdit.cursor) {
        case ROW_SLOT:
            saveEdit.slot = (u8)Wrap(saveEdit.slot + step, 0, SAVE_FILES - 1);
            break;
        case ROW_LIVES:
            s->lives = (u8)Clamp(s->lives + step, MIN_LIVES, MAX_LIVES);
            break;
        case ROW_WON:
            s->hubTarget = (u8)Clamp(s->hubTarget + step, 0, 7);
            /* The fountain cannot hold more crystals than the file has won. */
            s->hubLevel = (u8)Clamp(s->hubLevel, 0, s->hubTarget);
            break;
        case ROW_RETURNED:
            s->hubLevel = (u8)Clamp(s->hubLevel + step, 0, s->hubTarget);
            break;
        case ROW_DIFFICULTY:
            /* The game reads the difficulty of the file in play all through the run. */
            if (saveEdit.slot == currentSaveSlot) {
                HudMessage("the file in play keeps its difficulty");
            } else {
                s->difficultyLevel = (u8)Wrap(s->difficultyLevel + step, 0, 2);
            }
            break;
    }
}

static void ChangeLevel(int step, u16 held) {
    int bit = saveEdit.cursor;
    SaveSlot *s = Slot();

    switch (saveEdit.column) {
        case COL_DONE:
            s->levels ^= 1 << bit;
            break;
        case COL_GARIBS:
            s->allGaribs ^= 1 << bit;
            break;
        case COL_TIME:
            SetTime(bit, saveEditBests[bit].time + step * ((held & CONT_R) ? 60 : (held & CONT_Z) ? 10 : 1));
            break;
    }
}

/* A request waits for the controller thread. */
static int Pending(void) {
    return saveEdit.request != REQUEST_NONE;
}

static int Input(u16 pressed, u16 held) {
    int rows = saveEdit.page ? LEVEL_BITS : ROW_COUNT;
    int step = 0;

    /* A held direction repeats. */
    if (held & (CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT)) {
        holdFrames = (held == lastHeld && holdFrames < 255) ? holdFrames + 1 : 0;
        if (holdFrames > 6) {
            pressed |= held & (CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT);
        }
    } else {
        holdFrames = 0;
    }
    lastHeld = held;
    if (saveEdit.confirm) {
        if (pressed & CONT_A) {
            int reload = saveEdit.confirm == CONFIRM_RELOAD;

            saveEdit.confirm = CONFIRM_NONE;
            if (Pending()) {
                HudMessage("the last request is still running");
            } else {
                saveEdit.request = reload ? REQUEST_RELOAD : REQUEST_WRITE;
                HudMessage(reload ? "reloading the save file" : "writing the save file");
            }
        } else if (pressed) {
            saveEdit.confirm = CONFIRM_NONE;
        }
        return 1;
    }
    if (pressed & CONT_B) {
        if (saveEdit.page) {
            saveEdit.page = 0;
            saveEdit.cursor = ROW_LEVELS;
            return 1;
        }
        return 0;
    }
    if (pressed & CONT_UP) {
        saveEdit.cursor = (u8)((saveEdit.cursor + rows - 1) % rows);
    }
    if (pressed & CONT_DOWN) {
        saveEdit.cursor = (u8)((saveEdit.cursor + 1) % rows);
    }
    if (pressed & CONT_LEFT) {
        step = -1;
    }
    if (pressed & CONT_RIGHT) {
        step = 1;
    }
    if (saveEdit.page == 1) {
        if (step != 0) {
            ChangeLevel(step, held);
        }
        if (pressed & CONT_A) {
            saveEdit.column = (u8)((saveEdit.column + 1) % COL_COUNT);
        }
        return 1;
    }
    if (step != 0) {
        ChangeFile(step * ((held & CONT_Z) ? 10 : 1));
    }
    if (pressed & CONT_A) {
        switch (saveEdit.cursor) {
            case ROW_LEVELS:
                saveEdit.page = 1;
                saveEdit.cursor = 0;
                break;
            case ROW_WRITE:
            case ROW_RELOAD:
                if (Pending()) {
                    HudMessage("the last request is still running");
                } else {
                    saveEdit.confirm = saveEdit.cursor == ROW_WRITE ? CONFIRM_WRITE : CONFIRM_RELOAD;
                }
                break;
        }
    }
    return 1;
}

static void Time(char *buf, int size, int seconds) {
    Format(buf, size, "%d:%02d", seconds / 60, seconds % 60);
}

static void Frame(Gfx **dl, int rows) {
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 28 + (rows + 3) * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
}

static const char *const rowNames[ROW_COUNT] = {
    "File", "Lives", "Crystals won", "Crystals returned", "Difficulty", "Levels", "Write to the cartridge",
    "Reload from the cartridge",
};

static void DrawFile(Gfx **dl) {
    char buf[48];
    char name[8];
    const SaveSlot *s = Slot();
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;

    Frame(dl, ROW_COUNT + 1);
    TextPrint(dl, x, VIEW_Y + 3, 1, 0xFFE070FF, "Save file");
    TextPrint(dl, VIEW_X + VIEW_W - 6 - TextWidth("Edits your real save!", 1), VIEW_Y + 3, 1, 0xFF9060FF,
              "Edits your real save!");
    SlotName(name, s);
    Format(buf, sizeof(buf), "%s  score %d%s", name, s->score, saveEdit.slot == currentSaveSlot ? "  (in play)" : "");
    TextPrint(dl, x, y, 1, 0xB0B8C8FF, buf);
    for (int r = 0; r < ROW_COUNT; r++) {
        int row = y + (r + 1) * ROW_H + 2;
        int selected = r == saveEdit.cursor;

        buf[0] = '\0';
        switch (r) {
            case ROW_SLOT:
                Format(buf, sizeof(buf), "%d", saveEdit.slot + 1);
                break;
            case ROW_LIVES:
                Format(buf, sizeof(buf), "%d", s->lives);
                break;
            case ROW_WON:
                Format(buf, sizeof(buf), "%d", s->hubTarget);
                break;
            case ROW_RETURNED:
                Format(buf, sizeof(buf), "%d", s->hubLevel);
                break;
            case ROW_DIFFICULTY:
                Format(buf, sizeof(buf), "%s", difficultyNames[s->difficultyLevel % 3]);
                break;
            case ROW_LEVELS:
                Format(buf, sizeof(buf), ">");
                break;
        }
        if (selected) {
            TextRect(dl, VIEW_X + 2, row - 1, VIEW_X + VIEW_W - 2, row + ROW_H - 2, 0x5070D060);
        }
        TextPrint(dl, x, row, 1, selected ? 0xFFFFFFFF : 0xB0B8C8FF, rowNames[r]);
        TextPrint(dl, VIEW_X + VIEW_W - 8 - TextWidth(buf, 1), row, 1, selected ? 0xFFE070FF : 0xC0A860FF, buf);
    }
    y += (ROW_COUNT + 2) * ROW_H;
    if (saveEdit.confirm == CONFIRM_WRITE) {
        TextPrint(dl, x, y, 1, 0xFF9060FF, "Press A to write over your save. Other: no");
    } else if (saveEdit.confirm == CONFIRM_RELOAD) {
        TextPrint(dl, x, y, 1, 0xFF9060FF, "Press A to drop your edits and reload. Other: no");
    } else if (Pending()) {
        TextPrint(dl, x, y, 1, 0x80D080FF, "Waiting for the game's own save...");
    } else if (saveEdit.status != STATUS_NONE) {
        static const char *const text[] = { "", "Written. The game checked the CRCs.", "Reloaded from the cartridge.",
                                            "No EEPROM.", "The cartridge's data failed its check. Nothing reloaded." };
        TextPrint(dl, x, y, 1, saveEdit.status == STATUS_BAD_CRC ? 0xFF9060FF : 0x80D080FF, text[saveEdit.status % 5]);
    } else {
        TextPrint(dl, x, y, 1, 0x8090A8FF, "</> change  A choose  B back");
    }
    TextPrint(dl, x, y + ROW_H, 1, 0x8090A8FF, saveEdit.bestsEdited ? "Best times edited (all files)" : "");
}

static void DrawLevels(Gfx **dl) {
    char buf[48];
    const SaveSlot *s = Slot();
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;
    int first = saveEdit.cursor < ROWS / 2 ? 0 : saveEdit.cursor - ROWS / 2;
    static const int colX[COL_COUNT] = { 140, 186, 232 };

    if (first > LEVEL_BITS - ROWS) {
        first = LEVEL_BITS - ROWS;
    }
    Frame(dl, ROWS + 1);
    Format(buf, sizeof(buf), "Levels of file %d", saveEdit.slot + 1);
    TextPrint(dl, x, VIEW_Y + 3, 1, 0xFFE070FF, buf);
    TextPrint(dl, x, y, 1, 0x8090A8FF, "Level");
    TextPrint(dl, VIEW_X + colX[COL_DONE], y, 1, 0x8090A8FF, "Done");
    TextPrint(dl, VIEW_X + colX[COL_GARIBS], y, 1, 0x8090A8FF, "Garibs");
    TextPrint(dl, VIEW_X + colX[COL_TIME], y, 1, 0x8090A8FF, "Best time");
    for (int r = 0; r < ROWS; r++) {
        int bit = first + r;
        int row = y + (r + 1) * ROW_H + 2;
        int selected = bit == saveEdit.cursor;
        u32 white = selected ? 0xFFFFFFFF : 0xB0B8C8FF;

        if (selected) {
            TextRect(dl, VIEW_X + 2, row - 1, VIEW_X + VIEW_W - 2, row + ROW_H - 2, 0x5070D060);
        }
        Format(buf, sizeof(buf), "%.18s", LevelName(bit));
        TextPrint(dl, x, row, 1, white, buf);
        for (int c = 0; c < COL_COUNT; c++) {
            u32 color = selected && c == saveEdit.column ? 0xFFE070FF : white;

            if (c == COL_DONE) {
                Format(buf, sizeof(buf), "%s", (s->levels >> bit) & 1 ? "yes" : "no");
            } else if (c == COL_GARIBS) {
                Format(buf, sizeof(buf), "%s", (s->allGaribs >> bit) & 1 ? "yes" : "no");
            } else {
                Time(buf, sizeof(buf), saveEditBests[bit].time);
            }
            TextPrint(dl, VIEW_X + colX[c], row, 1, color, buf);
        }
    }
    y += (ROWS + 2) * ROW_H;
    TextPrint(dl, x, y, 1, 0x8090A8FF, "A column  </> change  Z 10 s  R 60 s");
    TextPrint(dl, x, y + ROW_H, 1, 0x8090A8FF, "Best times are shared by all files. B back");
}

static void Draw(Gfx **dl) {
    if (saveEdit.page) {
        DrawLevels(dl);
    } else {
        DrawFile(dl);
    }
}

const View saveEditView = { Open, Input, Draw };

static int EepromBusy(void) {
    return eepromMessageQueue[0] != 0 || eepromMessageNum != 0;
}

/* Reads the cartridge's files, best times and CRCs into the buffers and checks them as the game does at boot
 * (CheckCRC). Returns 1 when everything was read and every CRC is right. */
static int LoadCartridge(void) {
    if (osEepromLongRead(&controllerMsgQ, EEPROM_SLOTS_BLOCK, (u8 *)loadedSlots, sizeof(loadedSlots)) != 0 ||
        osEepromLongRead(&controllerMsgQ, EEPROM_TIMES_BLOCK, loadedBests, sizeof(loadedBests)) != 0 ||
        osEepromLongRead(&controllerMsgQ, EEPROM_CRC_BLOCK, (u8 *)loadedCRCs, sizeof(loadedCRCs)) != 0) {
        return 0;
    }
    for (int i = 0; i < SAVE_FILES; i++) {
        if (UpdateCRCData((u8 *)&loadedSlots[i], sizeof(SaveSlot)) != loadedCRCs[i]) {
            return 0;
        }
    }
    return UpdateCRCData(loadedBests, sizeof(loadedBests)) == loadedCRCs[7];
}

/* The cartridge's data becomes the game's copy and the editor's. */
static void ReloadSave(void) {
    int ok = LoadCartridge();
    u32 mask = __osDisableInt();

    if (EepromBusy() || difficultySwapped) {
        /* The game posted a request while the blocks were read: it may change its copy. The reload starts over.
         * It also waits while the glove's input holds the difficulty at Easy. */
        __osRestoreInt(mask);
        return;
    }
    if (ok) {
        Copy(saveSlot, loadedSlots, sizeof(loadedSlots));
        Copy(bestTimes, loadedBests, sizeof(bestTimes));
        for (int i = 0; i < SAVE_FILES; i++) {
            eepromCRCS[i] = loadedCRCs[i];
        }
        eepromCRCS[7] = loadedCRCs[7];
        SnapshotLocked();
    }
    __osRestoreInt(mask);
    saveEdit.status = ok ? STATUS_RELOADED : STATUS_BAD_CRC;
    saveEdit.request = REQUEST_NONE;
}

/* Merges the edits into the game's copy and posts the game's requests for the files (6), the best times (4), the
 * hiscore table (1) and the CRCs (11, which computes them from the game's copy, the hiscore table too). The
 * difficulty of the file in play stays: the game reads it all through the run. Interrupts must be off. */
static void WriteEdits(void) {
    int play = currentSaveSlot;
    SaveSlot merged;
    u8 difficulty = 0;

    if (play >= 0 && play < SAVE_FILES) {
        difficulty = saveSlot[play].difficultyLevel;
    }
    for (int i = 0; i < SAVE_FILES; i++) {
        MergeSlot(&merged, &saveSlot[i], &saveEditSlots[i], &originalSlots[i]);
        saveSlot[i] = merged;
    }
    for (int i = 0; i < LEVEL_BITS; i++) {
        bestTimes[i] = *MergeBest(&bestTimes[i], &saveEditBests[i], &originalBests[i]);
    }
    if (play >= 0 && play < SAVE_FILES) {
        saveSlot[play].difficultyLevel = difficulty;
    }
    PostEepromMessage(6);
    PostEepromMessage(4);
    PostEepromMessage(1);
    PostEepromMessage(11);
    SnapshotLocked();
}

/* The controller thread, after its read of the controllers. A request waits until the game has no EEPROM request.
 * A write checks that and posts with interrupts off, as the game thread posts requests too. A reload reads the
 * blocks itself, as SettingsService does, and takes the data over with interrupts off. */
void SaveEditService(void) {
    u32 mask;

    if (saveEdit.request == REQUEST_NONE) {
        return;
    }
    if (!eepromPresent) {
        saveEdit.status = STATUS_NO_EEPROM;
        saveEdit.request = REQUEST_NONE;
        return;
    }
    if (EepromBusy()) {
        return;
    }
    if (saveEdit.request == REQUEST_RELOAD) {
        ReloadSave();
        return;
    }
    mask = __osDisableInt();
    if (EepromBusy() || difficultySwapped) {
        __osRestoreInt(mask);
        return;
    }
    WriteEdits();
    __osRestoreInt(mask);
    saveEdit.status = STATUS_WRITTEN;
    saveEdit.request = REQUEST_NONE;
}
