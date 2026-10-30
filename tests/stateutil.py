"""Shared by slots.py and instantreset.py: the check of a saved state against the game's memory."""
import bisect


class Verify:
    """With stateVerify set, each save is decoded again and compared with the game's memory (practice/src/savestate.c).
    The audio thread writes to its buffers while that runs, and a few words there may differ. The ROM counts the words
    that differ outside the audio code's buffers (VERIFY_VOLATILE_START and END): there must be none."""

    def __init__(self, emu, sym):
        self.emu, self.sym = emu, sym
        self.seen = 0
        self.seen_outside = 0
        addrs = [a for a, _ in sym.labels]
        self.name = lambda a: sym.labels[bisect.bisect_right(addrs, a) - 1][1]  # noqa: E731

    def check(self, what):
        """Fails if the last save decodes to something other than the game's memory."""
        bad = self.emu.r32(self.sym("stateVerifyBad"))
        outside = self.emu.r32(self.sym("stateVerifyOutside"))
        new, self.seen = bad - self.seen, bad
        new_outside, self.seen_outside = outside - self.seen_outside, outside
        if new == 0:
            return
        where = [self.emu.r32(self.sym("stateVerifyList") + 4 * i) for i in range(8)]
        names = sorted({self.name(a) for a in where if a})
        print(f"verify: {what}: {new} words differ ({new_outside} outside the audio buffers), in {names}")
        if new_outside:
            self.emu.fail(f"{what}: the saved state decodes to something else ({new_outside} words outside the audio "
                          f"buffers, in {names})")


def hud_message(emu, sym):
    """The text that the HUD shows now or showed last (hudMessageText in practice/src/hud.c)."""
    pointer = emu.r32(sym("hudMessageText"))
    if pointer == 0:
        return ""
    data = emu.read(pointer, 64)
    return data.split(b"\0")[0].decode("ascii", "replace")
