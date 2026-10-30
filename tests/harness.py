#!/usr/bin/env python3
"""The emulator test harness: runs a ROM in mupen64plus under script control.

    harness.py TEST [--rom ROM] [--elf ELF] [--no-pak] [--gfx rice|angrylion] [--out DIR] [--frames N]

It uses gloveworld's scripted emulator (gloveworld/tools/emu; run its setup.sh once). TEST names a module in
tests/ that defines run(emu, sym, args), a generator like the scripts of gloveworld's emu.py: each `yield N` lets
N video frames pass. sym(name) is a symbol's address in the ROM's ELF. The practice ROM keeps the retail addresses,
but its own symbols exist only in its ELF. A test reports failure by raising an exception or calling
emu.fail(message).

A test that needs a Controller Pak sets PAKS = "1" (the controllers that have one); RUMBLE = "1" gives a controller a
Rumble Pak instead. The core then keeps the pak in a
.mpk file in OUT/save and formats it when the file is new. A test may also define prepare(mpk, args), which runs
before emulation starts and may write that file. The core writes only controller 1's pak back to the file.

mupen64plus calls the frame callback only when the video plugin shows a new frame. A test of a screen that never
changes defines SECONDS and check(emu, sym, args) instead of run: the harness lets the emulator run for SECONDS of
real time, then calls check from another thread. The result goes to OUT/result.json.
"""
import argparse
import ctypes
import hashlib
import importlib
import json
import os
import shutil
import subprocess
import sys
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
GW = os.path.join(ROOT, "gloveworld")
sys.path.insert(0, os.path.join(GW, "tools", "emu"))
import emu as gwemu  # noqa: E402

# gloveworld's emu.py gives a patched ROM the retail ROM's entry in the core's ini (one player) and copies each
# state with the patched ROM's MD5 before loading it. The tests need the core's defaults (controller 2 and its
# Controller Pak), and they load only states the practice ROM saved itself, often on the frame after the save.
if hasattr(gwemu, "data_dir_for"):
    gwemu.data_dir_for = lambda md5: gwemu.DATADIR
if hasattr(gwemu, "state_for"):
    gwemu.state_for = lambda path, md5: path

PRACTICE_ROM = os.path.join(ROOT, "build", "glover-practice.z64")
PRACTICE_ELF = os.path.join(ROOT, "build", "glover-practice.elf")
# The build with the test mailbox for the SD card (make testrom). A test module with TEST_BUILD = True runs on it.
TEST_ROM = os.path.join(ROOT, "build", "test", "glover-practice-test.z64")
TEST_ELF = os.path.join(ROOT, "build", "test", "glover-practice-test.elf")
RETAIL_ROM = os.path.join(ROOT, "baserom.us.z64")
RETAIL_ELF = os.path.join(GW, "build", "NGVE.elf")

M64TYPE_BOOL = 3
M64TYPE_STRING = 4


class TestFailed(Exception):
    pass


class Emu(gwemu.Emu):
    def __init__(self, rom, gfx, out, expansion=True, verbose=False, save_from=None):
        super().__init__(rom, gfx, out, verbose)
        if hasattr(self.input, "scriptinput_pulse"):
            self.input.scriptinput_pulse.argtypes = [ctypes.c_int, ctypes.c_uint, ctypes.c_int]
            self.input.scriptinput_pulse_left.argtypes = [ctypes.c_int]
        # The core reads these settings when emulation starts, so they can be set after the ROM is open.
        section = ctypes.c_void_p()
        self._check(self.core.ConfigOpenSection(b"Core", ctypes.byref(section)), "ConfigOpenSection")
        value = ctypes.c_int(0 if expansion else 1)
        self._check(self.core.ConfigSetParameter(section, b"DisableExtraMem", M64TYPE_BOOL, ctypes.byref(value)),
                    "DisableExtraMem")
        # The core moves the PI and SI interrupts by a random amount, seeded from the clock. That changes the
        # picture's dither noise from run to run, so every run uses the fixed timing.
        off = ctypes.c_int(0)
        self._check(self.core.ConfigSetParameter(section, b"RandomizeInterrupt", M64TYPE_BOOL, ctypes.byref(off)),
                    "RandomizeInterrupt")
        # Every run starts with an empty EEPROM. The game writes its save while it boots and at the end of each
        # level, so a save left by an earlier run would change the timing.
        # --save-from starts from a copy of another run's save folder instead.
        saves = os.path.join(out, "save")
        shutil.rmtree(saves, ignore_errors=True)
        if save_from:
            shutil.copytree(save_from, saves)
        else:
            os.makedirs(saves)
        self._check(self.core.ConfigSetParameter(section, b"SaveSRAMPath", M64TYPE_STRING,
                                                 ctypes.c_char_p(saves.encode() + b"/")), "SaveSRAMPath")
        # angrylion's worker threads make runs differ from each other; one thread gives the same run every time.
        self._check(self.core.ConfigOpenSection(b"Video-AngrylionPlus", ctypes.byref(section)), "ConfigOpenSection")
        off = ctypes.c_int(0)
        self._check(self.core.ConfigSetParameter(section, b"Parallel", M64TYPE_BOOL, ctypes.byref(off)), "Parallel")

    def attach_ram(self):
        if self._ram is None:
            p = self.core.DebugMemGetPointer(gwemu.DBG_PTR_RDRAM)
            self._ram = (ctypes.c_ubyte * 0x800000).from_address(p)

    def raw(self, a, n):
        """The n bytes of RDRAM from a, without a copy. The core keeps RDRAM in 32-bit words of the host's byte
        order, so the bytes of each word are reversed; this suits a checksum or a comparison."""
        self.attach_ram()
        a &= 0x7FFFFF
        return memoryview(self._ram).cast("B")[a:a + n]

    def stop(self):
        self.core.CoreDoCommand(gwemu.CMD_STOP, 0, None)

    def fb_png(self, name, addr, width=320, height=240):
        """Writes a 16-bit frame buffer in RDRAM to OUT/NAME.png. Only a plugin that draws into RDRAM (angrylion)
        fills it; it works from any thread, unlike shot()."""
        data = self.read(addr, width * height * 2)
        rgb = bytearray()
        for y in reversed(range(height)):  # write_png takes the rows bottom-up, as the core returns them
            for x in range(width):
                p = data[(y * width + x) * 2] << 8 | data[(y * width + x) * 2 + 1]
                rgb += bytes(((p >> 11 & 31) * 255 // 31, (p >> 6 & 31) * 255 // 31, (p >> 1 & 31) * 255 // 31))
        path = os.path.join(self.out, name + ".png")
        gwemu.write_png(path, width, height, bytes(rgb))
        return path

    def pulse(self, *names, polls=1):
        """Presses the buttons for the next POLLS reads of the controller by the game or by the practice code, however
        long they are apart in time. """
        v = 0
        for n in names:
            v |= 1 << gwemu.BUTTON_BITS[n.upper()]
        self.input.scriptinput_pulse(0, v, polls)

    def pulse_left(self):
        return self.input.scriptinput_pulse_left(0)

    def fail(self, message):
        raise TestFailed(message)

    def r16s(self, a):
        v = self.r16(a)
        return v - 0x10000 if v & 0x8000 else v

    def r32s(self, a):
        v = self.r32(a)
        return v - 0x100000000 if v & 0x80000000 else v


# angrylion's dither noise comes from one random sequence that runs on from boot. A frame drawn more or less
# during a load then changes the noise of every later picture, though each picture is drawn the same. The tests'
# copy starts each display list from the same seed: a picture then depends only on its own drawing.
RESEED_AT = "    // don't do anything if the RDP has crashed or the registers are not set up correctly\n"
RESEED = "    state[0].rseed = 3; // Glover practice ROM tests: each list starts from the same noise\n"


def angrylion(folder):
    """Builds angrylion with the reseed above into FOLDER, from the source that gloveworld's setup.sh fetched."""
    library = os.path.join(folder, "mupen64plus-video-angrylion-plus.so")
    stamp = os.path.join(folder, "angrylion.stamp")
    if os.path.exists(library) and os.path.exists(stamp) and open(stamp).read() == RESEED:
        return
    source = os.path.join(ROOT, "build", "tests", "angrylion-src")
    shutil.rmtree(source, ignore_errors=True)
    shutil.copytree(os.path.join(gwemu.BUILD, "angrylion-rdp-plus"), source, ignore=shutil.ignore_patterns(".git"))
    path = os.path.join(source, "src", "core", "n64video.c")
    text = open(path).read()
    if RESEED_AT not in text:
        raise TestFailed("angrylion's n64video.c changed: the tests' reseed does not apply")
    open(path, "w").write(text.replace(RESEED_AT, RESEED + RESEED_AT, 1))
    work = os.path.join(ROOT, "build", "tests", "angrylion-build")
    subprocess.run(["cmake", "-S", source, "-B", work, "-DCMAKE_BUILD_TYPE=Release"], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run(["make", "-C", work, "-j4"], check=True, stdout=subprocess.DEVNULL)
    if os.path.lexists(library):
        os.remove(library)
    shutil.copy(os.path.join(work, "mupen64plus-video-angrylion-plus.so"), library)
    open(stamp, "w").write(RESEED)


def plugins():
    """Makes build/tests/emu, the plugin folder of every test, and points gloveworld's emu.py at it:
    - tests/pulseinput.c, the emulator's input plugin with pulses (emu.pulse(), emu.pulse_left());
    - angrylion with the reseed above;
    - links to the other plugins of gloveworld's build."""
    folder = os.path.join(ROOT, "build", "tests", "emu")
    os.makedirs(folder, exist_ok=True)
    source, library = os.path.join(HERE, "pulseinput.c"), os.path.join(folder, "scriptinput.so")
    if not os.path.exists(library) or os.path.getmtime(library) < os.path.getmtime(source):
        subprocess.run(["cc", "-shared", "-fPIC", "-O2", "-I/usr/include/mupen64plus", "-o", library, source],
                       check=True)
    angrylion(folder)
    for name in os.listdir(gwemu.BUILD):
        link = os.path.join(folder, name)
        if not os.path.lexists(link):
            os.symlink(os.path.join(gwemu.BUILD, name), link)
    gwemu.BUILD = folder


def mpk_path(rom, saves):
    """The core's name for the Controller Pak file of ROM: the header's name, a dash and the first 8 digits of the
    ROM's MD5 in capitals."""
    data = open(rom, "rb").read()
    name = data[0x20:0x34].decode("ascii", "replace").rstrip(" \0")
    return os.path.join(saves, f"{name}-{hashlib.md5(data).hexdigest()[:8].upper()}.mpk")


def symbols(elf):
    """name: address, and the sorted (address, name) of the labels that start code or data in a section."""
    out = subprocess.run(["mips-linux-gnu-nm", elf], check=True, capture_output=True, text=True).stdout
    table = {}
    labels = []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            table[parts[2]] = int(parts[0], 16)
            if parts[1] in "TtDdBbRr":
                labels.append((int(parts[0], 16), parts[2]))
    return table, sorted(labels)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("test")
    ap.add_argument("--rom", default=None, help="default: the practice ROM, or the test build for a test with TEST_BUILD")
    ap.add_argument("--elf", default=None)
    ap.add_argument("--no-pak", action="store_true", help="run without the Expansion Pak (4 MB of RAM)")
    ap.add_argument("--gfx", default="rice", choices=["rice", "angrylion"])
    ap.add_argument("--out", default=None)
    ap.add_argument("--frames", type=int, default=None, help="a length the test may use")
    ap.add_argument("--features", default="off", choices=["off", "on"], help="practice features during the test")
    ap.add_argument("--save-from", help="start with a copy of this save folder (another test's OUT/save)")
    ap.add_argument("--verbose", action="store_true")
    a = ap.parse_args()
    a.out = a.out or os.path.join(ROOT, "build", "tests", a.test)
    os.makedirs(a.out, exist_ok=True)
    os.environ.setdefault("DISPLAY", ":99")
    os.environ.setdefault("SDL_AUDIODRIVER", "dummy")

    sys.path.insert(0, HERE)
    test = importlib.import_module(a.test)
    test_build = getattr(test, "TEST_BUILD", False)
    a.rom = a.rom or (TEST_ROM if test_build else PRACTICE_ROM)
    a.elf = a.elf or (TEST_ELF if test_build else PRACTICE_ELF)
    table, labels = symbols(a.elf)

    def sym(name):
        if name not in table:
            raise TestFailed(f"no symbol {name} in {a.elf}")
        return table[name]

    sym.has = lambda name: name in table
    sym.labels = labels
    plugins()
    # A test lists the controllers that have a Controller Pak in PAKS ("1": controller 1, "12": both).
    os.environ["PULSEINPUT_PAKS"] = getattr(test, "PAKS", "")
    os.environ["PULSEINPUT_RUMBLE"] = getattr(test, "RUMBLE", "")
    os.environ["PULSEINPUT_PORTS"] = "12" if "2" in getattr(test, "PAKS", "") else "1"
    emu = Emu(a.rom, a.gfx, a.out, expansion=not a.no_pak, verbose=a.verbose, save_from=a.save_from)
    result = {"test": a.test, "rom": a.rom, "pass": False}
    if hasattr(test, "prepare"):
        # prepare(mpk, args) may write the Controller Pak file that the core opens when emulation starts.
        test.prepare(mpk_path(a.rom, os.path.join(a.out, "save")), a)
    try:
        if hasattr(test, "check"):
            errors = []

            def timed():
                try:
                    emu.attach_ram()
                    test.check(emu, sym, a)
                except Exception as e:  # reported after the emulator stops
                    errors.append(e)
                emu.stop()

            def idle(e):
                while True:
                    yield 1000

            timer = threading.Timer(test.SECONDS, timed)
            timer.start()
            emu.run(idle, None)
            timer.join()
            if errors:
                raise errors[0]
        else:
            emu.run(lambda e: test.run(e, sym, a), None)
        result["pass"] = True
    except TestFailed as e:
        result["message"] = str(e)
    result["frames"] = emu.frame
    with open(os.path.join(a.out, "result.json"), "w") as f:
        json.dump(result, f, indent=1)
    print(("PASS " if result["pass"] else "FAIL ") + a.test + ("" if result["pass"] else ": " + result["message"]))
    sys.exit(0 if result["pass"] else 1)


if __name__ == "__main__":
    main()
