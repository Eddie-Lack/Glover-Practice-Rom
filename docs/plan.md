# Plan

This is the working plan for the practice ROM. Agents keep it current: mark what is done, and correct what turned
out different. Ideas that need the owner's decision go in [decisions.md](decisions.md), not here.

## Facts to start from

- The retail ROM is 8 MiB. gloveworld splits it into `header`, `boot`, `entry`, `main` (all game code and data)
  and `assets` (`glover.yaml`). Every game function is matched C, compiled with GCC 2.7.2 (`docs/building.md`).
- The game uses the lower 4 MB of RAM. The memory allocator's heap runs from `jallocHeapStart` to 0x80400000
  (`JallocInit` in main.c, game.c and frontend.c). The upper 4 MB (0x80400000 to 0x807FFFFF) is free with the
  Expansion Pak.
- The game reads the controller once per game frame in `ControllerProc` (main.c), then `ControllerStep`. Button
  edges, hold times and the recorded demos all count game frames. A level runs at 20 game frames a second (NTSC).
- The save is a 4 kbit EEPROM (`eeprom.c`).
- The retail ROM still has the team's debug code: `ProcessInputPersonal` (personal.c) reads an unused controller
  entry, `pad[4]`; `PrintTimers` (timer.c) draws timing bars; the demo recorder writes `.moo` files. gloveworld's
  `docs/game.md`, section "Debug code", lists every control. `tools/patches/debugpad2.py` already makes a ROM in
  which controller 2 drives it.
- gloveworld has an emulator harness (`tools/emu`, mupen64plus with the Rice or angrylion video plugin and the SDL
  audio plugin) that runs the ROM, feeds input and reads RDRAM. This repository's harness builds on it
  ([testing.md](testing.md)). Its patch tools (`tools/patches/`) check the input ROM's SHA-1, accept any
  byte order, recompute the header checksum, and have a browser page (`debugpatch.html`) that patches without
  installing anything.

## Repository layout

```
gloveworld/          submodule, pinned to a gloveworld commit
practice/            the practice code (C, built with a current GCC)
  include/           its declarations of the game's types and functions, and its own headers
  src/               the practice segment
  src/loader/        the loader: loads the practice segment, or shows the Expansion Pak message
hooks/               the changes to gloveworld's source that the practice build needs (see Hooks)
  include/           practice_hooks.h
  asm/               the boot hook
tools/               the linker script maker, the layout check, the BPS patch maker, the browser patch page
tests/               the emulator harness and the tests
docs/                plan.md, decisions.md, build.md, testing.md, game.md
Makefile
README.md            for players and contributors
wiki/                the controls and the features, one page per menu
```

## Build

The details are in [build.md](build.md).

1. `make setup` builds gloveworld's toolchain and splits the player's ROM inside the submodule (gloveworld's own
   `make toolchain` and `make setup`).
2. `make` compiles gloveworld's `src/*.c` with the hooks and `-DPRACTICE`, compiles `practice/src/`, and links
   them with a linker script that `tools/mkld.py` makes from gloveworld's `NGVE.ld`. The generated script is not
   tracked. `tools/checklayout.py` checks that every game symbol is at its retail address,
   except the three end labels of the trimmed assets blob ([build.md](build.md)).
3. The ROM is the retail layout. The loader and the practice segment sit in its free end, from 0x780000. The
   ROM stays at 8 MiB.
4. The header gets its own name (`GLOVER PRACTICE`) and a new checksum. The boot chip stays the same (CIC-6102).
5. `make patch` writes a BPS patch from the retail ROM to the practice ROM (about 44 KB, `tools/bps.py`). BPS
   checks both files. No IPS patch yet: the patch page and any BPS patcher cover the
   emulators and flash carts in use.
6. The patch page (`tools/patch.html`, based on gloveworld's `debugpatch.html`) holds the patch, accepts any byte
   order, checks the ROM's SHA-1, and applies the patch. `make patch` writes it as `build/glover-practice.html`.

Code choice (decided): the game's own files stay on GCC 2.7.2 with gloveworld's flags. The practice code uses a
current GCC (`mips-linux-gnu-gcc` 12), which links cleanly with the game's code. Reasons: C11, warnings, smaller
code, and nothing ties unmatched code to the old compiler ([build.md](build.md#code-choice)).

## Memory and ROM map

| Range | Use |
| --- | --- |
| 0x80000000 to 0x803FFFFF | the game, as in retail: every symbol at its retail address |
| 0x80380000 to about 0x80381000 | the loader, during boot only; then it is cleared and the game's heap takes it back |
| 0x80400000 to about 0x8047FFFF | practice code, data and settings (target: at most 512 KB) |
| 0x80480000 to 0x806FFFFF | the savestate pool (2.5 MB): compressed bases and slots ([build.md](build.md#savestates)) |
| 0x80700000 to 0x8077FFFF | the ghosts: four slots of 0x20000 bytes ([build.md](build.md#the-ghost)) |
| 0x80780000 to 0x807FFFFF | the input recording (512 KB: 131072 game frames, 109 minutes) |
| ROM 0x000000 to 0x77DE1F | the retail ROM's layout and data |
| ROM 0x780000 to 0x7FFFFF | the free end of the 8 MiB: the loader, then the practice segment, copied to 0x80400000 at boot |

A full copy of the lower 4 MB does not fit next to the practice code. What a savestate must hold, measured on the route of the faithfulness test (`tests/memory.py`):

| Part | Size |
| --- | --- |
| the game's data and bss (0x801D51D0 to 0x802B0D10), with the thread stacks | 0xDBB40 (878 KB), of which the two display list buffers (`dynamic`) are 0x56460 |
| the heap (0x802B0D10 to 0x80400000) | 0x14F2F0 (1.31 MB); in Atlantis 1 and 2 at most 0x12C920 in use by static blocks, which the game takes from the top |
| the music driver's heap (`audio_memory`) and sound banks below 0x80100000 | 0x493E0 (293 KB) and more |
| the frame buffers and the Z buffer | 3 x 0x25800 (450 KB); probably not needed if a state loads at a frame boundary |

That is about 2.3 MB without compression. One slot fits in the upper 4 MB next to the practice code. Two or
more need fast compression (zero runs and a simple LZ pass) or leaving out the display list buffers. Slots
beyond that are a decision (decisions.md). A state as built is 1.9 MB, without the display
list buffers, the thread stacks and the music driver. The pool of compressed states is described in [build.md](build.md#savestates).

The recording is 512 KB (109 minutes, at 20 game frames a second). A ghost keeps 6.8 minutes.

## Hooks

The practice code needs calls from the game at a few places: boot, the start of each game frame, after the
controller read, before and after the game's logic, the end of the display list, level start and end, death,
checkpoints, and the pause menu.

Decided ([build.md](build.md#hooks)):

1. A hook replaces a call that the game makes with a call to a practice function of the same type,
   `PRACTICE_WRAP(fn)()`. The practice function calls the game's function and does its own work. No code or data
   of the game moves. Every address stays as in retail, and the patch stays small.
2. The hooks are patch files in `hooks/`, applied to a copy of the submodule's source at build time. gloveworld is
   a separate project (the owner's choice): nothing goes upstream. Changes there that would help are noted in
   [gloveworld.md](gloveworld.md).
3. Code that must be in the main segment takes the place of a function that nothing calls and has no rodata.
   The boot hook uses `ClearStaticMemory` (0x74 bytes). `TitleScreen` and `ScreenShot` are larger, but their
   rodata must stay.
4. The test of whether `main` can grow: it can. 353 of splat's fixed-address symbols had to become relative to
   labels, and then a build with growing hooks played Atlantis 1 to its end, frame for frame as retail. It was
   not kept: it moved every later function and variable, which breaks tools that read retail addresses, and
   made the patch 320 KB.

Hooks in place: the table in [build.md](build.md#layout) lists every one.

## Boot

([build.md](build.md#boot)):

- The boot hook runs in MainProc once the scheduler and the message queues exist. It copies a small loader into
  the unused top of the game's heap and runs it.
- The loader checks `osMemSize`. Below 8 MB it shows "Expansion Pak required" and stops. It draws with the RDP
  through the game's own display list code: emulators with high-level graphics plugins do not show a frame
  buffer drawn by the CPU.
- Otherwise it copies the practice segment to 0x80400000 with the game's DMA and clears its bss. The hook then
  calls `PracticeInit`, which clears the loader's memory.
- Practice settings (bindings, toggles, HUD layout) are saved apart from the game's save, in the unused part of the
  EEPROM after the game's data (see Milestones, 1) or in a Controller Pak file. The game's own save blocks are
  touched only by the save file editor, which asks first.

## Input

The practice code reads the controller itself, every game frame, before the game sees it. A practice input is
taken out of the game's input for that frame. The game never acts on it.

Rules:

- No practice input may happen in normal speedrun play. The D-pad, L, Z and every single button are taken by the
  run. The analog stick and Start are nearly free. The four C buttons are never held at once.
- Opening the menu must be possible from any state, and must not need the stick to be centred.

Bindings. The menu combination is chosen; the rest are candidates that the owner tries on hardware:

| Input | Use |
| --- | --- |
| all four C buttons | open or close the practice menu (chosen) |
| stick held fully in one direction plus a D-pad direction | eight quick actions (for example save state, load state, previous and next slot, reset the level, warp to the last checkpoint) |
| Start plus A, B, Z or a C button | more quick actions; Start alone still pauses |
| controller 2 | a second set: every button free for tools, the debug controls of `pad[4]`, frame advance on the D-pad |

All quick actions are bindable in the menu. Store bindings with the settings.

## Drawing

- The overlay is drawn after the game's own display list for the frame: the practice code appends commands (a
  `G_DL` call to its own list) before the list ends. It uses the game's font textures where possible, or its own
  small font in the practice segment.
- The HUD should cost little RSP and RDP time. The lag that the HUD adds is measured with the game's timing
  bars (`PrintTimers`) and the frame counter. The HUD must add no lag frames when it is hidden.
- Menus pause the game's logic while open (optional per menu: some tools want the game running).

## Timers

The owner's rules for runs:

- **Individual levels** matter most. They are timed with the game's own in-game timer. The practice ROM shows it
  with more precision (game frames and hundredths), keeps a best time per level, and can show the difference to
  the best time live and at the end of the level.
- **Full game runs** use real time. Time starts on the frame after the difficulty is chosen to start a new game.
  It ends on the frame Glover enters the loading zone into the fountain to return the final crystal and beat the
  game. The practice ROM offers this timer with splits (one per level or per crystal, chosen in the menu).
- **The in-game timer bug.** After a Time Trial is started for any level, until the console is switched off, the
  in-game timer starts as soon as a level is entered, about 4 seconds early, even outside Time Trial. In the
  normal case it starts about when the player gains control of Glover. The owner believes the Time Trial
  countdown leaves some state set. Find the cause in gloveworld's source (timer.c, game.c, the Time Trial code in
  frontend.c and bonus code), and fix it in the practice ROM: the timer must start at its normal point in every
  level that is not a Time Trial, whatever was played before. Write the cause and the fix into `docs/`, and test
  both cases with the emulator harness (with and without a Time Trial before, compared with the retail ROM's
  normal case). The fix is always on; it is the one deliberate difference from retail with the features off.

## Ball controls

On Medium and Hard, the retail game reverses the stick while Glover stands on the ball; on Easy it does not. The
practice ROM uses the Easy direction at every difficulty by default. A setting in the practice menu ("Ball
controls: Easy / Normal") turns on the retail behaviour, and it is saved with the settings. The test is in
`ProcessInput` (controller.c): the ball-walk direction depends on `saveSlot[currentSaveSlot].difficultyLevel ==
DIFFICULTY_EASY`.

## Milestones

Each milestone ends with a release patch, a short changelog, and tests that pass (see Testing).
The milestones 0 to 4 are done. What each one holds is in [build.md](build.md) and the wiki.

### 0. Foundation (done)

- The repository, the submodule and the build (Build, above). A ROM that boots to the title screen with a line of
  practice text on screen, and the Expansion Pak check.
- The patch maker and the browser patch page.
- The hook approach and the compiler choice for practice code, written down.- The emulator test harness wired up: boot, feed input, read RDRAM, compare with retail. `make test` runs the
  boot test, the test without the Expansion Pak and the faithfulness test ([testing.md](testing.md)).
- The ROM keeps its 8 MiB: the loader and the practice segment sit in the retail ROM's empty end
  ([build.md](build.md#the-free-end-of-the-rom)). `tests/romlayout.py` tests it.

Not tested: hardware, Project64 and RMG. The Expansion Pak message has been seen only in mupen64plus with angrylion.

### 1. Menu and HUD (done)

- The menu: a list with sub-menus, moved with the D-pad, A to choose, B to go back, left and right to change a
  value (Z held: steps of 10). All four C buttons open and close it. The game's logic waits while it is open
  (a setting). It will show the binding next to each item once quick actions exist (milestone 2).
- HUD items, each switchable and placeable (Layout, in units of 2 pixels): the in-game timer (with game
  frames), the input display, the glove's and the ball's position and speed, the glove's action with its time
  and lock and the ball type, the frame counter, the lag counter, the random number seed, the last level's
  in-game time. The timer is on by default.
- The settings save: EEPROM blocks 56 to 63, which the game never uses (docs/game.md). Saved from the menu,
  read at boot. The 64 bytes hold the switches as bits. There is room for 13 HUD items and 12 bindings
  (`SavedSettings` in `practice/include/settings.h`). A save of another version is ignored. The flags byte holds
  8 switches and is full.
- Menu pages and look (done, after the owner found long menus cut off at the bottom): a menu shows at most 14 items
  and a longer one scrolls; related items moved to sub pages. The Settings menu has "Menu opacity" (six steps) and
  "Menu position" (three places across, three down), saved in the two HUD slots of the saved block that no item
  uses. Details in [build.md](build.md#the-menu-pages-and-the-menu-look); `tests/menupages.py`.
- The ball controls setting (see Ball controls): Easy by default.
- The in-game timer fix (see Timers), always on: its cause is `timer321` (docs/game.md).
- The bindings: done in milestone 2.

### 2. Practice basics (done)

- Level select and warps: done. Any of the 42 levels of the game's level select, from a level (through the Time
  Trial's restart exit) or from the title (as LevelSelectScreen does, with a chosen difficulty). Any
  checkpoint, through the game's own respawn. A chosen number of lives, garibs and a power-up (Warp menu, "Lives",
  "Garibs", "Power-up"): done. They apply once the new level's intro is over (the in-game timer runs, or 150 game
  frames passed): the game takes back a ball type given earlier, in the first game frames of a level
  (`tests/warpgive.py`). Such a warp is not kept as a practice best time.
- Reset the level: done (the in-game timer starts again with it). Back to the last checkpoint: done.
- Timers: the in-game timer and its fix are done, and the last level's time (a HUD item). The full-game timer:
  done, with a split at each level or each crystal; the HUD shows the last split, and the menu lists them all.
  The best time per level: done in milestone 4 (practice best times).
- Input display: done, with the last four presses: the game frame of each press and how long it was held.
- The game's own controller display (the demos' picture) during play, for controller 1 or 2: done (HUD menu,
  `practice/src/paddisplay.c`, `tests/paddisplay.py`). It loads texture bank 18 at the level start, so a change
  takes effect at the next level start or reset.
- Frame advance and slow motion (one game frame in 2, 3 or 4): done.
- Cheats from `cheat.c` as switches: done. The game's debug controls of `pad[4]` on controller 2: done (a
  setting). Controller 2's L in them (it marks every level and garib of the save slot as done) has its own
  switch, off by default and saved with the settings: done. A fog switch (Game menu, "Fog": the level's fog or
  none): done ([build.md](build.md#the-fog-switch)). Moon jump and noclip for the glove (Game menu, switches that
  are not saved, with a "MODIFIED" marker on the HUD): done (`practice/src/noclip.c`, `tests/noclip.py`). They have
  no bindable actions: the saved settings hold 12 bindings and all 12 are used ([decisions.md](decisions.md)).
  Set action (Game menu, "Set action"): done. It picks one of 41 glove actions by name and starts it
  with the game's own `ApplyAction` on the next game frame, or bursts, releases or drops the ball
  (`practice/src/setaction.c`, `tests/setaction.py`). The HUD shows "MODIFIED set action" for three seconds.
- Music and sound effect toggles: done (mute).
- Quick action bindings: done, with candidates as defaults (stick fully up or down plus the D-pad) for the owner
  to try. Each action's binding shows next to it in the menu, and the Bindings menu changes them.

### 3. Savestates (done)

- A pool of compressed states in the upper 4 MB: done ([build.md](build.md#savestates)). A state is 1.9 MB raw: the
  game's data and bss, the heap and the texture buffers. The first state in a level takes about 0.6 MB (a base), each
  further state in the level some kilobytes. Ten slots and up to three bases; a load gives the same game again on every
  game frame (`tests/savestate.py`, `tests/slots.py`). A save or a load stops the game for about 0.4 s (1.5 s for a base),
  and the frame before shows "saving..." or "loading...".
- Save and load on bindings (stick fully left plus D-pad up or down) and in the menu, with a short note on screen:
  done. Both use the slot chosen in the Savestate menu.
- The instant reset ("Reset: instant" in the Warp menu): done, from a state that is saved on each level's first game
  frame.
- States on a flash cart's SD card: done for the SummerCart64, untested on hardware ([build.md](build.md#savestates-on-the-sd-card)).
  Up to 100 files of 0.63 MB, each one state with no dependence on a base, in a folder of a FAT16 or FAT32 card, through
  seven items on the Savestate menu's SD card page. Not built: a stronger compressor (about 4.4 times is within reach, the
  files are 3.1 times) and an EverDrive 64 driver (docs/decisions.md).

### 4. Game science and glitch hunting (done)

- Collision view: the faces of the landscape and the platforms, from a cache of the level's meshes built once
  per level and drawn with each actor's matrix, so the view is steady and platforms move with it. The whole level
  by default, or a radius of 1000, 500 or 250. A face filter, three colour modes and three back face modes. The
  collision spheres of the glove, the ball and the enemies, and the open edges (edges of one face only, drawn as
  pink bands). Done ([build.md](build.md#the-collision-view)), with `tests/collview.py` and `tests/collview2.py`.
  The old switch "Up-to-date platforms" is gone: every platform is drawn where it is.
- Collision view, face info and wireframe: done (`Run.collInfo`, `Run.collWire`;
  [build.md](build.md#face-info), `tests/collinfo.py`). The wireframe draws the scene again in a render mode that
  shows only the pixels an edge crosses, so it needs no data.
- Overlay list size: measured. The most one frame used was 3,811 of 6,000 commands (4,611 with every trainer and the
  frame log on as well, so 4,800 stays); `OVERLAY_SIZE` is 4,800
  ([build.md](build.md#hiding-objects), `tests/overlay.py`).
- Hide objects (Inspect, "Hide objects"): done. Fifteen kinds, one bit each in `Run.hide`. The game's drawing
  functions still run; their drawing commands become no-ops ([build.md](build.md#hiding-objects)). `tests/hide.py`.
- Spawn and move actors: done (Inspect, "Spawn actor", and Z on an actor's page in the actor list). The spawn
  list holds a garib, a garib that gives a life, the power-ups whose models the level has loaded, and the enemy
  types that the level has. Platforms are not spawned. Facts and reasons in [game.md](game.md#actors-made-and-moved-at-run-time);
  `tests/actors.py`.
- Actor list and inspector: type, position, speed, state; select one with the D-pad and watch or edit it. The
  list and a details page are done (Inspect, Actors), and a move of Glover above the chosen actor. Editing: done
  (Start on the details page): position, speed and rotation in steps of 0.01 to 100, and the state's hits, damage and
  mode. It counts as a modified game (BT_ACTORS, the HUD's marker). `tests/actoredit.py`.
- A memory watch list and a simple memory editor: done (Inspect, Memory; four words on the HUD). The watch list
  is not saved.
- Random number display and control (`rseed`): the display (with the calls since the level started, and the
  calls on the last game frame), the freeze, one step (as one call of `Random`) and the seed of a level's start
  are done, and setting it: "set" freezes the seed and opens the memory view on the frozen value.
- The glove's and ball's state in detail: action, action time, locks, contact with the ground and the wall.
  Done (Inspect, Glove and ball): the timers and counters of `handControl` and `ballControl`, and the kinds of
  face that each touched on the last game frame.
- Input recording and playback in the game's `.moo` format, plus a richer format with the stick, for checking a
  trick frame by frame. Done in the practice ROM's own form: Record saves a state and keeps each game frame's
  buttons and stick (4 bytes; up to 109 minutes at 0x80780000); Play loads the recording's own state (a pool slot) and gives the input
  back. Recordings survive a power-off on a SummerCart64's SD card (Savestate, SD card, "Save recording to card" and
  "Load recording from card": RECORD.GRC, [build.md](build.md#savestates-on-the-sd-card); `tests/sdrecording.py`,
  `tests/sdrecordingload.py`). The practice ROM does not write the game's `.moo` format (buttons
  only); it plays the game's own recordings instead (Game demos, below).
- Trick trainers (Game menu, "Trainers"; each off at start, not saved): read the game, draw on the HUD and write
  nothing. The frame log is done (Inspect, Frame log): each game frame of the glove with the buttons read, the
  action, its time, its lock and the no-control time. The facts behind the trainers are in
  [game.md](game.md#tricks) and the models in `tools/tricks/`; the boss facts are in [game.md](game.md#the-bosses). The
  owner named four tricks, and a fifth (boss windows) was added:
  - (a) L mash timing: done. Names each L press (stand or hold) with its gap to the press before and a verdict
    (on time, late, slow, rest), a strip of the last 24 game frames, the ball's move, its peak, the mean of the last 20
    and the target for a perfect mash with the pad held, and warnings (pad too weak, camera turning, pad released, in
    the air, another ball, normal controls). `tests/trainers.py`.
  - (b) Line keeper (C-left or C-right on a diagonal): done. Latches the camera's yaw after 4 frames with the same
    stick direction on a diagonal, and each frame shows the heading error E, the advice (E + 2 Edot against 0.10), a
    strip of the last 40 frames with C down, the C share of the last 20 frames against the expected share, and the
    drift of the path. The tap size is `21.82 / D` with D read from the game. `tests/trainers2.py`; the rule is
    `tools/tricks/dpad_trainer.py`. It helps most with the ball stood on and in a mash (the zero delay case).
  - (c) MBZ-L timing: done. For B or A pressed from holding the ball (B also from a bounce): where Z, the aim and the
    L fall against the window, the speed before and after, the best speed, and one verdict. `tests/trainers.py`.
  - (d) Moon Pound aim: done (`practice/src/moonaim.c`). Plays the slam for Z on each of the next 8 frames against the
    collision faces near Glover (the game's contact test as a pure function), shows LAND or CLIP for each, the fall
    column, the nearest hole candidate (found by the dead-cone rule of `tools/tricks/cone.py`), the offset and the
    distance (searched exactly when the column is within 1.5 units), a map, the phase, and the stick for one tap.
    Grey while A is held with the jump still lifting and while the action lock runs; silent in water, with
    Hercules and in the last boss. Its work is held to 4 ms a game frame (a cell with no time is grey); it can add lag.
    `tests/trainers_moon.py`, `tests/trainers_moon_cost.py`. Not done: holes on moving platforms (their faces are
    taken as they are), and a check in levels other than the hub.
  - (e) Boss windows: done (`practice/src/bosswin.c`). In the Atlantis boss, the Prehistoric boss and the last Space
    fight (level 40) it counts the game frames until a hit counts (49, 34 and 20), says how late each hit was and
    why a touch was lost, and shows each boss's own timers (the crab's kill time, the fish's stun, the snowball, the
    reload, the lasers left, the time away). It does not need the glove under control. `tests/bosstrainer.py`.
    Not done: the crab's kill window and the fish's stun were measured with the game's
    flag written, not with a real slam; the Prehistoric hit with a ball of radius 9 or speed 3 was not run.
  - (f) L mash metronome: built, then removed at the owner's request. Its bit (0x20) stays unused.
  - (g) Wall clip (MAZ-R with the ball bearing): done. While the glove holds the ball (JOINED or PREWHACK), the nearest
    wall in front of the ball (`MoonWallScan` in `practice/src/moonaim.c`) gives the distance to its plane and the speed
    along its normal. The window is 6 up to `2.95 * speed + 2.99` (the glove in the air; `2.6 * speed + 2.99` on the
    ground), with a least speed of 3.2 (5.5). The cancel (Z in PREWHACK) keeps its numbers, the frame of the R and
    whether the glove's centre went behind the plane. `tests/trainers3.py` (a wall written into the floor's actor).
  - (h) Moon Pound holes (Inspect, Collision view, "Moon Pound holes"): done. A violet post on each hole that the
    aim's dead-cone code finds within about 50 units of Glover (`HolesGameFrame`, `AddHoles`). `tests/collholes.py`.
  The trainers run from one hook after `ActionsGameFrame`, which takes the state of the glove and the ball. The logic
  runs once a game frame in `PracticeProcessInput`, with the pad of that frame (the first `ClearText` run of a game
  frame often sees the pad of the frame before; [testing.md](testing.md#trainers)). All trainers are quiet in
  noclip, moon jump, the free camera, a pause, an exit, and while the player does not control the glove (the boss windows
  work in the Space fight, where Glover is fixed). [build.md](build.md#the-trick-trainers).
  The trainer log (the last 5 to 10 results at the side of the screen) and the result hold (a trainer's panel keeps
  an attempt's result for 1 to 3 s) are done; both are set in the Settings menu and saved with the settings.
  `tests/trainerlog.py`.
- Camera speed (Game menu, Movement and camera; `normal, fast, faster`; not saved): done. A hook on `MoveCam` raises
  the follow limits (docs/game.md, "How the camera follows"). It taints the practice best time (BT_OTHER). The HUD
  draws a camera icon above the last digit of the score, not MODIFIED. [build.md](build.md#camera-speed).
  `tests/camspeed.py`.

- Show the taps that the game missed (Inspect, second last item; off by default, with a warning that the timing
  changes): done. A thread of the practice code reads controller 1 about every 5 ms and counts the taps that began
  and ended between two game reads; the input display shows them in orange. Details and limits in
  [build.md](build.md#the-extra-controller-reads); `tests/missedtaps.py`.
- Chaos inputs (Game menu; off, rare, sometimes, often): done. Adds a one-frame press of a random button,
  takes away the next press, or nudges the glove's place or speed by a few units; its own random generator; the
  HUD shows "MODIFIED chaos". [build.md](build.md#chaos-inputs); `tests/chaos.py`.
- Memory corruption tools (Inspect, last item; in the memory editor): done. Pokes (Z in the memory view; once,
  freeze, random bits, flip a bit; eight of them) and "Flip one random bit" in a region. Own random generator;
  writes only to RAM below the practice segment and above its data; the HUD shows "MODIFIED pokes".
  [build.md](build.md#memory-corruption); `tests/corrupt.py`.
- The team's early title menu (Warp, 9th item; 10th goes back): done. `TitleScreen` stays at its retail address and
  the hook `PracticeDoMenu` calls it in place of `DoMenu`. [build.md](build.md#the-early-title-menu);
  [game.md](game.md#the-early-title-menu); `tests/titlemenu.py`. A warp from the game's title menu also works now.

- Game demos (done; Warp menu, last three items): plays the game's `.moo` recordings the way the title does. "Demo" picks one
  of eight (Atlantis 1, Carnival 2A, Pirates 1, Prehistoric 1A, Fear 1A, then Carnival 1 and Space 1 which the title never
  shows, and the Space boss cutscene), "Watch demo" plays it, "Watch all demos" plays the eight in order and stops on
  A, B or Start. From a level it leaves through the Time Trial's restart exit. A demo ends at the title, as in the game.
  The placeholder recording of the other levels is not offered. `practice/src/demo.c`; `tests/demos.py`; facts in
  [game.md](game.md#demo-recordings).

- Free camera: done (Game menu). The draw thread builds the view from a camera that controller 2 flies,
  with the game running or waiting; the game's own camera and state are not written. [build.md](build.md#the-free-camera);
  `tests/freecam.py`.
- Ghost of a recorded run: done (Game menu). An input recording also keeps the glove's and the ball's place
  on each game frame; the draw thread draws a see-through glove and ball at the places of the same game frame, in
  step with the live run. One ghost for each of four levels, not saved. [build.md](build.md#the-ghost);
  `tests/ghost.py`.

- Practice best times (done): one best in-game time per level (hundredths, from the HUD timer's count), in RAM apart
  from the game's save, noted at the game's own level-complete point (`CheckForLevelCompleted`, and on Hard, the Time
  Trial, the two calls that follow its own record check). A Time Trial finish counts. Modified runs (also Go to
  checkpoint and a change of Lives or Health in the level), runs with frame advance or slow motion, and runs with a
  savestate loaded are not kept. The Hi Scores screen shows them
  (default; Settings menu, "Hi Scores screen shows") and the real save is untouched. Settings, "Clear practice best
  times". They and the choice of screen can be saved to a Controller Pak (below). The times can also go to the SD card
  (Settings, "Save best times to card" and "Load best times from card": BESTS.GRB, SummerCart64 only).
  [build.md](build.md#practice-best-times); `tests/besttimes.py`.

- Save file editor (done): Settings, last item. Edits a copy of the six save files (lives, crystals won and returned,
  difficulty; per level the done flag, the all-garibs flag and the best time in whole seconds). The game's own copy
  in RAM changes only at Write, which asks first and uses the game's EEPROM requests (6, 4 and 11: files, best
  times, CRCs). Reload asks first, reads the cartridge into buffers and takes it over only when the game's CRC
  check passes. The game keeps one all-garibs flag a level, not a count.
  [build.md](build.md#the-save-file-editor); [game.md](game.md#the-eeprom); `tests/saveedit.py`.

- Controller Pak storage (done): Settings, last item, "Controller Pak". A small driver for the pak's serial
  commands and file system, run by the controller thread a few blocks each game frame. One note, "GLOVER
  PRACTICE", in the standard layout (ID area, index table, note table). Other games' notes stay untouched. It
  holds the full settings, the Hi Scores screen choice, the trainer switches, the practice best times and the
  input recording if it fits. Check, save, load, delete, repair (damaged copies of the tables) and format (only
  for a pak with no valid ID block or index table), for controller 1 or 2; saves, deletes, repairs and formats
  ask first. A save keeps the pak consistent after every write; a Rumble Pak is refused. With the pak unused there is no serial traffic and a
  frame costs one byte test. Not built: the load at boot (docs/decisions.md). Untested on hardware.
  [build.md](build.md#the-controller-pak); `tests/cpak.py`, `cpakload.py`, `cpakforeign.py`, `cpakedge.py`,
  `cpakfull.py`, `cpakbad.py`, `cpaknone.py`, `cpakid.py`, `cpakindex.py`, `cpakcross.py`, `cpakdupe.py`,
  `cpakrumble.py`, `cpakfail.py`, `cpakfailfull.py`.

- Savestate slots and the instant reset (done): ten slots in a pool of compressed states, the recording's own state, and
  the level start state that "Reset: instant" loads ([build.md](build.md#savestates); `tests/slots.py`,
  `tests/instantreset.py`, `tests/lzhost.py`).

- Savestates on the SD card (done, see section 3): a FAT16 and FAT32 file system (`practice/src/fat.c`), the SummerCart64
  driver (`sd.c`; the test mailbox is in the test build only, `make testrom`), the card files and the menu (`card.c`), and the tests `tests/fathost.py`,
  `sdcard.py`, `sdcardload.py`, `sdcardfull.py` and `sdcardbig.py` ([build.md](build.md#savestates-on-the-sd-card)).

### 5. Polish and release

- Documentation for players: the controls, every tool in one line each, how to apply the patch.
- Tests on hardware (a flash cart) and the main emulators.
- A release process (done, [release.md](release.md)): the version comes from the tag `vX.Y.Z` on HEAD (a development
  build shows the next version with the short hash) and shows in the practice menu and on the patch page. `make release`
  checks, builds and packs the patch and the patch page with the version in their names, and `tools/release.py publish`
  makes the GitHub release. The owner runs the publish step.

## Faithfulness

- With all features off, a recorded input played on the practice ROM and on the retail ROM must give the same game
  state, frame by frame. `tests/faithful.py` does this: it compares the game state on every video frame and the
  RAM below 0x80400000 every 250 video frames while a level runs. Since every address is as in retail, the RAM is
  compared by address. Areas that record time (the CPU counter, thread contexts, the audio driver) are masked:
  the practice code costs a few CPU cycles. Results and the masked areas are in [testing.md](testing.md).
- During gameplay there must be no input lag and no change to the picture (the owner). Small differences that do
  not change gameplay are fine: a load that ends a video frame earlier or later, or a value that only records
  time. The test fails on any difference in the game's state, on a visible change to the picture (a coarse picture
  of each frame, [testing.md](testing.md)), and on a game frame that falls on a different video frame of the level
  than in retail. Measure lag in long runs on hardware and in an
  emulator.
- Write each faithfulness result into `docs/testing.md`.

## Testing

- Emulators: Project64 and Rosalie's Mupen GUI first (the runners use them), mupen64plus for the automated harness
  (RMG is built on its core), ares as a check of accuracy. Each needs the Expansion Pak on. The harness uses
  angrylion with one thread: it draws into RDRAM, and its runs repeat exactly ([testing.md](testing.md)).
- Hardware: EverDrive 64 and SummerCart64, each with an Expansion Pak. SummerCart64's USB link can carry debug
  text and recordings (see gloveworld's IS-Viewer notes).
- Automated: the harness boots the patched ROM, plays recorded input, reads the practice code's state from RDRAM,
  and checks results. A test per feature where it can be automated.

## The owner's answers

- Repository: `Glover-Practice-Rom` under Eddie-Lack, public. No licence for now.
- Timing: see Timers, above. Individual levels matter most.
- Hardware and emulators used by runners: EverDrive 64 and SummerCart64; Project64 and Rosalie's Mupen GUI
  (RMG); ares is possible but unlikely. Test these first.
- Region: the USA ROM only.
- The menu opens with all four C buttons.
- Ball controls: Easy by default at every difficulty, Normal as a menu setting.
- Players, the owner included, use Windows and apply the patch with the patch page. Only agents build the ROM, on
  Linux. The patch page is the deliverable that matters; the build needs no setup for Windows or for people.
- The version line is in the practice menu (decisions.md).
