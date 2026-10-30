/* The memory view: RDRAM as words, from an address chosen digit by digit. Left and right choose a digit, up and
 * down change it. R switches between the address and the word at the address, which is then changed in place.
 * A adds the word at the address to the watch list on the HUD; L clears the list. B goes back.
 * Z marks the address and its word as a poke, and Start opens the second page, the list of pokes
 * (corrupt.h). The Memory corruption menu opens that page directly. */
#include "besttimes.h"
#include "corrupt.h"
#include "fmt.h"
#include "game.h"
#include "menu.h"
#include "memview.h"
#include "safe.h"
#include "text.h"

#define VIEW_X 16
#define VIEW_Y 28
#define VIEW_W 288
#define ROWS 12
#define ROW_H 10
#define RAM_FIRST 0x80000000
#define RAM_LAST 0x807FFFFC /* the last word of the Expansion Pak's memory */

u32 watches[WATCH_COUNT];

static u32 address = 0x801E76D4; /* timer321: a first example */
static u8 digit;                 /* 0: the highest of the 8 digits */
static u8 editValue;             /* 1: up and down change the word at the address */
static u8 page;                  /* 0: the words; 1: the pokes */
static u8 pokesOnly;             /* 1: the pokes page was opened directly, so B closes the view */
static u8 selected;              /* the poke chosen on the pokes page */

static u32 Clamp(u32 a) {
    a &= ~3u;
    if (a < RAM_FIRST) {
        return RAM_FIRST;
    }
    return a > RAM_LAST ? RAM_LAST : a;
}

static void Open(void) {
    digit = 0;
    editValue = 0;
    page = 0;
    pokesOnly = 0;
}

static void OpenPokes(void) {
    page = 1;
    pokesOnly = 1;
}

u32 MemViewAddress(void) {
    return address;
}

void MemViewEdit(u32 *word) {
    address = Clamp((u32)word);
    Open();
    editValue = 1;
    digit = 7;
}

/* Adds step to digit d (0 is the highest) of v, wrapping within that digit. */
static u32 StepDigit(u32 v, int d, int step) {
    int shift = (7 - d) * 4;
    u32 n = ((v >> shift) + step) & 0xF;

    return (v & ~(0xFu << shift)) | (n << shift);
}

static void Watch(u32 a) {
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

static const char *const pokeModeNames[POKE_MODES] = { "off", "once", "freeze", "random", "flip" };

/* The pokes page: up and down choose a poke, left and right its mode, C left and right its size, C up and down
 * the bits that random and flip change, L and R how often they act, A writes the poke now, Z clears it, Start
 * shows its address in the words. */
static int PokeInput(u16 pressed) {
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
    if (pressed & CONT_CRIGHT) {
        p->size = p->size == 4 ? 1 : p->size * 2;
        p->bits = p->size * 8;
    }
    if (pressed & CONT_CLEFT) {
        p->size = p->size == 1 ? 4 : p->size / 2;
        p->bits = p->size * 8;
    }
    /* Bits: 1, 2, 4, 8, 16, 32 as far as the size allows. */
    if (pressed & CONT_CUP) {
        p->bits = p->bits * 2 > p->size * 8 ? 1 : p->bits * 2;
    }
    if (pressed & CONT_CDOWN) {
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
        address = Clamp(p->address);
        editValue = 0;
        page = 0;
    }
    PokeFix(p);
    PokeRecount();
    return 1;
}

static int Input(u16 pressed, u16 held) {
    int step = 0;
    (void)held;

    if (page == 1) {
        return PokeInput(pressed);
    }
    if (pressed & CONT_B) {
        return 0;
    }
    if (pressed & CONT_Z) {
        PokeMark(address, *(volatile u32 *)address);
    }
    if (pressed & CONT_START) {
        page = 1;
        pokesOnly = 0;
        return 1;
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
        digit = (digit + 7) % 8;
    }
    if (pressed & CONT_RIGHT) {
        digit = (digit + 1) % 8;
    }
    if (pressed & CONT_UP) {
        step = 1;
    }
    if (pressed & CONT_DOWN) {
        step = -1;
    }
    if (step != 0) {
        if (editValue) {
            u32 *p = (u32 *)address;
            *p = StepDigit(*p, digit, step);
            /* An edit in place is a poke: the run is not kept and the HUD shows the marker. */
            corrupt.dirty = 1;
            BestTimesTaint(BT_POKES);
        } else {
            address = Clamp(StepDigit(address, digit, step));
        }
    }
    return 1;
}

static void PokesDraw(Gfx **dl) {
    char buf[64];
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;

    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 20 + (ROWS + 4) * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, 0xFFE070FF, "Pokes");
    TextPrint(dl, x, y, 1, 0x8090A8FF, "# address  value    mode   bits every writes");
    for (int i = 0; i < POKE_COUNT; i++) {
        const Poke *p = &corrupt.pokes[i];
        int row = y + (i + 1) * ROW_H + 2;
        u32 color = i == selected ? 0xFFE070FF : 0xFFFFFFFF;
        char value[9];

        if (i == selected) {
            TextRect(dl, VIEW_X + 2, row - 1, VIEW_X + VIEW_W - 2, row + ROW_H - 2, 0x5070D060);
        }
        if (p->address == 0) {
            Format(buf, sizeof(buf), "%d -", i + 1);
            TextPrint(dl, x, row, 1, 0x8090A8FF, buf);
            continue;
        }
        Format(value, sizeof(value), p->size == 1 ? "%02X" : p->size == 2 ? "%04X" : "%08X", p->value);
        Format(buf, sizeof(buf), "%d %08X %-8s %-6s %-4d /%-3d  %u", i + 1, p->address, value, pokeModeNames[p->mode],
               p->bits, pokePeriods[p->period % 6], p->count);
        TextPrint(dl, x, row, 1, color, buf);
    }
    y += (POKE_COUNT + 2) * ROW_H;
    TextPrint(dl, x, y, 1, 0x8090A8FF, "</> mode  C</> size  C^v bits  L R every");
    TextPrint(dl, x, y + ROW_H, 1, 0x8090A8FF, "A write now  Z clear  Start words  B back");
    TextPrint(dl, x, y + 2 * ROW_H, 1, CorruptMarker() ? 0xFF9060FF : 0x8090A8FF,
              CorruptMarker() ? "Modified game! Writes only to RDRAM" : "Writes only to RDRAM, not the practice code");
}

static void Draw(Gfx **dl) {
    u32 first = (address & ~0xFu) - (ROWS / 2) * 16;
    u32 value = *(volatile u32 *)address;
    char buf[64];
    int x = VIEW_X + 6;
    int y = VIEW_Y + 16;

    if (page == 1) {
        PokesDraw(dl);
        return;
    }
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 20 + (ROWS + 4) * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, 0xFFE070FF, "Memory");

    /* The address and the word, with the chosen digit marked. */
    Format(buf, sizeof(buf), "%08X: %08X", address, value);
    TextPrint(dl, x, y, 1, 0xFFFFFFFF, buf);
    TextRect(dl, x + (editValue ? 10 + digit : digit) * 6 - 1, y + 9, x + (editValue ? 10 + digit : digit) * 6 + 5,
             y + 10, 0xFFE070FF);
    /* The word as a float only when it is safe for the FPU (safe.h). */
    if (SafeFloat((const f32 *)address)) {
        Format(buf, sizeof(buf), "%d  %.4f", (s32)value, (double)*(volatile f32 *)address);
    } else {
        Format(buf, sizeof(buf), "%d", (s32)value);
    }
    TextPrint(dl, x + 120, y, 1, 0xB0B8C8FF, buf);

    for (int r = 0; r < ROWS; r++) {
        u32 a = first + r * 16;
        int row = y + (r + 2) * ROW_H;

        if (a < RAM_FIRST || a > RAM_LAST) {
            continue;
        }
        Format(buf, sizeof(buf), "%08X", a);
        TextPrint(dl, x, row, 1, 0x8090A8FF, buf);
        for (int w = 0; w < 4; w++) {
            u32 *p = (u32 *)(a + w * 4);
            Format(buf, sizeof(buf), "%08X", *(volatile u32 *)p);
            TextPrint(dl, x + 54 + w * 54, row, 1, (u32)p == address ? 0xFFE070FF : 0xFFFFFFFF, buf);
        }
    }
    TextPrint(dl, x, y + (ROWS + 2) * ROW_H, 1, 0x8090A8FF,
              editValue ? "^v change the word  R address  B back" : "^v</> address  R edit  A watch  L clear");
    TextPrint(dl, x, y + (ROWS + 3) * ROW_H, 1, 0x8090A8FF, "Z poke this word  Start pokes");
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
