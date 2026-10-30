/* The practice menu: a list of items with sub-menus. D-pad up and down move, left and right change a value, A
 * chooses, B goes back. The menus themselves are in menus.c. */
#include "game.h"
#include "fmt.h"
#include "bindings.h"
#include "menu.h"
#include "settings.h"
#include "text.h"

extern const Menu rootMenu;

#define MENU_DEPTH 6
#define MENU_W 256
/* At most 14 rows: with the title row and the hint line the panel is 189 pixels high, and at its lowest place
 * (MENU_Y low) it ends at pixel 225 of 240. A longer menu scrolls. */
#define MENU_ROWS 14
#define ROW_H 11
/* Held up or down repeats after this many game frames, then every REPEAT_STEP frames. */
#define REPEAT_DELAY 6
#define REPEAT_STEP 1

u8 menuOpen;

/* The places of the panel's top left corner, for the choices left, centre, right and top, middle, low. The panel
 * is 256 wide and the screen 320: every choice keeps 12 pixels or more to the screen's edges. */
static const u8 menuXs[3] = { 12, 32, 52 };
static const u8 menuYs[3] = { 12, 28, 36 };
/* The alpha of the panel for each opacity step: clear to solid. The title bar is 16 more. */
static const u8 menuAlphas[MENU_OPACITY_STEPS] = { 0, 0x40, 0x80, 0xC0, 0xD8, 0xFF };

/* The colour of the panel (BAR 0) or its title bar (BAR 1), also used by the tools that take over the panel. */
u32 MenuPanelColor(int bar) {
    u32 a = menuAlphas[settings.menuOpacity - 1];

    if (bar) {
        a = a + 16 > 255 ? 255 : a + 16;
        return 0x3048A000 | a;
    }
    return 0x10183000 | a;
}

static const Menu *stack[MENU_DEPTH];
static u8 cursor[MENU_DEPTH];
static u8 scroll[MENU_DEPTH];
static int depth;
static u8 holdFrames;
const View *menuView; /* the open view, or NULL */

/* The menu opens at its top level, with the cursor where it was. */
void MenuOpen(void) {
    stack[0] = &rootMenu;
    depth = 0;
    menuView = NULL;
    menuOpen = 1;
}

void MenuShowView(const View *v) {
    menuView = v;
}

void MenuClose(void) {
    menuOpen = 0;
}

static void Change(const MenuItem *item, int step) {
    int v;
    u8 old = item->value != NULL ? *item->value : 0;

    switch (item->kind) {
        case ITEM_TOGGLE:
            *item->value ^= 1;
            break;
        case ITEM_CHOICE:
        case ITEM_NUMBER:
            v = *item->value + step;
            if (v > item->max) {
                v = item->kind == ITEM_CHOICE ? item->min : item->max;
            } else if (v < item->min) {
                v = item->kind == ITEM_CHOICE ? item->max : item->min;
            }
            *item->value = (u8)v;
            break;
    }
    /* A choice or a number with an action tells it when the value changed. */
    if ((item->kind == ITEM_CHOICE || item->kind == ITEM_NUMBER) && item->action != NULL && *item->value != old) {
        item->action(item->arg);
    }
}

void MenuInput(u16 pressed, u16 held) {
    const Menu *menu = stack[depth];
    const MenuItem *item = &menu->items[cursor[depth]];
    int move = 0;

    if (menuView != NULL) {
        if (!menuView->input(pressed, held)) {
            menuView = NULL;
        }
        return;
    }
    if (held & (CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT)) {
        holdFrames++;
    } else {
        holdFrames = 0;
    }
    /* A held direction repeats. */
    if (holdFrames > REPEAT_DELAY && (holdFrames - REPEAT_DELAY) % REPEAT_STEP == 0) {
        pressed |= held & (CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT);
    }
    if (pressed & CONT_UP) {
        move = -1;
    }
    if (pressed & CONT_DOWN) {
        move = 1;
    }
    if (move != 0) {
        cursor[depth] = (cursor[depth] + menu->count + move) % menu->count;
        if (cursor[depth] < scroll[depth]) {
            scroll[depth] = cursor[depth];
        }
        if (cursor[depth] >= scroll[depth] + MENU_ROWS) {
            scroll[depth] = cursor[depth] - MENU_ROWS + 1;
        }
        return;
    }
    if (pressed & (CONT_LEFT | CONT_RIGHT)) {
        /* Z held: steps of 10 for numbers. */
        int step = (held & CONT_Z) && item->kind == ITEM_NUMBER ? 10 : 1;
        Change(item, (pressed & CONT_LEFT) ? -step : step);
        return;
    }
    if (pressed & CONT_A) {
        switch (item->kind) {
            case ITEM_SUBMENU:
                if (depth + 1 < MENU_DEPTH) {
                    depth++;
                    stack[depth] = item->submenu;
                    cursor[depth] = 0;
                    scroll[depth] = 0;
                }
                break;
            case ITEM_ACTION:
                item->action(item->arg);
                break;
            case ITEM_BIND:
                BindingCapture(item->arg);
                break;
            case ITEM_VIEW:
                menuView = item->view;
                if (menuView->open != NULL) {
                    menuView->open();
                }
                break;
            default:
                Change(item, 1);
                break;
        }
        return;
    }
    if ((pressed & CONT_L) && item->kind == ITEM_BIND) {
        BindingClear(item->arg);
        return;
    }
    if (pressed & CONT_B) {
        if (depth > 0) {
            depth--;
        } else {
            MenuClose();
        }
    }
}

static void ValueText(const MenuItem *item, char *buf, int size) {
    buf[0] = '\0';
    switch (item->kind) {
        case ITEM_SUBMENU:
        case ITEM_VIEW:
            Format(buf, size, ">");
            break;
        case ITEM_TOGGLE:
            Format(buf, size, "%s", *item->value ? "on" : "off");
            break;
        case ITEM_CHOICE:
            Format(buf, size, "%s", item->names[*item->value]);
            break;
        case ITEM_NUMBER:
            Format(buf, size, "%d", *item->value);
            break;
        case ITEM_INFO:
            item->info(buf, size, item->arg);
            break;
        case ITEM_ACTION:
            if (item->info != NULL) {
                item->info(buf, size, item->arg);
            } else if (item->bind >= 0) {
                BindingText(item->bind, buf, size);
            }
            break;
        case ITEM_BIND:
            BindingText(item->arg, buf, size);
            break;
    }
}

void MenuDraw(Gfx **dl) {
    const View *v = menuView;

    if (v != NULL) {
        v->draw(dl);
        return;
    }
    const Menu *menu = stack[depth];
    int rows = menu->count < MENU_ROWS ? menu->count : MENU_ROWS;
    int h = (rows + 3) * ROW_H + 2;
    int mx = menuXs[settings.menuAcross - 1];
    int my = menuYs[settings.menuDown - 1];
    char value[48];

    TextRect(dl, mx, my, mx + MENU_W, my + h, MenuPanelColor(0));
    TextRect(dl, mx, my, mx + MENU_W, my + ROW_H + 4, MenuPanelColor(1));
    TextPrint(dl, mx + 6, my + 3, 1, 0xFFE070FF, menu->title);
    for (int r = 0; r < rows; r++) {
        int i = scroll[depth] + r;
        const MenuItem *item = &menu->items[i];
        int y = my + (r + 1) * ROW_H + 8;
        int selected = i == cursor[depth];
        u32 color = selected ? 0xFFFFFFFF : 0xB0B8C8FF;

        if (selected) {
            TextRect(dl, mx + 2, y - 2, mx + MENU_W - 2, y + ROW_H - 2, 0x5070D0B0);
        }
        TextPrint(dl, mx + 8, y, 1, color, item->label);
        ValueText(item, value, sizeof(value));
        if (value[0] != '\0') {
            TextPrint(dl, mx + MENU_W - 8 - TextWidth(value, 1), y, 1, selected ? 0xFFE070FF : 0xC0A860FF, value);
        }
    }
    if (menu->count > MENU_ROWS) {
        Format(value, sizeof(value), "%d/%d", cursor[depth] + 1, menu->count);
        TextPrint(dl, mx + MENU_W - 8 - TextWidth(value, 1), my + 3, 1, 0xB0B8C8FF, value);
    }
    const MenuItem *sel = &menu->items[cursor[depth]];

    if (sel->hint != NULL) {
        TextPrint(dl, mx + 8, my + h - ROW_H, 1, 0xFF9060FF, sel->hint);
    } else {
        TextPrint(dl, mx + 8, my + h - ROW_H, 1, 0x8090A8FF,
                  sel->kind == ITEM_BIND ? "A new binding  L clear  B back" : "A choose  B back  </> change");
    }
}
