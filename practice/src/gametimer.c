/* The full-game timer and its splits. */
#include "fmt.h"
#include "game.h"
#include "gametimer.h"
#include "settings.h"
#include "text.h"

GameTimer gameTimer;

static u32 lastVi;
static u8 lastToStart;
static s8 lastLevel = -1;
static u8 lastCrystals;

u32 GameTimerHundredths(u32 frames) {
    return frames * 1001 / 600;
}

void GameTimerReset(void) {
    gameTimer.running = 0;
    gameTimer.done = 0;
    gameTimer.splitCount = 0;
    gameTimer.elapsed = 0;
}

void GameTimerStart(void) {
    GameTimerReset();
    gameTimer.running = 1;
    lastVi = __osViIntrCount;
    lastLevel = gameInfo.level;
    lastCrystals = saveSlot[currentSaveSlot].hubLevel;
}

void GameTimerExclude(u32 frames) {
    lastVi += frames;
}

static void Split(s8 level) {
    if (gameTimer.splitCount < SPLITS_MAX) {
        gameTimer.splits[gameTimer.splitCount] = gameTimer.elapsed;
        gameTimer.splitLevel[gameTimer.splitCount] = level;
        gameTimer.splitCount++;
    }
}

/* A level that ends a split: a new level is one the game plays, not the end of level screens or a cutscene. */
static int Playable(s8 level) {
    return level >= 0 && level < LEVEL_PRESENT;
}

void GameTimerTick(void) {
    u32 now = __osViIntrCount;
    u8 toStart = toStartGame != 0;
    SaveSlot *slot = &saveSlot[currentSaveSlot];

    /* The difficulty was chosen on the last game frame: the timer starts now. */
    if (toStart && !lastToStart) {
        GameTimerStart();
        now = lastVi;
    }
    lastToStart = toStart;
    if (!gameTimer.running) {
        return;
    }
    gameTimer.elapsed += now - lastVi;
    lastVi = now;
    if (settings.splitByCrystal) {
        if (slot->hubLevel != lastCrystals) {
            Split(gameInfo.level);
        }
    } else if (gameInfo.level != lastLevel && Playable(lastLevel)) {
        Split(lastLevel);
    }
    lastLevel = gameInfo.level;
    lastCrystals = slot->hubLevel;
    /* The last crystal: the exit into the cave ran on the last game frame, with six crystals back already. */
    if ((gameInfo.exitFlags & 1) && gameInfo.exitType == EXIT_CAVE && slot->hubLevel == 6 &&
        slot->hubTarget == 7) {
        Split(gameInfo.level);
        gameTimer.running = 0;
        gameTimer.done = 1;
    }
}

static void Time(char *buf, int size, const char *label, u32 frames) {
    u32 h = GameTimerHundredths(frames);

    if (h >= 360000) {
        Format(buf, size, "%s%d:%02d:%02d.%02d", label, h / 360000, h / 6000 % 60, h / 100 % 60, h % 100);
    } else {
        Format(buf, size, "%s%d:%02d.%02d", label, h / 6000, h / 100 % 60, h % 100);
    }
}

void GameTimerDraw(Gfx **dl, int x, int y) {
    char buf[40];
    int n = gameTimer.splitCount;

    Time(buf, sizeof(buf), gameTimer.done ? "done " : "game ", gameTimer.elapsed);
    TextPrint(dl, x, y, 1, gameTimer.running || gameTimer.done ? 0xFFFFFFFF : 0x707888FF, buf);
    if (n > 0) {
        u32 segment = gameTimer.splits[n - 1] - (n > 1 ? gameTimer.splits[n - 2] : 0);
        char label[8];
        Format(label, sizeof(label), "%d  ", n);
        Time(buf, sizeof(buf), label, segment);
        TextPrint(dl, x, y + 10, 1, 0xC0D0FFFF, buf);
    }
}
