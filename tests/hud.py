"""The HUD: every item on, in Atlantis 1. The timer must be in the frame buffer at its place, and must count.
Writes hud.png (needs --gfx angrylion)."""
import layout
import trace

WIDTH = 320


def white_pixels(emu, fb, x0, y0, w, h):
    n = 0
    for y in range(y0, y0 + h):
        row = emu.read(fb + (y * WIDTH + x0) * 2, w * 2)
        for i in range(0, len(row), 2):
            p = row[i] << 8 | row[i + 1]
            if (p >> 11) >= 30 and (p >> 6 & 31) >= 30 and (p >> 1 & 31) >= 30:
                n += 1
    return n


def run(emu, sym, args):
    gi = sym("gameInfo")
    settings = sym("settings")
    for i in range(layout.HUD_COUNT):
        emu.w8(settings + layout.SETTINGS_HUD + layout.HUD_PLACE_SIZE * i, 1)
    step, wait = "logos", 0
    while True:
        mode, level, fc = emu.r8(gi), emu.r8(gi + 1), emu.r32(sym("frameCount"))
        wait += 1
        if step == "logos" and level == trace.PRESENT and mode == 4 and fc >= 200:
            step, wait = "start", 0
        elif step == "start":
            emu.hold("START") if wait <= 6 else emu.release()
            if level == trace.FLYTHRU and mode == 4 and fc >= 60:
                emu.w8(gi, 2)
                step, wait = "select", 0
        elif step == "select":
            if wait == 200:
                emu.w32(sym("selectedLevel"), trace.ATLANTIS1)
            elif wait == 400:
                emu.hold("START")
            elif wait == 440:
                emu.release()
            elif wait > 440 and level == trace.ATLANTIS1 and mode == 4 and fc >= 150:
                break
        yield 1
    emu.hold("A", "Z", x=40, y=70)
    yield 10
    held = emu.r16(sym("input"))
    stick = (emu.r8(sym("input") + 4), emu.r8(sym("input") + 5))
    if held & 0xA000 != 0xA000 or stick != (40, 70):
        emu.fail(f"the input layer reads buttons {held:#06x} and stick {stick}, not A, Z and (40, 70)")
    frames = emu.r32(sym("player") + 0x18)
    if frames == 0:
        emu.fail("the in-game timer does not count")
    if args.gfx == "angrylion":
        x, y = emu.r8(settings + layout.SETTINGS_HUD + 1) * 2, emu.r8(settings + layout.SETTINGS_HUD + 2) * 2
        counts = [white_pixels(emu, sym(fb), x, y, 100, 8) for fb in ("cfb_16_a", "cfb_16_b")]
        print("white timer pixels in the two frame buffers:", counts)
        if max(counts) < 40:
            emu.fail("the timer is not in the frame buffer")
        emu.fb_png("hud", sym("cfb_16_a"))
        emu.fb_png("hud1", sym("cfb_16_b"))
