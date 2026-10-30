#ifndef PRACTICE_BINDINGS_H
#define PRACTICE_BINDINGS_H

#include "types.h"

/* A binding is a set of buttons on controller 1 or 2, and on controller 1 maybe a stick direction held fully.
 * It fires when all its buttons are held and one of them is pressed. Its buttons are kept from the game. */
#define BIND_PORT2 1
#define BIND_STICK_SHIFT 1
enum StickDir { STICK_NONE, STICK_UP, STICK_DOWN, STICK_LEFT, STICK_RIGHT };

void BindingsDefault(void);
/* The controller thread, once a game frame, while the menu is closed. */
void BindingsCheck(void);
/* Text for an action's binding, for the menu. */
void BindingText(int action, char *buf, int size);
/* Waits for the next combination and binds it to ACTION. */
void BindingCapture(int action);
/* While capturing: 1. The controller thread calls BindingsCapturing every frame. */
int BindingsCapturing(void);
void BindingClear(int action);

#endif
