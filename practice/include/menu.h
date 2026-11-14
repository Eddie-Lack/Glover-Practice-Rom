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
    ITEM_QUICK,   /* an action whose binding shows next to it; arg is both the action and what it is called with */
};

struct Menu;

/* A tool that takes over the menu's panel. */
typedef struct View {
    void (*open)(void);
    /* Returns 0 when the view closes (B on its first page). */
    int (*input)(u16 pressed, u16 held);
    void (*draw)(Gfx **dl);
} View;

/* 24 bytes: the pointer that each kind needs shares one word, and the text or function that only some kinds need
 * shares another. */
typedef struct MenuItem {
    const char *label;
    u8 kind;
    u8 min;
    u8 max;
    s8 arg;                  /* ACTION, QUICK, BIND: passed on; QUICK: the action whose binding shows */
    union {
        u8 *value;                  /* TOGGLE, CHOICE, NUMBER; INFO of a repeated row: the number of rows */
        const struct Menu *submenu; /* SUBMENU */
        const View *view;           /* VIEW */
    };
    void (*action)(int arg); /* ACTION, QUICK; CHOICE, NUMBER: called with arg after the value changed */
    union {
        const char *const *names;                   /* CHOICE: max + 1 names */
        void (*info)(char *buf, int size, int arg); /* INFO; ACTION: gives its value text */
    };
    const char *hint;        /* shown in place of the key help while the item is selected, or NULL */
} MenuItem;

_Static_assert(sizeof(MenuItem) == 24, "MenuItem is 24 bytes (tests/menunav.py knows the layout)");

/* A menu with `repeat` set has one item, which stands for count rows. Its value points to the live row count
 * (count is not used), the label of row N is "N", and its info function gets N as arg. */
typedef struct Menu {
    const char *title;
    const MenuItem *items;
    u8 count;
    u8 repeat;
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
