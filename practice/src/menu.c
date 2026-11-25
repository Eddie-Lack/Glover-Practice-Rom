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
/* A held direction repeats after REPEAT_DELAY reads of the controller, first on every second read (REPEAT_SLOW
 * repeats), then on every read. A held repeat stops at the first and the last row or value; a new press wraps. */
#define REPEAT_DELAY 6
#define REPEAT_SLOW 8
#define DIRECTIONS (CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT)
/* Reads that A must be held on a MENU_HOLD row. */
#define HOLD_READS 10
/* The lowest pixel row the panel may reach (menupages.py: 12 pixels above the bottom). */
#define MENU_BOTTOM 228

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
static u8 holdFrames;   /* reads that the same directions have been held */
static u16 holdDirs;    /* the directions held on the last read */
static u16 blockedDirs; /* directions held when the menu opened: they act once let go */
static u8 confirmReads; /* reads that A has been held on a MENU_HOLD row */
const View *menuView; /* the open view, or NULL */

/* The menu opens where it was closed: the same page, cursor and view. */
void MenuOpen(void) {
    if (stack[0] == NULL) {
        stack[0] = &rootMenu;
        depth = 0;
    }
    blockedDirs = DIRECTIONS;
    holdFrames = 0;
    confirmReads = 0;
    menuOpen = 1;
}

void MenuShowView(const View *v) {
    menuView = v;
}

void MenuClose(void) {
    menuOpen = 0;
}

/* The number of rows of the open menu. A repeating menu shows one row for each of the live count. */
static int MenuCount(const Menu *menu) {
    return menu->repeat ? *menu->items[0].value : menu->count;
}

static const MenuItem *MenuRow(const Menu *menu, int i) {
    return &menu->items[menu->repeat ? 0 : i];
}

/* Keeps the cursor and the scroll inside the menu after the row count dropped. */
static void FitCursor(const Menu *menu) {
    int count = MenuCount(menu);
    int last = count > MENU_ROWS ? count - MENU_ROWS : 0;

    if (cursor[depth] >= count) {
        cursor[depth] = count > 0 ? count - 1 : 0;
    }
    if (scroll[depth] > last) {
        scroll[depth] = last;
    }
}

/* The first word of a name, for the groups of a CHOICE with arg 1. */
static int SameGroup(const char *a, const char *b) {
    while (*a != '\0' && *a != ' ' && *a == *b) {
        a++;
        b++;
    }
    return (*a == '\0' || *a == ' ') && (*b == '\0' || *b == ' ');
}

/* The value before V, wrapping. */
static int Prev(const MenuItem *item, int v) {
    return v == item->min ? item->max : v - 1;
}

/* The first value of the group that V is in. */
static int GroupStart(const MenuItem *item, int v) {
    for (int k = item->min; k < item->max && SameGroup(item->names[Prev(item, v)], item->names[v]); k++) {
        v = Prev(item, v);
    }
    return v;
}

/* Z with left or right on a grouped choice: the first name of the next group, or the start of this group (of the
 * group before, if V is its start). */
static int GroupStep(const MenuItem *item, int v, int dir) {
    int i = v;

    if (dir > 0) {
        for (int k = item->min; k <= item->max && SameGroup(item->names[i], item->names[v]); k++) {
            i = i >= item->max ? item->min : i + 1;
        }
        return i;
    }
    i = GroupStart(item, v);
    return i != v ? i : GroupStart(item, Prev(item, v));
}

static void Change(const MenuItem *item, int step, int repeat, int group) {
    int v;
    u8 old;

    if (item->kind != ITEM_TOGGLE && item->kind != ITEM_CHOICE && item->kind != ITEM_NUMBER) {
        return;
    }
    old = *item->value;
    switch (item->kind) {
        case ITEM_TOGGLE:
            *item->value ^= 1;
            break;
        case ITEM_CHOICE:
        case ITEM_NUMBER:
            if (group && item->kind == ITEM_CHOICE && item->arg == 1) {
                *item->value = (u8)GroupStep(item, *item->value, step);
                break;
            }
            v = *item->value + step;
            if (v > item->max) {
                v = item->kind == ITEM_CHOICE && !repeat ? item->min : item->max;
            } else if (v < item->min) {
                v = item->kind == ITEM_CHOICE && !repeat ? item->max : item->min;
            }
            *item->value = (u8)v;
            break;
    }
    /* A choice or a number with an action tells it when the value changed. */
    if ((item->kind == ITEM_CHOICE || item->kind == ITEM_NUMBER) && item->action != NULL && *item->value != old) {
        item->action(item->arg);
    }
}

/* The repeat of a held direction: returns the directions that act on this read, and sets *REPEAT when they are
 * repeats rather than a new press. */
static u16 Repeat(u16 pressed, u16 held, int *repeat) {
    u16 dirs = held & DIRECTIONS;

    *repeat = 0;
    /* A direction that was held when the menu opened acts only after it is let go. */
    blockedDirs &= held;
    dirs &= ~blockedDirs;
    pressed &= ~blockedDirs;
    if (dirs != holdDirs) {
        /* Another direction (a D-pad rocked from right to down): a new press, with its own delay. */
        holdDirs = dirs;
        holdFrames = 0;
        return pressed & DIRECTIONS;
    }
    if (dirs == 0) {
        holdFrames = 0;
        return 0;
    }
    if (holdFrames < 255) {
        holdFrames++;
    }
    if (pressed & DIRECTIONS) {
        return pressed & DIRECTIONS;
    }
    if (holdFrames > REPEAT_DELAY &&
        (holdFrames > REPEAT_DELAY + 2 * REPEAT_SLOW || (holdFrames - REPEAT_DELAY) % 2 == 0)) {
        *repeat = 1;
        return dirs;
    }
    return 0;
}

void MenuInput(u16 pressed, u16 held) {
    const Menu *menu = stack[depth];
    const MenuItem *item;
    int move = 0;
    int count;
    int repeat;
    u16 dirs = Repeat(pressed, held, &repeat);

    pressed = (pressed & ~DIRECTIONS) | dirs;
    if (menuView != NULL) {
        if (!menuView->input(pressed, held & ~blockedDirs)) {
            menuView = NULL;
        }
        return;
    }
    FitCursor(menu);
    count = MenuCount(menu);
    item = MenuRow(menu, cursor[depth]);
    if (item->kind == ITEM_ACTION && item->min == MENU_HOLD && (held & CONT_A) && confirmReads > 0) {
        /* A held on a row that asks for a hold: it runs once the bar is full. */
        if (++confirmReads >= HOLD_READS) {
            confirmReads = 0;
            item->action(item->arg);
        }
        return;
    }
    if (!(held & CONT_A)) {
        confirmReads = 0;
    }
    if (pressed & CONT_UP) {
        move = -1;
    }
    if (pressed & CONT_DOWN) {
        move = 1;
    }
    if (move != 0) {
        if (count == 0) {
            return;
        }
        confirmReads = 0;
        if (repeat && (cursor[depth] + move < 0 || cursor[depth] + move >= count)) {
            /* A held move stops at the ends. */
            return;
        }
        cursor[depth] = (cursor[depth] + count + move) % count;
        if (cursor[depth] < scroll[depth]) {
            scroll[depth] = cursor[depth];
        }
        if (cursor[depth] >= scroll[depth] + MENU_ROWS) {
            scroll[depth] = cursor[depth] - MENU_ROWS + 1;
        }
        return;
    }
    if (pressed & (CONT_LEFT | CONT_RIGHT)) {
        /* Z held: steps of 10 for numbers, the next group for a grouped choice. */
        int step = (held & CONT_Z) && item->kind == ITEM_NUMBER ? 10 : 1;
        if (item->kind == ITEM_ACTION && item->info != NULL && item->min != MENU_HOLD && !repeat) {
            /* A row that shows its own state (on, off, shown, hidden) switches with left and right too. */
            item->action(item->arg);
            return;
        }
        Change(item, (pressed & CONT_LEFT) ? -step : step, repeat, (held & CONT_Z) != 0);
        return;
    }
    if ((pressed & CONT_A) && item->kind == ITEM_ACTION && item->min == MENU_HOLD) {
        confirmReads = 1;
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
            case ITEM_QUICK:
                if (item->action != NULL) {
                    item->action(item->arg);
                }
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
                Change(item, 1, 0, 0);
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

static void ValueText(const MenuItem *item, int arg, char *buf, int size) {
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
            item->info(buf, size, arg);
            break;
        case ITEM_ACTION:
            if (item->info != NULL) {
                item->info(buf, size, arg);
            }
            break;
        case ITEM_QUICK:
            BindingText(item->arg, buf, size);
            break;
        case ITEM_BIND:
            BindingText(item->arg, buf, size);
            break;
    }
}

/* The key help of a row: only the keys that do something there. */
static const char *KeyHelp(const MenuItem *item, int count) {
    if (count == 0 || item->kind == ITEM_INFO) {
        return "B back";
    }
    switch (item->kind) {
        case ITEM_BIND:
            return "A new binding  L clear  B back";
        case ITEM_TOGGLE:
        case ITEM_CHOICE:
        case ITEM_NUMBER:
            return "A choose  B back  </> change";
        case ITEM_ACTION:
            if (item->info != NULL && item->min != MENU_HOLD) {
                return "A choose  B back  </> change";
            }
            return "A choose  B back";
        default:
            return "A choose  B back";
    }
}

/* Splits TEXT at the last space that keeps the first part within WIDTH pixels: the first part goes to LINE, and
 * the rest is returned (NULL when it all fits). */
static const char *Wrap(const char *text, int width, char *line, int size) {
    int n = 0;
    int cut = -1;

    line[0] = '\0';
    while (text[n] != '\0' && n < size - 1) {
        line[n] = text[n];
        line[n + 1] = '\0';
        if (TextWidth(line, 1) > width) {
            break;
        }
        if (text[n] == ' ') {
            cut = n;
        }
        n++;
    }
    if (text[n] == '\0') {
        return NULL;
    }
    if (cut < 0) {
        cut = n;
    }
    line[cut] = '\0';
    return text[cut] == ' ' ? text + cut + 1 : text + cut;
}

void MenuDraw(Gfx **dl) {
    const View *v = menuView;

    if (v != NULL) {
        v->draw(dl);
        return;
    }
    const Menu *menu = stack[depth];
    int count;
    int rows;
    int first;
    int h;
    int mx = menuXs[settings.menuAcross - 1];
    int my = menuYs[settings.menuDown - 1];
    char value[48];
    char number[4];
    char line[48];
    const char *help;
    const char *rest;
    u32 helpColor;

    FitCursor(menu);
    count = MenuCount(menu);
    const MenuItem *sel = MenuRow(menu, cursor[depth]);
    help = sel->hint != NULL ? sel->hint : KeyHelp(sel, count);
    helpColor = sel->hint != NULL ? 0xFF9060FF : 0x8090A8FF;
    rest = Wrap(help, MENU_W - 16, line, sizeof(line));
    /* A compact page shows only the selected row: what it moves stays in sight behind the panel. */
    rows = menu->compact ? 1 : count < MENU_ROWS ? count : MENU_ROWS;
    first = menu->compact ? cursor[depth] : scroll[depth];
    h = (rows + 3 + (rest != NULL)) * ROW_H + 2;
    if (my + h > MENU_BOTTOM) {
        my = MENU_BOTTOM - h;
    }
    TextRect(dl, mx, my, mx + MENU_W, my + h, MenuPanelColor(0));
    TextRect(dl, mx, my, mx + MENU_W, my + ROW_H + 4, MenuPanelColor(1));
    TextPrint(dl, mx + 6, my + 3, 1, 0xFFE070FF, menu->title);
    for (int r = 0; r < rows && count > 0; r++) {
        int i = first + r;
        const MenuItem *item = MenuRow(menu, i);
        const char *label = item->label;
        int y = my + (r + 1) * ROW_H + 8;
        int selected = i == cursor[depth];
        u32 color = selected ? 0xFFFFFFFF : 0xB0B8C8FF;

        if (selected) {
            TextRect(dl, mx + 2, y - 2, mx + MENU_W - 2, y + ROW_H - 2, 0x5070D0B0);
            if (confirmReads > 0) {
                /* A held on a row that asks for a hold: the bar fills until it runs. */
                TextRect(dl, mx + 2, y - 2, mx + 2 + (MENU_W - 4) * confirmReads / HOLD_READS, y + ROW_H - 2,
                         0xFF9060B0);
            }
        }
        if (menu->repeat) {
            Format(number, sizeof(number), "%d", i + 1);
            label = number;
        }
        TextPrint(dl, mx + 8, y, 1, color, label);
        ValueText(item, menu->repeat ? i : item->arg, value, sizeof(value));
        if (value[0] != '\0') {
            TextPrint(dl, mx + MENU_W - 8 - TextWidth(value, 1), y, 1, selected ? 0xFFE070FF : 0xC0A860FF, value);
        }
    }
    if (count > MENU_ROWS || (menu->compact && count > 1)) {
        Format(value, sizeof(value), "%d/%d", cursor[depth] + 1, count);
        TextPrint(dl, mx + MENU_W - 8 - TextWidth(value, 1), my + 3, 1, 0xB0B8C8FF, value);
    }
    /* The help line, on two lines when it is longer than the panel is wide. */
    TextPrint(dl, mx + 8, my + h - ROW_H * (rest != NULL ? 2 : 1), 1, helpColor, line);
    if (rest != NULL) {
        TextPrint(dl, mx + 8, my + h - ROW_H, 1, helpColor, rest);
    }
}
