"""Moves through the practice menu by item names. It reads the menu's state from RAM (the stack of open menus, the
cursors, the Menu and MenuItem tables of practice/include/menu.h), so a test does not depend on the order of the
items."""
from tools import press

# MenuItem: label 0, kind 4, min 5, max 6, arg 7, value / submenu / view 8, action 12, names / info 16, hint 20;
# 24 bytes. Menu: title 0, items 4, count 8, repeat 9. A repeating menu (the splits page) has one item, whose value
# points to the live row count; its row N is labelled "N".
ITEM_SIZE = 24
(ITEM_SUBMENU, ITEM_TOGGLE, ITEM_CHOICE, ITEM_NUMBER, ITEM_ACTION, ITEM_INFO, ITEM_BIND, ITEM_VIEW,
 ITEM_QUICK) = range(9)
MENU_ROWS = 14


def cstr(emu, addr):
    out = bytearray()
    while len(out) < 80:
        c = emu.read(addr + len(out), 1)[0]
        if c == 0:
            break
        out.append(c)
    return out.decode("latin-1")


class Page:
    """The page that is open now."""

    def __init__(self, emu, sym):
        self.depth = emu.r32(sym("depth"))
        self.menu = emu.r32(sym("stack") + 4 * self.depth)
        self.cursor = emu.r8(sym("cursor") + self.depth)
        self.scroll = emu.r8(sym("scroll") + self.depth)
        self.title = cstr(emu, emu.r32(self.menu))
        items = emu.r32(self.menu + 4)
        self.count = emu.r8(self.menu + 8)
        repeat = emu.r8(self.menu + 9)
        if repeat:
            self.count = emu.r8(emu.r32(items + 8))
        self.items = []
        for i in range(self.count):
            a = items + (0 if repeat else i * ITEM_SIZE)
            label = str(i + 1) if repeat else cstr(emu, emu.r32(a))
            self.items.append((label, emu.r8(a + 4), emu.r32(a + 8), a))

    def labels(self):
        return [it[0] for it in self.items]

    def index(self, label):
        for i, it in enumerate(self.items):
            if it[0] == label:
                return i
        raise AssertionError(f"no item {label!r} in the menu {self.title!r}: {self.labels()}")


def to_item(emu, sym, label):
    """Moves the cursor to the item LABEL of the open page (a number: the item at that place), by the shorter way."""
    for _ in range(80):
        page = Page(emu, sym)
        want = label if isinstance(label, int) else page.index(label)
        if page.cursor == want:
            return page
        down = (want - page.cursor) % page.count
        yield from press(emu, "DOWN" if down <= page.count - down else "UP")
    emu.fail(f"could not move to {label!r}")


def goto(emu, sym, *labels, enter=True):
    """From the page that is open, moves to each item in turn and presses A on it (not on the last when ENTER is
    false)."""
    for n, label in enumerate(labels):
        yield from to_item(emu, sym, label)
        if enter or n + 1 < len(labels):
            yield from press(emu, "A")


def top(emu, sym):
    """Backs out to the root page."""
    for _ in range(8):
        if Page(emu, sym).depth == 0:
            return
        yield from press(emu, "B")
