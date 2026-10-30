# Third party notices

This repository uses or builds on the projects below. Each section names the source, where it is used and its licence.
This file does not set a licence for the Glover Practice ROM itself.

No ROM, and no data copied from one, is in this repository. The patch that a release hands out holds only the
practice code and the changed bytes.

## 1. gloveworld

- Source: https://github.com/Eddie-Lack/gloveworld (the `gloveworld` submodule), a matching decompilation of Glover (USA)
- Licence: the checkout has no licence file. It is a project of the same owner as this repository. The owner
  decides how its files may be reused.
- Use, by file:
  - `hooks/*.patch` are patches to gloveworld's `src/` files (`main.c`, `game.c`, `credits.c`). They hold lines of those
    files as context. `hooks/include/practice_hooks.h` and `hooks/asm/` are our own.
  - The game's own code is built from gloveworld's sources, its GCC 2.7.2 and its linker script (`Makefile`,
    `tools/mkld.py`, `tools/trimassets.py`). The practice build uses its objects and its ELF
    (`tools/checklayout.py`, `tools/mkstate.py`).
  - `practice/include/game.h` and other headers in `practice/include/` list names, struct layouts and values of the game
    (for example `include/hand.h` for `stateview.c`). They come from gloveworld's headers and `symbol_addrs.txt`. The
    structs hold only the fields used.
  - `tools/mkrom.py` imports `checksum` and `put_word` from gloveworld's `tools/patches/debugpad2.py` when it runs. The
    file is not copied.
  - `tools/patch.html` (the patch page) began as gloveworld's `tools/patches/debugpatch.html`. The page's script
    (reading the ROM, byte order, SHA-1 check, applying the patch) follows it. The look is new.
  - `tests/pulseinput.c` is gloveworld's `tools/emu/scriptinput.c` with pulses added (see 2).
  - `tests/harness.py` and the tests run on gloveworld's `tools/emu/emu.py`. They import it from the submodule.
  - `tools/tricks/*.py` model one game frame of gloveworld's C. Each file names the functions it follows.

## 2. mupen64plus

- Source: https://github.com/mupen64plus/mupen64plus-core (plugin API headers `m64p_plugin.h` and `m64p_types.h`)
- Licence: GNU GPL version 2 or later. Full text: https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt
- Use: `tests/pulseinput.c` is an input plugin for mupen64plus. It includes those headers and is built with `cc`
  into `build/tests/`. The headers and mupen64plus are not included. The plugin is a test tool and is not part of a
  release.

## 3. angrylion-rdp-plus

- Source: https://github.com/ata4/angrylion-rdp-plus, fetched and built by gloveworld's `tools/emu/setup.sh`
- Licence: the MAME licence (the file `MAME License.txt` in the source)
- Use: `tests/harness.py` copies the source into `build/tests/`, adds one line to `src/core/n64video.c` (a fixed noise
  seed for each display list) and builds it. The result is a modified build used for test pictures only. It is not
  committed or released.

## 4. SummerCart64

- Source: https://github.com/Polprzewodnikowy/SummerCart64
- Licence: the repository is under the GNU GPL version 3 (file `LICENSE`)
- Use: `practice/src/sd.c` is written from the cart's public documentation (`docs/00_quick_startup_guide.md`,
  `01_memory_map.md`, `02_n64_commands.md`, `03_usb_interface.md`, `04_config_options.md`). The register addresses,
  the command letters and the key values are facts of the cart's interface. No source code of the cart, its
  firmware or any program for it was read or copied.

## 5. N64 hardware documentation

- Source: the public RSP and RDP command documentation (the n64brew wiki, https://n64brew.dev)
- Use: `practice/include/rdp.h` follows the layout of the RSP and RDP commands. Facts and layouts are used. No text or
  code is copied from the wiki.
- The Controller Pak driver (`practice/src/cpak.c`) is written from the commonly known layout of the pak's serial
  commands and file system, with no document read and no code copied (docs/build.md, "The Controller Pak").

## 6. File formats

- BPS patches (`tools/bps.py`): the format of byuu's beat. The encoder and decoder are our own code, written from the
  format's description.
- FAT16 and FAT32 (`practice/src/fat.c`): our own code, with no code from another file system library.
- The N64 header checksum for the CIC-6102 boot chip comes from gloveworld's `debugpad2.py` (see 1).

## 7. Tools used but not included

These build or test the project. None of their code is in the repository or in a release.

- The [decompals build of GCC 2.7.2 for MIPS](https://github.com/decompals/mips-gcc-2.7.2) (GPL 2), through gloveworld:
  compiles the game's own files
- GCC for `mips-linux-gnu` and GNU binutils (GPL 3): compile and link the practice code
- [mupen64plus](https://mupen64plus.org) (GPL 2 or later) and its plugins: run the emulator tests
- [Playwright](https://playwright.dev) (Apache 2.0): the patch page test (`tests/patchpage.js`)
