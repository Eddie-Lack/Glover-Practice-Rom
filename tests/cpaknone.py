"""Controller Pak storage with no pak in the controller. Until the view is used there is no serial interface traffic
of the pak code (cpak.commands and cpak.transfers stay 0 through the boot, the title and the open menu). Then the
check and a save both say "no Controller Pak". Each sends one status command and moves no data block."""
import layout
from cpakutil import cursor_to, open_view, press, status, wait_idle

PAKS = ""


def run(emu, sym, args):
    c = sym("cpak")
    yield 600
    if emu.r32(c + layout.CPAK_TRANSFERS) or emu.r32(c + layout.CPAK_COMMANDS) or emu.r8(c + layout.CPAK_WORKING):
        emu.fail("the pak code moved data before anyone asked")
    yield from open_view(emu, sym)
    # open_view opens the view, which asks for a check: the status read finds no pak.
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NO_PAK:
        emu.fail(f"the check without a pak gave status {status(emu, sym)}, not {layout.CPAK_NO_PAK}")
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NO_PAK:
        emu.fail(f"a save without a pak gave status {status(emu, sym)}")
    if emu.r32(c + layout.CPAK_TRANSFERS) != 0:
        emu.fail(f"{emu.r32(c + layout.CPAK_TRANSFERS)} blocks moved with no pak")
    if emu.r32(c + layout.CPAK_COMMANDS) != 2:
        emu.fail(f"{emu.r32(c + layout.CPAK_COMMANDS)} commands for the check and the save, not 2 status commands")
    emu.fb_png("cpaknone", sym("cfb_16_a")) if args.gfx == "angrylion" else None
