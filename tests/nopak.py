"""Without the Expansion Pak the practice ROM shows its message and stops. The game's logic never runs.

The message is redrawn every other video frame, but a test may also stop a run on a timer: see harness.py.
The frame buffer check needs --gfx angrylion, which draws into RDRAM."""
WIDTH = 320
SECONDS = 15


def white_pixels(emu, fb, y0, y1):
    n = 0
    for y in range(y0, y1):
        row = emu.read(fb + y * WIDTH * 2, WIDTH * 2)
        for i in range(0, len(row), 2):
            p = row[i] << 8 | row[i + 1]
            if (p >> 11) >= 30 and (p >> 6 & 31) >= 30 and (p >> 1 & 31) >= 30:
                n += 1
    return n


def check(emu, sym, args):
    if emu.r32(sym("osMemSize")) != 0x400000:
        emu.fail(f"osMemSize is {emu.r32(sym('osMemSize')):#x}; the emulator still has the Expansion Pak")
    if emu.r32(sym("frameCount")) != 0 or emu.r32(sym("dispFrameCount")) != 0:
        emu.fail("the game's logic or graphics thread ran")
    if args.gfx == "angrylion":
        emu.fb_png("nopak", sym("cfb_16_a"))
        counts = [white_pixels(emu, sym(fb), 84, 100) for fb in ("cfb_16_a", "cfb_16_b")]
        print("white pixels in the message's first line, in the two frame buffers:", counts)
        if min(counts) < 300:
            emu.fail("the message is not in the frame buffers")
