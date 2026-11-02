"""The practice menu: all four C buttons open it on the title menu, the game's logic stops while it is open, the
game sees none of the menu's input, and B closes it. Needs --gfx angrylion for the frame buffer check."""
C_ALL = ("CUP", "CDOWN", "CLEFT", "CRIGHT")
WIDTH = 320


def yellow_pixels(emu, fb, x0, y0, w, h):
    n = 0
    for y in range(y0, y0 + h):
        row = emu.read(fb + (y * WIDTH + x0) * 2, w * 2)
        for i in range(0, len(row), 2):
            p = row[i] << 8 | row[i + 1]
            if (p >> 11) >= 28 and (p >> 6 & 31) >= 22 and (p >> 1 & 31) <= 18:
                n += 1
    return n


def run(emu, sym, args):
    menu_open = sym("menuOpen")
    count = sym("frameCount2")
    pad_buttons = sym("pad") + 0x1A  # pad[0].button[14]
    # The title flythrough is a demo playback, which replaces the controller's data; Start leaves it for the
    # title menu, as in trace.py.
    yield 2400
    emu.hold("START")
    yield 6
    emu.release()
    yield 600
    emu.hold(*C_ALL)
    yield 12
    if emu.r8(menu_open) != 1:
        emu.fail("all four C buttons did not open the menu")
    held = emu.read(pad_buttons, 14)
    if any(held):
        emu.fail(f"the game saw buttons while the menu opened: {list(held)}")
    emu.release()
    before = emu.r32(count)
    yield 60
    if emu.r32(count) != before:
        emu.fail(f"the game's frames went on while the menu was open ({before} to {emu.r32(count)})")
    if args.gfx == "angrylion":
        counts = [yellow_pixels(emu, sym(fb), 38, 31, 100, 8) for fb in ("cfb_16_a", "cfb_16_b")]
        print("menu title pixels in the two frame buffers:", counts)
        if max(counts) < 50:
            emu.fail("the menu is not in the frame buffer")
        emu.fb_png("menu", sym("cfb_16_a"))
    emu.hold("DOWN")
    yield 8
    emu.release()
    yield 8
    emu.hold("B")
    yield 8
    if emu.r8(menu_open) != 0:
        emu.fail("B did not close the menu")
    if emu.read(pad_buttons, 14)[1]:
        emu.fail("the game saw the B that closed the menu")
    emu.release()
    yield 30
    if emu.r32(count) == before:
        emu.fail("the game's frames did not go on after the menu closed")
