/* The loader. The boot hook (hooks/asm/PracticeMeasureFrameTicks.s) copies it into the game's heap, which is
 * still unused, and calls PracticeLoader. It holds its own copy of the text code (the Makefile compiles text.c and
 * font.c again with PRACTICE_LOADER), because without the Expansion Pak the practice segment cannot be loaded. */
#include "game.h"
#include "practice.h"
#include "text.h"

#define SCREEN_WIDTH 320
#define EXPANSION_PAK_SIZE 0x800000
/* The CPU counter runs at 46.875 MHz; this is one NTSC video frame. */
#define COUNT_PER_FRAME (46875000 / 60)

static u8 atlas[TEXT_ATLAS_SIZE] __attribute__((aligned(8)));

/* Draws one frame of the message with the game's own display list code, as DrawGraphics does. The RDP draws
 * the text: emulators with high-level graphics plugins show only frame buffers that the RDP drew. */
static void DrawMessageFrame(void) {
    static const char *const lines[] = {
        "THE GLOVER PRACTICE ROM NEEDS",
        "THE N64 EXPANSION PAK.",
        "IN AN EMULATOR, TURN IT ON IN THE SETTINGS.",
    };
    const char *title = "EXPANSION PAK REQUIRED";

    dynamicp = dynamic[draw_buffer];
    tlistp = tlistPointers[draw_buffer];
    glistp = (Gfx *)(dynamicp + DYNAMIC_GLIST);
    currentTask = 1;
    *(Gfx **)((u8 *)tlistp[currentTask] + TASK_DATA_PTR) = glistp;
    InitDisplayLists();
    ClearZBuffer();
    ClearFrameBuffer();
    TextBegin(&glistp, NULL);
    TextPrint(&glistp, (SCREEN_WIDTH - TextWidth(title, 2)) / 2, 84, 2, 0xFFFFFFFF, title);
    for (u32 i = 0; i < ARRAY_COUNT(lines); i++) {
        TextPrint(&glistp, (SCREEN_WIDTH - TextWidth(lines[i], 1)) / 2, 124 + i * 12, 1, 0xC8C8C8FF, lines[i]);
    }
    CleanupAndSendDisplayList(0, 0x40);
    SendLastFrame();
}

/* Shows the message and stops. The game's scheduler and graphics tasks are set up, but its graphics thread does
 * not exist yet, so this code sends the frames itself. */
static void NoExpansionPak(void) {
    TextInit(atlas);
    for (;;) {
        u32 start = osGetCount();
        DrawMessageFrame();
        /* Two video frames: the task has finished long before the next one is sent. */
        while (osGetCount() - start < 2 * COUNT_PER_FRAME) {
        }
    }
}

static void Clear(u8 *start, u8 *end) {
    for (u32 *p = (u32 *)start; p < (u32 *)end; p++) {
        *p = 0;
    }
}

void PracticeLoader(void) {
    /* The boot hook copied the code and data; the bss may hold anything. */
    Clear(loader_BSS_START, loader_BSS_END);
    if (osMemSize < EXPANSION_PAK_SIZE) {
        NoExpansionPak();
    }
    /* DmaRomToRam takes a cartridge address, as the game passes it. */
    DmaRomToRam(0xB0000000 + (u32)practice_ROM_START, practice_VRAM, practice_ROM_END - practice_ROM_START);
    Clear(practice_BSS_START, practice_BSS_END);
}
