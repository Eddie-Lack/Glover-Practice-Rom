/* Force the Fear boss seed (fearseed.h). */
#include "fearseed.h"
#include "game.h"
#include "practice.h"

#define LEVEL_FEARBOSS 33
#define BALL_BURST 2

u8 fearSeedOn = 1;
static u32 seedStarts; /* practice.levelStarts of the boss start whose first burst was seen */

void FearSeedDraw(void) {
    if (fearSeedOn && gameInfo.level == LEVEL_FEARBOSS && gameInfo.mode == GAME_MODE_LEVEL &&
        ballControl.state == BALL_BURST && seedStarts != practice.levelStarts) {
        seedStarts = practice.levelStarts;
        rseed = FEARSEED_VALUE;
    }
}
