"""Every page of the practice menu fits on the screen, and the menu look settings work.

1. Every menu and sub menu is opened by the menu's own input (the walk goes through each ITEM_SUBMENU). On each page
   the frame buffer must show the panel inside the screen (at least 12 pixels clear of the bottom), every row that the
   page lists (at most 14), and the hint line. A page with more than 14 rows scrolls: with the cursor moved to the last
   item, the last 14 rows must show.
2. Menu opacity: with each of its six steps the panel is nearer to its solid colour than with the one before, and
   "solid" is exactly it.
3. Menu position: at each of the nine places the panel's four edges are where the settings put them, and the tallest
   page (14 rows) ends inside the screen at the lowest place.
Needs --gfx angrylion. The panel is made solid for 1 and 3 so that its colour is known."""
import layout
import menunav
import trace
from menunav import Page, goto, to_item
from tools import C_ALL, press

W, H = 320, 240
MENU_OPACITY_STEPS = 6
MENU_W = 256
ROW_H = 11
XS = (12, 32, 52)
YS = (12, 28, 36)
PANEL = (2, 3, 6)  # 0x101830 in 5 bits
BAR = (6, 9, 20)   # 0x3048A0


def panel_geometry(emu, sym, count):
    s = sym("settings")
    mx = XS[emu.r8(s + layout.SETTINGS_MENU_ACROSS) - 1]
    my = YS[emu.r8(s + layout.SETTINGS_MENU_DOWN) - 1]
    rows = min(count, menunav.MENU_ROWS)
    return mx, my, rows, (rows + 3) * ROW_H + 2


class Shot:
    """Both frame buffers, whichever is shown."""

    def __init__(self, emu, sym):
        self.bufs = [emu.read(sym(n), W * H * 2) for n in ("cfb0", "cfb1")]
        self.best = 0

    def px(self, b, x, y):
        i = (y * W + x) * 2
        p = self.bufs[b][i] << 8 | self.bufs[b][i + 1]
        return p >> 11 & 31, p >> 6 & 31, p >> 1 & 31

    def is_(self, b, x, y, color, tol=1):
        return all(abs(c - k) <= tol for c, k in zip(self.px(b, x, y), color))

    def choose(self, mx, my):
        """The buffer that has the panel's top left pixel."""
        scores = [sum(self.is_(b, mx + 100 + i, my + 1, BAR, 2) for i in range(10)) for b in (0, 1)]
        self.best = scores.index(max(scores))
        return max(scores)

    def bright(self, x0, y0, x1, y1):
        b = self.best
        return sum(1 for y in range(y0, y1) for x in range(x0, x1)
                   if (lambda p: p[0] >= 14 and p[1] >= 14)(self.px(b, x, y)))


def check_page(emu, sym, name, log):
    page = Page(emu, sym)
    mx, my, rows, h = panel_geometry(emu, sym, page.count)
    if my + h > H - 12:
        emu.fail(f"{name}: the panel ends at {my + h}, less than 12 pixels from the bottom")
    shot = Shot(emu, sym)
    if shot.choose(mx, my) < 8:
        emu.fail(f"{name}: no panel at {mx}, {my} in the frame buffers")
    b = shot.best
    first = page.scroll
    for r in range(rows):
        y = my + (r + 1) * ROW_H + 8
        if shot.bright(mx + 8, y, mx + MENU_W - 8, y + 7) < 6:
            emu.fail(f"{name}: row {first + r} ({page.items[first + r][0]!r}) is not drawn")
    if shot.bright(mx + 8, my + h - ROW_H, mx + MENU_W - 8, my + h - 3) < 6:
        emu.fail(f"{name}: the hint line is not drawn")
    for x in (mx + 5, mx + 120, mx + MENU_W - 6):
        if not shot.is_(b, x, my + h - 1, PANEL, 2):
            emu.fail(f"{name}: the panel does not reach its bottom edge at {x}, {my + h - 1}: {shot.px(b, x, my + h - 1)}")
    log.append((name, page.count, rows))


def walk(emu, sym, path, log):
    """Checks this page and, depth first, every sub menu."""
    yield 4
    page = Page(emu, sym)
    name = " > ".join(path + [page.title])
    check_page(emu, sym, name, log)
    if page.count > menunav.MENU_ROWS:
        yield from press(emu, "UP")  # wraps to the last item: the page scrolls to the end
        yield 4
        p2 = Page(emu, sym)
        if p2.cursor != page.count - 1 or p2.scroll != page.count - menunav.MENU_ROWS:
            emu.fail(f"{name}: UP from the top gave cursor {p2.cursor}, scroll {p2.scroll}")
        check_page(emu, sym, name + " (end)", log)
        yield from press(emu, "DOWN")
    for i, (label, kind, sub, _) in enumerate(page.items):
        if kind == menunav.ITEM_SUBMENU:
            yield from to_item(emu, sym, i)
            yield from press(emu, "A")
            if Page(emu, sym).depth != page.depth + 1:
                emu.fail(f"{name}: A on {label!r} did not open a page")
            yield from walk(emu, sym, path + [page.title], log)
            yield from press(emu, "B")


def sample(shot, mx, my, h):
    """The mean distance of the panel's bottom strip (two blank rows) from the solid colour."""
    b = shot.best
    total = n = 0
    for y in (my + h - 2, my + h - 1):
        for x in range(mx + 4, mx + MENU_W - 4, 2):
            total += sum(abs(c - k) for c, k in zip(shot.px(b, x, y), PANEL))
            n += 1
    return total / n


def run(emu, sym, args):
    gi = sym("gameInfo")
    settings = sym("settings")
    while not (emu.r8(gi + 1) == trace.PRESENT and emu.r32(sym("frameCount")) >= 200):
        yield 1
    yield from press(emu, "START", frames=6)
    yield 600
    yield from press(emu, *C_ALL)
    if not emu.r8(sym("menuOpen")):
        emu.fail("the menu did not open")
    emu.w8(settings + layout.SETTINGS_MENU_OPACITY, 6)  # solid
    yield 6

    # 1. Every page.
    log = []
    yield from walk(emu, sym, [], log)
    for name, count, rows in log:
        print(f"page {name}: {count} rows listed, {rows} drawn")
    print(len(log), "pages checked")
    if len(log) < 20:
        emu.fail(f"only {len(log)} pages were checked")

    # 2. Menu opacity: Settings, Menu opacity. From 85% (step 5): solid, then around to clear and up.
    yield from goto(emu, sym, "Settings")
    emu.w8(settings + layout.SETTINGS_MENU_OPACITY, 5)
    yield from to_item(emu, sym, "Menu opacity")
    dist = {}
    # The menu is drawn into one buffer only while the game waits: the one that has the solid panel now.
    page = Page(emu, sym)
    mx, my, rows, h = panel_geometry(emu, sym, page.count)
    shot = Shot(emu, sym)
    shown = min((0, 1), key=lambda b: sample(setb(shot, b), mx, my, h))
    for _ in range(MENU_OPACITY_STEPS):
        yield from press(emu, "RIGHT")
        yield 4
        step = emu.r8(settings + layout.SETTINGS_MENU_OPACITY)
        page = Page(emu, sym)
        mx, my, rows, h = panel_geometry(emu, sym, page.count)
        dist[step] = sample(setb(Shot(emu, sym), shown), mx, my, h)
    print("distance from the solid colour by step:", {k: round(v, 2) for k, v in sorted(dist.items())})
    order = [dist[k] for k in sorted(dist)]
    if sorted(dist) != list(range(1, MENU_OPACITY_STEPS + 1)):
        emu.fail(f"the opacity steps visited were {sorted(dist)}")
    if dist[6] > 0.5:
        emu.fail(f"solid is {dist[6]:.2f} from the panel colour")
    if any(a < b - 0.3 for a, b in zip(order, order[1:])):
        emu.fail(f"the panel does not get more solid with each step: {order}")
    if order[0] < order[-1] + 2:
        emu.fail(f"clear and solid look alike: {order}")
    if len({round(v, 1) for v in order}) < 4:
        emu.fail(f"fewer than four different looks in six steps: {order}")
    emu.w8(settings + layout.SETTINGS_MENU_OPACITY, 6)
    yield 6

    # 3. Menu position: the nine places, on the Settings page.
    for down in (1, 2, 3):
        for across in (1, 2, 3):
            for item, addr, want in (("Menu position across", layout.SETTINGS_MENU_ACROSS, across),
                                     ("Menu position down", layout.SETTINGS_MENU_DOWN, down)):
                yield from to_item(emu, sym, item)
                for _ in range(3):
                    if emu.r8(settings + addr) == want:
                        break
                    yield from press(emu, "RIGHT")
                if emu.r8(settings + addr) != want:
                    emu.fail(f"{item} did not reach {want}")
            yield 6
            page = Page(emu, sym)
            mx, my, rows, h = panel_geometry(emu, sym, page.count)
            check_page(emu, sym, f"Settings at {across}, {down}", [])
            shot = Shot(emu, sym)
            shot.choose(mx, my)
            b = shot.best
            ym = my + h // 2
            for x, inside in ((mx - 2, False), (mx, True), (mx + MENU_W - 1, True), (mx + MENU_W + 1, False)):
                if shot.is_(b, x, ym, PANEL, 1) != inside:
                    emu.fail(f"place {across}, {down}: x {x} at y {ym} is {'not ' if inside else ''}the panel")
            if not shot.is_(b, mx + 120, my, BAR, 2) or shot.is_(b, mx + 120, my - 2, BAR, 2):
                emu.fail(f"place {across}, {down}: the title bar does not start at y {my}")
    # The tallest pages at the lowest place (right, low is the last place visited).
    yield from press(emu, "B")
    for name, rows in (("Warp", 17), ("Game", menunav.MENU_ROWS)):
        yield from goto(emu, sym, name)
        yield 4
        if Page(emu, sym).count != rows:
            emu.fail(f"{name} should have {rows} rows")
        check_page(emu, sym, name + " at the lowest place", [])
        yield from press(emu, "B")
    emu.fb_png("menupages_low", sym("cfb0"))


def setb(shot, b):
    shot.best = b
    return shot
