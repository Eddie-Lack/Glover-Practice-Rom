#!/usr/bin/env python3
"""Runs every test: the ROM layout test, the boot test, the test without the Expansion Pak, the menu, the in-game
timer fix, the HUD, the controller display, the tools (warp, reset, frame advance, checkpoints, cheats), the settings save and load,
savestates (the compressor, one level, ten slots, the instant reset), input recordings, the Game menu's tools and switches, the full-game timer, the Inspect menu, the
collision view, the frame log, the slap's Z cancel, noclip and moon jump, Set action, Spawn and move actors, the taps
that the game missed, chaos inputs, memory corruption, the early title menu, the free camera, the ghost, the practice best times, the save file editor, the Controller Pak storage, the trick trainers (the L mash and MBZ-L, the line keeper, the Moon Pound aim, the boss windows, the wall clip), the hole painter, the savestates, a recording and the best times on the SD card (the FAT code on the host, then the ROM with a test mailbox for a cart), and the faithfulness test.

    run.py [--quick]

--quick skips the faithfulness test, which takes about seven minutes. The emulator must be set up first
(gloveworld/tools/emu/setup.sh), with a display on :99.
"""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = [
    ["romlayout.py"],
    ["harness.py", "boot", "--gfx", "angrylion"],
    ["harness.py", "nopak", "--no-pak", "--gfx", "angrylion"],
    ["harness.py", "menu", "--gfx", "angrylion"],
    ["harness.py", "timerfix", "--gfx", "angrylion"],
    ["harness.py", "hud", "--gfx", "angrylion"],
    ["harness.py", "tools", "--gfx", "angrylion"],
    ["harness.py", "settings", "--gfx", "angrylion"],
    ["harness.py", "settingsload", "--save-from", "build/tests/settings/save"],
    ["harness.py", "settingsold", "--save-from", "build/tests/settings/save"],
    ["harness.py", "menupages", "--gfx", "angrylion"],
    ["lzhost.py"],
    ["fathost.py"],
    ["releasehost.py"],
    ["harness.py", "savestate", "--gfx", "angrylion"],
    ["harness.py", "slots", "--gfx", "angrylion"],
    ["harness.py", "instantreset", "--gfx", "angrylion"],
    ["harness.py", "recording", "--gfx", "angrylion"],
    ["harness.py", "gametools", "--gfx", "angrylion"],
    ["harness.py", "warpgive", "--gfx", "angrylion"],
    ["harness.py", "gameswitches", "--gfx", "angrylion"],
    ["harness.py", "paddisplay", "--gfx", "angrylion"],
    ["harness.py", "gametimer"],
    ["harness.py", "inspectview", "--gfx", "angrylion"],
    ["harness.py", "collview", "--gfx", "angrylion"],
    ["harness.py", "collview2", "--gfx", "angrylion"],
    ["harness.py", "framelog", "--gfx", "angrylion"],
    ["harness.py", "slapcancel"],
    ["harness.py", "noclip", "--gfx", "angrylion"],
    ["harness.py", "setaction", "--gfx", "angrylion"],
    ["harness.py", "actors", "--gfx", "angrylion"],
    ["harness.py", "actoredit", "--gfx", "angrylion"],
    ["harness.py", "missedtaps", "--gfx", "angrylion"],
    ["harness.py", "chaos", "--gfx", "angrylion"],
    ["harness.py", "corrupt", "--gfx", "angrylion"],
    ["harness.py", "titlemenu", "--gfx", "angrylion"],
    ["harness.py", "demos", "--gfx", "angrylion"],
    ["harness.py", "freecam", "--gfx", "angrylion"],
    ["harness.py", "ghost", "--gfx", "angrylion"],
    ["harness.py", "besttimes", "--gfx", "angrylion"],
    ["harness.py", "saveedit", "--gfx", "angrylion"],
    ["harness.py", "saveeditload", "--save-from", "build/tests/saveedit/save"],
    ["harness.py", "saveeditbad", "--save-from", "build/tests/saveedit/save_bad"],
    ["harness.py", "trainers", "--gfx", "angrylion"],
    ["harness.py", "trainers2", "--gfx", "angrylion"],
    ["harness.py", "trainers3", "--gfx", "angrylion"],
    ["harness.py", "trainerlog", "--gfx", "angrylion"],
    ["harness.py", "camspeed", "--gfx", "angrylion"],
    ["harness.py", "collholes", "--gfx", "angrylion"],
    ["harness.py", "trainers_moon", "--gfx", "angrylion"],
    ["harness.py", "trainers_moon_cost", "--gfx", "angrylion"],
    ["harness.py", "bosstrainer", "--gfx", "angrylion"],
    ["harness.py", "cpak", "--gfx", "angrylion"],
    ["harness.py", "cpakload", "--save-from", "build/tests/cpak/save"],
    ["harness.py", "cpakforeign", "--gfx", "angrylion"],
    ["harness.py", "cpakedge", "--gfx", "angrylion"],
    ["harness.py", "cpakfull", "--gfx", "angrylion"],
    ["harness.py", "cpakbad", "--gfx", "angrylion"],
    ["harness.py", "cpaknone", "--gfx", "angrylion"],
    ["harness.py", "cpakid", "--gfx", "angrylion"],
    ["harness.py", "cpakindex", "--gfx", "angrylion"],
    ["harness.py", "cpakcross", "--gfx", "angrylion"],
    ["harness.py", "cpakdupe", "--gfx", "angrylion"],
    ["harness.py", "cpakrumble", "--gfx", "angrylion"],
    ["harness.py", "cpakfail", "--gfx", "angrylion"],
    ["harness.py", "cpakfailfull", "--gfx", "angrylion"],
    ["harness.py", "sdcard", "--gfx", "angrylion"],
    ["harness.py", "sdcardload", "--gfx", "angrylion"],
    ["harness.py", "sdcardfull", "--gfx", "angrylion"],
    ["harness.py", "sdcardbig", "--gfx", "angrylion"],
    ["harness.py", "sdrecording", "--gfx", "angrylion"],
    ["harness.py", "sdrecordingload", "--gfx", "angrylion"],
    ["faithful.py", "--with-features"],
]


def main():
    quick = "--quick" in sys.argv[1:]
    failed = 0
    for test in TESTS:
        if quick and test[0] == "faithful.py":
            continue
        proc = subprocess.run([sys.executable, os.path.join(HERE, test[0])] + test[1:], capture_output=True, text=True)
        lines = [l for l in proc.stdout.splitlines() if l.startswith(("PASS", "FAIL", "loads"))]
        print("\n".join(lines) if lines else f"FAIL {' '.join(test)}: exit code {proc.returncode}\n{proc.stderr[-2000:]}")
        failed += proc.returncode != 0
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
