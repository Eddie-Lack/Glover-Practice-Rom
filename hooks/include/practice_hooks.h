#ifndef PRACTICE_HOOKS_H
#define PRACTICE_HOOKS_H

/* A hook replaces a call that the game makes with a call to a practice function of the same type. The practice
 * function calls the game's function and does its own work before or after. The call stays one instruction, so
 * no code or data in the game moves.
 *
 * Without PRACTICE every hook is the original call, and the hooked source still builds the retail ROM. */
#ifdef PRACTICE
#define PRACTICE_WRAP(fn) Practice##fn
#else
#define PRACTICE_WRAP(fn) fn
#endif

/* Include after the game's own headers: the declarations use its types. */
void PracticeMeasureFrameTicks(void);
void PracticeSendLastFrame(void);
void PracticePrintTimers(void);
void PracticeClearText(void);
void PracticeDoMenu(void);
void PracticeReadController(u8 cntlr);
void PracticeReadControllerFromData(u8 cntlr);
void PracticeProcessInput(void);
void PracticeSeedRandom(void);
s32 PracticeCheckForLevelCompleted(void);
void PracticeInitOverlayList(void);
void PracticeInitScreenDebris(void);
void PracticePrintStats(void);
void PracticePrepareSong(u8 song);
void PracticeMoveCam(void);
void PracticeprintHiscores(char *name, s32 start);
void PracticeguLookAtHilite(Mtx *m, LookAt *l, Hilite *h, float xEye, float yEye, float zEye, float xAt, float yAt,
                            float zAt, float xUp, float yUp, float zUp, float xl1, float yl1, float zl1, float xl2,
                            float yl2, float zl2, int twidth, int theight);

#endif
