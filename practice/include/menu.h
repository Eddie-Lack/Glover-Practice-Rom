#ifndef PRACTICE_MENU_H
#define PRACTICE_MENU_H

#include "rdp.h"
#include "types.h"

enum MenuKind {
    ITEM_SUBMENU, /* opens another menu */
    ITEM_TOGGLE,  /* a u8 that is 0 or 1 */
    ITEM_CHOICE,  /* a u8 that picks one of several names */
    ITEM_NUMBER,  /* a u8 between min and max */
    ITEM_ACTION,  /* calls a function with arg; info, if set, gives its value text */
    ITEM_INFO,    /* shows a line of text from a function */
    ITEM_BIND,    /* the binding of action arg: A waits for a new one, L clears it */
    ITEM_VIEW,    /* opens a tool with its own drawing and input */
};

struct Menu;

/* A tool that takes over the menu's panel. */
typedef struct View {
    void (*open)(void);
    /* Returns 0 when the view closes (B on its first page). */
    int (*input)(u16 pressed, u16 held);
    void (*draw)(Gfx **dl);
} View;

typedef struct MenuItem {
    const char *label;
    u8 kind;
    u8 min;
    u8 max;
    u8 *value;                  /* TOGGLE, CHOICE, NUMBER */
    const struct Menu *submenu; /* SUBMENU */
    void (*action)(int arg);    /* ACTION; CHOICE, NUMBER: called with arg after the value changed */
    const char *const *names;   /* CHOICE: max + 1 names */
    void (*info)(char *buf, int size, int arg);
    s8 arg;                     /* ACTION, BIND: passed on; ACTION: the action whose binding shows, if >= 0 */
    s8 bind;
    const View *view;           /* VIEW */
    const char *hint;           /* shown in place of the key help while the item is selected, or NULL */
} MenuItem;

typedef struct Menu {
    const char *title;
    const MenuItem *items;
    u8 count;
} Menu;

extern u8 menuOpen;
/* The view that has the menu's panel, or NULL. */
extern const View *menuView;

void MenuOpen(void);
/* Gives the menu's panel to V, as an ITEM_VIEW does, without calling its open function. */
void MenuShowView(const View *v);
void MenuClose(void);
/* Moves through the menu with controller 1. Called once a game frame while the menu is open. */
void MenuInput(u16 pressed, u16 held);
void MenuDraw(Gfx **dl);
/* The colour of the panel (BAR 0) or of its title bar (BAR 1), by the Menu opacity setting. */
u32 MenuPanelColor(int bar);
/* Fills in the parts of the menus that come from the game's tables. */
void MenusInit(void);

#endif
