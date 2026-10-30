"""The Game menu's two switches in Atlantis 1, through the menu:

1. "Controller 2: L marks all done". With the debug controls on and this switch off, L held on controller 2 does not
   reach pad[4], and the save slot stays as it is. With the switch on, L reaches pad[4] and the team's debug code
   marks every level and garib of the save slot as done. The test puts the slot back before it ends and never lets
   the game save: the game saves only when a level ends, and the emulator starts every run with an empty EEPROM.
2. "Fog". The draw thread draws the level without its fog while the switch is on: the frame's display lists have no
   fog colour command and no command that turns the fog geometry mode on, and the level's own fog setting is still
   in place afterwards. With the switch off again the fog is back.

The emulator has no controller in port 2, so libultra leaves controllerdata[1] as the test wrote it. The practice
input layer reads it as it would read a real controller."""
import struct

import layout
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

CONT_L = 0x0020
CONT_A = 0x8000
PAD_SIZE = 0x190
PAD_BUTTON = 0x1A  # Pad.button[14]
PAD_PRESSED = 0x36  # Pad.pressed[14]
BUTTON_A, BUTTON_L = 0, 4  # indices in pad[].button
SAVE_SLOT_SIZE = 0x1C
SLOT_LEVELS, SLOT_GARIBS = 0x04, 0x08
INPUT_HELD2 = 0x06  # Input.held2 (practice/include/input.h)
RUN_FOG_OFF = 0x32  # Run.fogOff (practice/include/actions.h)
PRACTICE_FOG_FRAMES = 0x10  # Practice.fogFrames
FOG_ON = 3  # Fog.on
DYNAMIC_SIZE = 0x2B230
TASK_DATA_PTR, TASK_DATA_SIZE = 0x40, 0x44
G_SETGEOMETRYMODE, G_SETFOGCOLOR, G_FOG = 0xB7, 0xF8, 0x00010000


def fog_commands(emu, sym):
    """(fog colour commands, commands that turn on the fog mode) in the tasks of the last built frame, in both
    buffers: from task 1 to the game's currentTask."""
    colours = modes = 0
    for buf in range(2):
        base = sym("dynamic") + buf * DYNAMIC_SIZE
        for i in range(1, emu.r8(sym("currentTask"))):
            task = emu.r32(sym("tlistPointers") + 4 * (10 * buf + i))
            ptr, size = emu.r32(task + TASK_DATA_PTR), emu.r32(task + TASK_DATA_SIZE)
            if not base <= ptr < base + DYNAMIC_SIZE or size > 20000 or ptr % 8:
                continue
            # RDRAM is kept in host-order words: a command is two little-endian words here.
            for w0, w1 in struct.iter_unpack("<II", bytes(emu.raw(ptr, size & ~7))):
                if w0 >> 24 == G_SETFOGCOLOR:
                    colours += 1
                elif w0 >> 24 == G_SETGEOMETRYMODE and w1 & G_FOG:
                    modes += 1
    return colours, modes


def sample_fog(emu, sym, count=16):
    """Samples the fog every second video frame. Returns the most fog colour commands and fog mode commands seen in a
    sample, and the values of fog.on."""
    most = [0, 0]
    seen = set()
    for _ in range(count):
        colours, modes = fog_commands(emu, sym)
        most = [max(most[0], colours), max(most[1], modes)]
        seen.add(emu.r8(sym("fog") + FOG_ON))
        yield 2
    return most, seen


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = sym("frameCount")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    settings = sym("settings")
    run_ = sym("run")
    pad4 = sym("pad") + 4 * PAD_SIZE

    yield from wait_for(emu, lambda: level() == trace.PRESENT and emu.r32(fc) >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and emu.r32(fc) >= 30, 3000,
                        "the title flythrough")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and emu.r32(fc) >= 100, 3000,
                        "Atlantis 1")
    if emu.r8(sym("fog") + FOG_ON) != 1:
        emu.fail("Atlantis 1 has no fog; the test needs a level with fog")

    def game_menu(*path):
        """Opens the menu, chooses the items of PATH one after the other (A on each), and closes the menu."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from goto(emu, sym, *path)
        yield from press(emu, *C_ALL)

    # The fog is on in the retail game: the lists hold the fog colour and the fog mode.
    yield 30
    (colours, modes), seen = yield from sample_fog(emu, sym)
    if colours == 0 or modes == 0:
        emu.fail(f"with the switch off, the frames hold {colours} fog colour and {modes} fog mode commands")
    if seen != {1}:
        emu.fail(f"fog.on is {sorted(seen)} with the switch off, not 1")
    if emu.r8(run_ + RUN_FOG_OFF) or emu.r32(sym("practice") + PRACTICE_FOG_FRAMES):
        emu.fail("the fog switch is on, or has drawn a frame, before it was chosen")

    # 1. Game, Controller 2, "Controller 2: debug controls": on.
    yield from game_menu("Game", "Controller 2", "Controller 2: debug controls")
    if not emu.r8(settings + layout.SETTINGS_DEBUG_PAD):
        emu.fail("the debug controls did not turn on")
    if emu.r8(settings + layout.SETTINGS_DEBUG_PAD_L):
        emu.fail("the L switch was on before it was chosen")
    slot = sym("saveSlot") + SAVE_SLOT_SIZE * emu.r16(sym("currentSaveSlot"))
    levels, garibs = emu.r32(slot + SLOT_LEVELS), emu.r32(slot + SLOT_GARIBS)
    if levels == 0xFFFFFFFF or garibs == 0xFFFFFFFF:
        emu.fail(f"the save slot already holds every level ({levels:#x}) and garib ({garibs:#x})")

    # 2. L with the switch off: it reaches the practice code, not pad[4]. A does, which shows that pad[4] is filled.
    for _ in range(40):
        emu.w16(sym("controllerdata") + 6, CONT_L | CONT_A)
        yield 1
    held2 = emu.r16(sym("input") + INPUT_HELD2)
    if held2 != CONT_L | CONT_A:
        emu.fail(f"the input layer reads {held2:#06x} on controller 2, not L and A")
    if not emu.r8(pad4 + PAD_BUTTON + BUTTON_A):
        emu.fail("pad[4] does not follow controller 2")
    if emu.r8(pad4 + PAD_BUTTON + BUTTON_L):
        emu.fail("L on controller 2 reached pad[4] with the switch off")
    if emu.r32(slot + SLOT_LEVELS) != levels or emu.r32(slot + SLOT_GARIBS) != garibs:
        emu.fail("the save slot changed with the L switch off")
    emu.w16(sym("controllerdata") + 6, 0)
    yield 30

    # 3. The switch on (the next item): L reaches pad[4] and the debug code marks everything as done.
    yield from game_menu("Game", "Controller 2", "Controller 2: L marks all done")
    if not emu.r8(settings + layout.SETTINGS_DEBUG_PAD_L):
        emu.fail("the L switch did not turn on")
    for _ in range(40):
        emu.w16(sym("controllerdata") + 6, CONT_L | CONT_A)
        yield 1
    if not emu.r8(pad4 + PAD_BUTTON + BUTTON_L):
        emu.fail("L on controller 2 did not reach pad[4] with the switch on")
    if emu.r32(slot + SLOT_LEVELS) != 0xFFFFFFFF or emu.r32(slot + SLOT_GARIBS) != 0xFFFFFFFF:
        emu.fail(f"the save slot holds levels {emu.r32(slot + SLOT_LEVELS):#x} and garibs "
                 f"{emu.r32(slot + SLOT_GARIBS):#x}, not every one")
    # Put the slot back, and stop before the game can save it.
    emu.w32(slot + SLOT_LEVELS, levels)
    emu.w32(slot + SLOT_GARIBS, garibs)
    emu.w16(sym("controllerdata") + 6, 0)
    yield 30
    if emu.r32(slot + SLOT_LEVELS) != levels:
        emu.fail("the save slot changed after L was released")
    # Switch it off again. The settings are then as they were.
    yield from game_menu("Game", "Controller 2", "Controller 2: L marks all done")
    if emu.r8(settings + layout.SETTINGS_DEBUG_PAD_L):
        emu.fail("the L switch did not turn off")

    # 4. Fog: off. The frames hold no fog commands, and the level keeps its fog setting.
    yield from game_menu("Game", "Fog")
    if not emu.r8(run_ + RUN_FOG_OFF):
        emu.fail("the fog switch did not turn on")
    yield 30
    frames = emu.r32(sym("practice") + PRACTICE_FOG_FRAMES)
    (colours, modes), seen = yield from sample_fog(emu, sym)
    if colours or modes:
        emu.fail(f"with the fog off, the frames hold {colours} fog colour and {modes} fog mode commands")
    if 1 not in seen or not seen <= {0, 1}:
        emu.fail(f"fog.on is {sorted(seen)} with the fog switch on: the level's setting must stay 1 between frames")
    if emu.r32(sym("practice") + PRACTICE_FOG_FRAMES) <= frames:
        emu.fail("the draw thread did not count frames drawn without fog")

    # 5. Back on: the fog comes back.
    yield from game_menu("Game", "Fog")
    yield 30
    frames = emu.r32(sym("practice") + PRACTICE_FOG_FRAMES)
    (colours, modes), seen = yield from sample_fog(emu, sym)
    if colours == 0 or modes == 0:
        emu.fail(f"with the fog back on, the frames hold {colours} fog colour and {modes} fog mode commands")
    if seen != {1}:
        emu.fail(f"fog.on is {sorted(seen)} with the fog switch off again, not 1")
    if emu.r32(sym("practice") + PRACTICE_FOG_FRAMES) != frames:
        emu.fail("the draw thread went on turning the fog off")
