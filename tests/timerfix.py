"""The in-game timer fix (docs/game.md, "The in-game timer"). A level with a bonus timer leaves timer321 at 0. The
test writes 0 there, as such a level does, then starts Atlantis 1 from the level select and finds the game
frame on which the in-game timer starts. It must be frame 81, as after a fresh boot. (Retail, run with
--rom baserom.us.z64 --elf gloveworld/build/NGVE.elf, starts it on frame 1 and fails.) With --frames 0 the test
does not write timer321 (the normal case)."""
import trace


def run(emu, sym, args):
    gi = sym("gameInfo")
    level_frames = sym("player") + 0x18
    frame_count = sym("frameCount")
    step, wait = "logos", 0
    for f in range(1, 8000):
        mode, level, fc = emu.r8(gi), emu.r8(gi + 1), emu.r32(frame_count)
        wait += 1
        # The same way to the level select as trace.py.
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
                if args.frames != 0:
                    emu.w16(sym("timer321"), 0)
            elif wait == 400:
                emu.hold("START")
            elif wait == 440:
                emu.release()
                step = "level"
        if step == "level" and mode == 4 and level == trace.ATLANTIS1 and emu.r32(level_frames) > 0:
            start = emu.r32(frame_count) - emu.r32(level_frames) + 1
            print(f"the in-game timer started on game frame {start}")
            if start != 81:
                emu.fail(f"the in-game timer started on game frame {start}, not 81")
            return
        yield 1
    emu.fail("the in-game timer never started")
