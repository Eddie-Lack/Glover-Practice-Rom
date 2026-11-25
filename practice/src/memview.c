/* The memory view: RDRAM from an address chosen digit by digit. Left and right choose a digit, up and down change
 * it. C left and C right choose the size of the value at the address: a word, a half word or a byte. R switches
 * between the address and the value at the address, which is then changed in place. The value has its own digit
 * cursor, which starts on its lowest digit. A adds the word at the address to the watch list on the HUD and turns
 * the list on; L clears the list. B goes back. Z marks the address and its value as a poke, and Start opens the
 * second page, the list of pokes (corrupt.h). The Memory corruption menu opens that page directly. In the grid,
 * the words of the watch list are green and the words of a poke orange.
 *
 * MemViewEdit opens the view locked on one word of the practice data (the frozen seed): only that word shows, and
 * only its digits change. An edit there is not a poke. */
#include "besttimes.h"
#include "corrupt.h"
#include "fmt.h"
#include "game.h"
#include "menu.h"
#include "memview.h"
#include "safe.h"
#include "settings.h"
#include "text.h"

#define VIEW_X 16
#define VIEW_Y 28
#define VIEW_W 288
#define ROWS 12
#define ROW_H 10
#define RAM_FIRST 0x80000000u
#define RAM_END 0x80800000u /* the end of the Expansion Pak's memory */
#define YELLOW 0xFFE070FF
#define GREEN 0x80D080FF
#define ORANGE 0xFF9060FF
#define GREY 0x707888FF
#define HELP 0x8090A8FF

u32 watches[WATCH_COUNT];

static u32 address = 0x801E76D4; /* timer321: a first example */
static u8 size = 4;              /* bytes of the value at the address: 4, 2 or 1 */
static u8 addressDigit;          /* 0: the highest of the address's 8 digits */
static u8 valueDigit = 7;        /* 0: the highest of the value's 2 * size digits */
static u8 editValue;             /* 1: up and down change the value at the address */
static u8 page;                  /* 0: the words; 1: the pokes */
static u8 pokesOnly;             /* 1: the pokes page was opened directly, so B closes the view */
static u8 selected;              /* the poke chosen on the pokes page */
static u32 *locked;              /* MemViewEdit's word, or NULL */

static u16 cLast;      /* the C buttons held at the last call */
static u16 cCandidate; /* the C button held alone since it went down, or 0 */

u16 ViewCTaps(u16 held) {
    u16 c = held & CONT_C_ALL;
    u16 tap = 0;

    if (c & (c - 1)) {
        /* Two or more: the menu's button. */
        cCandidate = 0;
    } else if (c != 0 && cLast == 0) {
        cCandidate = c;
    } else if (c != cCandidate) {
        if (c == 0 && cLast == cCandidate) {
            tap = cCandidate;
        }
        cCandidate = 0;
    }
    cLast = c;
    return tap;
}

static u32 Clamp(u32 a) {
    a &= ~(u32)(size - 1);
    if (a < RAM_FIRST) {
        return RAM_FIRST;
    }
    return a > RAM_END - size ? RAM_END - size : a;
}

static u32 Read(u32 a) {
    switch (size) {
        case 1:
            return *(volatile u8 *)a;
        case 2:
            return *(volatile u16 *)a;
        default:
            return *(volatile u32 *)a;
    }
}

static void Open(void) {
    editValue = 0;
    page = 0;
    pokesOnly = 0;
    locked = NULL;
}

static void OpenPokes(void) {
    Open();
    page = 1;
    pokesOnly = 1;
}

u32 MemViewAddress(void) {
    return address;
}

void MemViewEdit(u32 *word) {
    Open();
    locked = word;
    valueDigit = 7;
}

/* Adds step to digit d (0 is the highest) of the DIGITS digits of v, wrapping within that digit. */
static u32 StepDigit(u32 v, int digits, int d, int step) {
    int shift = (digits - 1 - d) * 4;
    u32 n = ((v >> shift) + step) & 0xF;

    return (v & ~(0xFu << shift)) | (n << shift);
}

static int Watched(u32 a) {
    for (int i = 0; i < WATCH_COUNT; i++) {
        if (watches[i] == a) {
            return 1;
        }
    }
    return 0;
}

static void Watch(u32 a) {
    a &= ~3u;
    /* The list goes on: a word that is added shows on the HUD. */
    settings.hud[HUD_WATCH].on = 1;
    for (int i = 0; i < WATCH_COUNT; i++) {
        if (watches[i] == 0 || watches[i] == a) {
            watches[i] = a;
            return;
        }
    }
    for (int i = 0; i < WATCH_COUNT - 1; i++) {
        watches[i] = watches[i + 1];
    }
    watches[WATCH_COUNT - 1] = a;
}

/* Whether a poke covers a byte of the word at A. */
static int Poked(u32 a) {
    for (int i = 0; i < POKE_COUNT; i++) {
        const Poke *p = &corrupt.pokes[i];

        if (p->address != 0 && p->address < a + 4 && p->address + p->size > a) {
            return 1;
        }
    }
    return 0;
}

static const char *const pokeModeNames[POKE_MODES] = { "off", "once", "freeze", "random", "flip" };

/* The pokes page: up and down choose a poke, left and right its mode, C left and right its size, C up and down
 * the bits that random and flip change, L and R how often they act, A writes the poke now, Z clears it, Start
 * shows its address in the words. */
static int PokeInput(u16 pressed, u16 taps) {
    Poke *p = &corrupt.pokes[selected];
    int used = p->address != 0;

    if (pressed & CONT_B) {
        if (pokesOnly) {
            return 0;
        }
        page = 0;
        return 1;
    }
    if (pressed & CONT_UP) {
        selected = (selected + POKE_COUNT - 1) % POKE_COUNT;
    }
    if (pressed & CONT_DOWN) {
        selected = (selected + 1) % POKE_COUNT;
    }
    if (!used) {
        return 1;
    }
    if (pressed & CONT_LEFT) {
        p->mode = (p->mode + POKE_MODES - 1) % POKE_MODES;
    }
    if (pressed & CONT_RIGHT) {
        p->mode = (p->mode + 1) % POKE_MODES;
    }
    if (taps & CONT_CRIGHT) {
        p->size = p->size == 4 ? 1 : p->size * 2;
        p->bits = p->size * 8;
    }
    if (taps & CONT_CLEFT) {
        p->size = p->size == 1 ? 4 : p->size / 2;
        p->bits = p->size * 8;
    }
    /* Bits: 1, 2, 4, 8, 16, 32 as far as the size allows. */
    if (taps & CONT_CUP) {
        p->bits = p->bits * 2 > p->size * 8 ? 1 : p->bits * 2;
    }
    if (taps & CONT_CDOWN) {
        p->bits = p->bits <= 1 ? p->size * 8 : (p->bits + 1) / 2;
    }
    if (pressed & CONT_R) {
        p->period = (p->period + 1) % 6;
    }
    if (pressed & CONT_L) {
        p->period = (p->period + 5) % 6;
    }
    if (pressed & CONT_A) {
        PokeApply(p);
    }
    if (pressed & CONT_Z) {
        PokeClear(p);
        return 1;
    }
    if (pressed & CONT_START) {
        size = p->size == 1 || p->size == 2 ? p->size : 4;
        address = Clamp(p->address);
        valueDigit = size * 2 - 1;
        editValue = 0;
        page = 0;
    }
    PokeFix(p);
    PokeRecount();
    return 1;
}

/* The locked view: the digits of one word of the practice data. It is not game memory: no poke and no marker. The
 * seed changes, which the level's best time counts. */
static int LockedInput(u16 pressed) {
    int step = (pressed & CONT_UP) ? 1 : (pressed & CONT_DOWN) ? -1 : 0;

    if (pressed & CONT_B) {
        return 0;
    }
    if (pressed & CONT_LEFT) {
        valueDigit = (valueDigit + 7) % 8;
    }
    if (pressed & CONT_RIGHT) {
        valueDigit = (valueDigit + 1) % 8;
    }
    if (step != 0) {
        *locked = StepDigit(*locked, 8, valueDigit % 8, step);
        BestTimesTaint(BT_OTHER);
    }
    return 1;
}

static int Input(u16 pressed, u16 held) {
    u16 taps = ViewCTaps(held);
    int digits = size * 2;
    int step = 0;

    if (locked != NULL) {
        return LockedInput(pressed);
    }
    if (page == 1) {
        return PokeInput(pressed, taps);
    }
    if (pressed & CONT_B) {
        return 0;
    }
    if (pressed & CONT_Z) {
        PokeMark(address, Read(address), size);
    }
    if (pressed & CONT_START) {
        page = 1;
        pokesOnly = 0;
        return 1;
    }
    if (taps & (CONT_CLEFT | CONT_CRIGHT)) {
        /* C right: a smaller value (4, 2, 1 bytes); C left: a larger one. */
        if (taps & CONT_CRIGHT) {
            size = size == 1 ? 4 : size / 2;
        } else {
            size = size == 4 ? 1 : size * 2;
        }
        address = Clamp(address);
        digits = size * 2;
        valueDigit = digits - 1;
    }
    if (pressed & CONT_R) {
        editValue ^= 1;
    }
    if (pressed & CONT_A) {
        Watch(address);
    }
    if (pressed & CONT_L) {
        for (int i = 0; i < WATCH_COUNT; i++) {
            watches[i] = 0;
        }
    }
    if (pressed & CONT_LEFT) {
        if (editValue) {
            valueDigit = (valueDigit + digits - 1) % digits;
        } else {
            addressDigit = (addressDigit + 7) % 8;
        }
    }
    if (pressed & CONT_RIGHT) {
        if (editValue) {
            valueDigit = (valueDigit + 1) % digits;
        } else {
            addressDigit = (addressDigit + 1) % 8;
        }
    }
    if (pressed & CONT_UP) {
        step = 1;
    }
    if (pressed & CONT_DOWN) {
        step = -1;
    }
    if (step != 0) {
        if (editValue) {
            /* An edit in place is a poke: the run is not kept and the HUD shows the marker. As for the pokes, the
             * practice segment and its data are not written. */
            PokeWrite(address, StepDigit(Read(address), digits, valueDigit % digits, step), size);
        } else {
            address = Clamp(StepDigit(address, 8, addressDigit, step));
        }
    }
    return 1;
}

static void Panel(Gfx **dl, int rows, const char *title) {
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 20 + rows * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, YELLOW, title);
}

static void PokesDraw(Gfx **dl) {
    char buf[64];
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;

    Panel(dl, ROWS + 4, "Pokes");
    TextPrint(dl, x, y, 1, HELP, "# address  value    mode   bits every writes");
    for (int i = 0; i < POKE_COUNT; i++) {
        const Poke *p = &corrupt.pokes[i];
        int row = y + (i + 1) * ROW_H + 2;
        u32 color = i == selected ? YELLOW : 0xFFFFFFFF;
        char value[9];

        if (i == selected) {
            TextRect(dl, VIEW_X + 2, row - 1, VIEW_X + VIEW_W - 2, row + ROW_H - 2, 0x5070D060);
        }
        if (p->address == 0) {
            Format(buf, sizeof(buf), "%d -", i + 1);
            TextPrint(dl, x, row, 1, HELP, buf);
            continue;
        }
        Format(value, sizeof(value), p->size == 1 ? "%02X" : p->size == 2 ? "%04X" : "%08X", p->value);
        Format(buf, sizeof(buf), "%d %08X %-8s %-6s %-4d /%-3d  %u", i + 1, p->address, value, pokeModeNames[p->mode],
               p->bits, pokePeriods[p->period % 6], p->count);
        TextPrint(dl, x, row, 1, color, buf);
    }
    y += (POKE_COUNT + 2) * ROW_H;
    TextPrint(dl, x, y, 1, HELP, "</> mode  C</> size  C^v bits  L R every");
    TextPrint(dl, x, y + ROW_H, 1, HELP, "A write now  Z clear  Start words  B back");
    TextPrint(dl, x, y + 2 * ROW_H, 1, CorruptMarker() ? ORANGE : HELP,
              CorruptMarker() ? "Modified game! Writes only to RDRAM" : "Writes only to RDRAM, not the practice code");
}

/* A line under digit D of a field that starts COLUMN characters into the text at X. */
static void DigitMark(Gfx **dl, int x, int y, int column, int d) {
    TextRect(dl, x + (column + d) * 6 - 1, y + 9, x + (column + d) * 6 + 5, y + 10, YELLOW);
}

static void LockedDraw(Gfx **dl) {
    char buf[16];
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;

    Panel(dl, 3, "Random numbers");
    Format(buf, sizeof(buf), "%08X", *locked);
    TextPrint(dl, x, y, 1, 0xFFFFFFFF, buf);
    DigitMark(dl, x, y, 0, valueDigit % 8);
    TextPrint(dl, x, y + 2 * ROW_H, 1, HELP, "^v change the word  B back");
}

static void Draw(Gfx **dl) {
    u32 first = (address & ~0xFu) - (ROWS / 2) * 16;
    u32 value = Read(address);
    char buf[64];
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;
    int digits = size * 2;

    if (locked != NULL) {
        LockedDraw(dl);
        return;
    }
    if (page == 1) {
        PokesDraw(dl);
        return;
    }
    Panel(dl, ROWS + 4, "Memory");

    /* The address and the value, with the chosen digit marked. A value that an edit may not change is grey. */
    Format(buf, sizeof(buf), size == 1 ? "%08X: %02X" : size == 2 ? "%08X: %04X" : "%08X: %08X", address, value);
    TextPrint(dl, x, y, 1, editValue && !PokeAllowed(address, size) ? GREY : 0xFFFFFFFF, buf);
    DigitMark(dl, x, y, editValue ? 10 : 0, editValue ? valueDigit % digits : addressDigit);
    /* The word as a float only when it is safe for the FPU (safe.h). */
    if (size == 4 && SafeFloat((const f32 *)address)) {
        Format(buf, sizeof(buf), "%d  %.4f", (s32)value, (double)*(volatile f32 *)address);
    } else if (size == 4) {
        Format(buf, sizeof(buf), "%d", (s32)value);
    } else {
        Format(buf, sizeof(buf), "%d", size == 1 ? (s32)(s8)value : (s32)(s16)value);
    }
    TextPrint(dl, x + 120, y, 1, 0xB0B8C8FF, buf);

    for (int r = 0; r < ROWS; r++) {
        u32 a = first + r * 16;
        int row = y + (r + 2) * ROW_H;

        if (a < RAM_FIRST || a >= RAM_END) {
            continue;
        }
        Format(buf, sizeof(buf), "%08X", a);
        TextPrint(dl, x, row, 1, HELP, buf);
        for (int w = 0; w < 4; w++) {
            u32 p = a + w * 4;
            u32 color = p == (address & ~3u) ? YELLOW : Poked(p) ? ORANGE : Watched(p) ? GREEN : 0xFFFFFFFF;

            Format(buf, sizeof(buf), "%08X", *(volatile u32 *)p);
            TextPrint(dl, x + 54 + w * 54, row, 1, color, buf);
        }
    }
    TextPrint(dl, x, y + (ROWS + 2) * ROW_H, 1, HELP,
              editValue ? "^v change the word  R address  B back" : "^v</> address  R edit  A watch  L clear");
    TextPrint(dl, x, y + (ROWS + 3) * ROW_H, 1, HELP, "Z poke this word  Start pokes");
}

void WatchDraw(Gfx **dl, int x, int y) {
    char buf[32];

    for (int i = 0; i < WATCH_COUNT; i++) {
        if (watches[i] != 0) {
            Format(buf, sizeof(buf), "%08X %08X", watches[i], *(volatile u32 *)watches[i]);
            TextPrint(dl, x, y, 1, 0xFFFFFFFF, buf);
            y += 10;
        }
    }
}

const View memoryView = { Open, Input, Draw };
const View pokeView = { OpenPokes, Input, Draw };
