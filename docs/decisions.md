# Decisions for the owner

Ideas that need the owner's decision, mostly absurd or questionable ones. Agents add to these lists and do not
drop an entry without asking. A demo of an open entry is allowed; the full feature waits for the owner. Each entry
gives what it is, the cost, the risks, and a recommendation. The owner marks an entry **Yes**, **No** or **Later**, with a note if needed.

## Build and release

- **Keep the ROM at 8 MiB.** The retail ROM's data ends at 0x77DE20; the last 520 KB of its 8 MiB are zero and
  nothing reads them. The loader and the practice segment (5 KB today) could go there instead of after 8 MiB.
  The ROM would keep its size, which suits every flash cart and emulator, and the file would be smaller. Cost:
  one line in `tools/mkld.py`. Risk: the practice code in ROM is limited to 520 KB (savestates do not count);
  past that the ROM must grow again. Recommendation: yes, until the practice segment outgrows the space. **Yes.**
  **Built.** The loader starts at ROM 0x780000, so the space is 512 KB (the practice segment's RAM limit is the
  same). The cost was more than one line: gloveworld's assets blob holds the zero padding, so the build cuts it
  first (`tools/trimassets.py`). The loader and the practice segment took 47 KB then (130 KB in October 2026). See
  [build.md](build.md#the-free-end-of-the-rom).
- **Send the hooks upstream to gloveworld.** **No.** The owner: treat gloveworld as a separate project, made by
  someone else. The hooks stay as patch files in `hooks/`. Changes that would help go in
  [gloveworld.md](gloveworld.md).

- **Where to keep best times per level.** The plan keeps a best time per level in the practice save. The
  EEPROM has only 64 free bytes, and the settings fill them. Options: (a) keep best times in RAM only, lost at
  power-off: free; (b) a Controller Pak file: lasts, but needs Controller Pak code and the pak in a port;
  (c) the flash cart's SD card (see below). Recommendation: (a) now, (b) later if runners want it. **Kept apart from the real save, shown on the game's Hi Scores screen.** The owner: practice best times (with hundredths) in RAM, or on an SD card or Controller Pak; the Hi Scores screen shows them, and the real save's table stays untouched.
  **Built.** In RAM, in `PracticeBests` (`practice/src/besttimes.c`), shown on the Hi Scores screen by default (the
  Settings menu's "Hi Scores screen shows" and "Clear practice best times" items choose the screen and clear the times). Modified runs are not kept. The times
  can be saved to a Controller Pak (see "Use a Controller Pak" below); SD card storage is not built.
- **The default bindings.** The defaults are the plan's first candidates: the stick held fully up or down plus
  a D-pad direction ([wiki](../wiki/Controls.md)). The owner should try them in real practice; any action can be rebound in the
  menu, also to controller 2. Recommendation: keep them unless they get in the way. **Keep them.**

## Heavy investment

- **Savestates on the flash cart's SD card.** Unlimited slots that survive a power-off, through the SummerCart64
  or EverDrive SD interface. Cost: a driver per cart and a file format. Risk: slow saves (seconds). Recommendation:
  later, after RAM savestates work. **Yes.** The owner: savestates on a mounted drive (a flash cart's SD card); as many as fit, at most 100. Compress them as much as possible.
  **Built** (the SummerCart64 only; `practice/src/card.c`, `sd.c`, `fat.c`; build.md, "Savestates on the SD card").
  The Savestate menu has seven card items at its end (status, Check card, Card slot 1 to 100, Card file, Save slot
  to card, Load from card, Delete on card). A file is one state with no dependence on a base, 0.63 MB. Save, load
  and delete work through a small FAT16 and FAT32 file system of our own, in the folder GLOVERPR as STATE001.GST to
  STATE100.GST. Nothing touches the cart until the player asks, and only Save and Delete write. Tested with a test
  mailbox in place of the cart (the test build; `tests/sdcard.py`, `sdcardload.py`, `sdcardfull.py`, `sdcardbig.py`) and on disk images (`tests/fathost.py`).
  **Not tested on a console:** the SummerCart64 driver (its registers, the data buffer, the address that the SD commands
  take, the card's speed), and the file system on a real card. Left out: a stronger compressor and an
  EverDrive 64 driver (an entry each, below), and exFAT. A card of more than 32 GB is usually exFAT, and then the
  status says "not FAT16 or FAT32": the player needs a FAT32 card or partition.
- **Stronger compression of the card files.** The owner asked for the files to be compressed as much as possible. A card
  file uses the pool's compressor, so it is 633,198 bytes for the 1,965,652-byte state of Atlantis 1 (3.10 times). The
  same state, measured on a PC: deflate level 9 on the whole state 444,094 bytes (4.43 times); deflate level 9 on each
  16 KB chunk 470,212 (4.18); LZMA preset 6 333,000 (5.90). A deflate coder on the console (LZ77 with a hash chain and
  Huffman codes, in the public format of RFC 1951) would make files 26 to 30 percent smaller. Cost: about 5 KB of code in a
  practice segment that has 8.7 KB left, 100 KB or more of scratch memory for the encoder's chains (the pool may be
  full, so the memory would come from a recording or ghost buffer or from a smaller pool), and about 3 s more for a
  save on the console (an estimate). A load would take about 1 s longer. Risk: more code that a console must run
  right. LZMA is out of reach for the console (about 25 s a save and a megabyte of dictionary). The gain is a shorter
  write to the card, not room: 100 files take 63 MB. Recommendation: no, unless saves to the card feel slow on a console.
- **An EverDrive 64 driver.** No public document of the EverDrive's registers was found: its manuals are for users and
  point to a source repository (github.com, blocked here) that is code. A driver from memory would be a guess on a
  device that holds the player's saves. Cost: a driver of about 300 lines once there is a document. Recommendation:
  wait for a register document or for an owner who has the cart and can test.
- **More savestate slots.** One state is 1.9 MB; the space after the practice code (3.5 MB) held one. Run packing
  would give 1.24 MB, an LZ scheme about 0.5 MB (measured in Atlantis 1 with zlib). Recommendation: yes.
  **Yes, through the SD card** (above), with the strongest compression that is practical.
  **Built for the RAM.** Ten slots in a pool of 2.4 MB (`practice/src/savestate.c`, `practice/src/lz.c`; numbers in
  [build.md](build.md#savestates)). The compressor is the practice code's own, an LZ with word-aligned matches
  (3.0 to 3.1 times on a state). A slot is stored as its difference from a base, the compressed state of its level, which
  takes some kilobytes instead of 0.6 MB. The cost is time: a first state in a level takes about 1.3 s on the console
  (by the emulator's count), a further one 0.4 to 0.7 s, a load 0.4 s. The recording (it was 1 MB) shrank to 512 KB
  for the room. The SD card files (above) use the same compressor; a stronger one is an open entry above.
  Record input saves into a slot of its own and does not touch the chosen slot. Open for the owner: (1) the first state in a level takes 1.5 s (the compressor's
  work; a weaker one that takes 1.2 s makes the base 4 percent larger, and the decoder is the cost of a load, 0.4 s);
  I recommend keeping it. (2) When the pool is full, a save that does not fit empties the slot that it was meant for
  (its old state is freed first, then the save is tried again); the other slots are never touched. (3) The fourth
  level's states take about 450 KB each, because they have no base of their own and are stored against the closest
  base. Four bases need 2.53 MB and the pool is 2.44 MB. `STATE_BASES` is 3. A larger pool or a smaller ghost
  buffer (512 KB for four ghosts) would hold a fourth base.
- **More in the collision view.** Ideas from the design that were left out: the collision spheres of every
  enemy (their positions need the game's rotation code, which can stop the game on a bad value, so they need
  their own checked copy); platforms brought up to date from their mesh, position and rotation when far from
  Glover; edges drawn as lines (the game has no line microcode loaded); open edges that Glover could fall
  through. Cost: a day or two each. Risk: draw-thread time, which can add lag. Recommendation: the enemy spheres
  and the platform update first, if glitch hunters want them. **Yes: enemy spheres, up-to-date platforms and open edges, each with its own switch.**
  **Built.** Items of the Inspect menu's Collision view page: "Enemy collision spheres" and "Open collision edges"
  (`practice/src/collview.c`, `tests/collview2.py`; details in docs/build.md, "The collision view"). "Up-to-date
  platforms" was built too and is gone again: the view now draws every platform where it is, from a cache of its
  mesh. Drawing every edge as a line is planned as the wireframe.
- **Ghost of a recorded run.** Draw a see-through glove and ball from a recorded best run, in step with the live
  run. Cost: recording positions every frame, drawing a second glove model. Risk: RDP time. Recommendation: yes,
  **Yes.**
  **Built.** Game menu, "Ghost" (`practice/src/ghost.c`, `tests/ghost.py`). Each input recording also keeps
  the glove's and the ball's place and the glove's heading on every game frame (16 bytes a frame, in the Expansion
  Pak's memory from 0x80700000). With the switch on, a see-through yellow glove (with a white nose for its heading)
  and a pink ball are drawn at the places of the same game frame, counted from the recording's savestate. The
  glove model is not drawn: both are icosahedra. Four levels keep a ghost, each up to 8192 game frames (6.8
  minutes). Details in [build.md](build.md#the-ghost).
- **Rewind.** Keep a savestate every N frames in a ring and step back. Cost: much RAM, fast compression. Risk:
  lag while saving. Recommendation: later, as a lighter version (one state a second, a few seconds back). **No** for now.
- **Live link to a PC.** Send the timer, splits and game state over the SummerCart64 USB link to LiveSplit or a
  viewer. Cost: a PC tool. Recommendation: later. **No** for now.
- **Free camera.** Detach the camera and fly it with controller 2, the game paused or running. Cost: moderate.
  Risk: the game culls by its own camera. Recommendation: yes. **Yes.**
  **Built.** Game menu, "Free camera" (off, or a speed of 2 to 32 units a controller read)
  (`practice/src/freecam.c`, `tests/freecam.py`). The hook `PracticeguLookAtHilite` replaces only the eye, the target
  and the up vector of the view matrix; the game's camera and the game's state are not written. Controller 2 flies it
  (also while the game waits). The HUD shows a "free camera" note, not the MODIFIED marker. The culling risk is real
  and stays: [game.md](game.md#the-free-camera) lists what the game leaves out and why widening it is not cheap.

- **A metronome for the L mash.** A flash on every 2nd game frame from the first hold press. The player presses in
  step with it. It only draws. Cost: small. Risk: none for the game. Recommendation: yes, after the four trainers.
  **Built, then removed** at the owner's request (2026-11-04).
- **A dead-cell painter in the collision view (Moon Pound).** Marks the places where a falling glove touches no
  face (the holes at mesh vertices) in the collision view's colours. Read only. Cost: a day. Risk: draw time, as for
  the collision view. Recommendation: yes.
  **Built.** Inspect, Collision view, "Moon Pound holes": a violet post on each hole within about 50 units of Glover
  (`tests/collholes.py`). The game thread finds them (up to 3 ms a game frame measured); the view only draws.
- **A wall-clip indicator for MAZ-R.** Shows the cancel frame's distance to the wall against the window of 6 to
  about 23.65 units at speed 7 (docs/game.md, "Walls and the ball bearing"). Read only. Cost: a day. Recommendation:
  yes, after the four trainers.
  **Built.** Game menu, Trainers, "Wall clip" (`tests/trainers3.py`).
- **How the line keeper latches its reference.** Automatic after 4 steady frames of the same pad direction, or by a
  button combination. Automatic needs no input and is the plan. A combination costs a binding. Recommendation:
  automatic first; add the combination if the latch picks the wrong moment.

## Changes game behaviour (only when switched on)

- **A faster reset.** Reset and warp leave the level through the game's own exit, with its fade (about a
  second), then load the level. A reset without the fade (free the level and start it on the next game frame)
  would save that second. Risk: the draw thread may still use the freed level. Savestates now show a safer
  way: save a state automatically on the first game frame of each level, and make Reset load it. That is
  instant and gives exactly the level's start (with the same random numbers). Cost: a second state slot, so it
  waits for compression (More savestate slots, above), and the music does not restart. Recommendation: yes,
  as a setting ("Reset: load the level / instant"), once two slots fit. **Yes, as a setting** ("Reset: load the level / instant").
  **Built.** Warp menu, last item ("Reset"). It is off by default, and while it is off nothing is saved at a level start.
  The state of the level's first game frame goes into its own slot (the base of that level and a slot with no
  difference from it); the first time a level is entered it takes about as long as a first state, later times less.
  Reset loads it in about 0.25 s (it was 0.4 s: a load now skips the chunks that the game already holds). The level
  start state of a later start is stored without compression, up to 320 KB. The music does not restart: the song that
  plays goes on, and sound effects stop (as after any load). **Open for the owner:** the instant reset is silent
  where a real level start has ambient effects, because the state does not run the level start and `MusStop` stops the
  effects. Which levels are affected has not been listed. Options: leave it; or start the level's ambient sounds again
  after the load (cost: finding them per level; risk: a sound that plays twice). Recommendation: leave it until a
  player misses a sound. A reset also acts only in a level that runs: in a pause, a cameo or while Glover dies it is the
  normal reset. The game's RAM copy of the save (the save slots, best times, Hi Scores table and CRCs) is kept as it
  is at the reset; a plain load puts back the copy of the save. The setting is saved in a spare bit of the saved
  settings (bit 15 of the 16-bit HUD flags, which has 3 free bits above its 13 HUD slots). The EEPROM block keeps
  its version and its 64 bytes; the Controller Pak note holds the whole settings structure and has it too. Details in
  [build.md](build.md#savestates).
- **Controller 2's L in the debug controls.** The team's debug code (`ProcessInputPersonal`) marks every level
  and garib of the current save slot as done when L is pressed on `pad[4]`, and the game may save it. The
  practice ROM leaves L out of the debug controls. Cost: none. Risk: a player loses a real save. Recommendation:
  keep it out; if wanted, add it behind a separate switch with a warning. **Behind its own switch**, off by default, with a warning.
  **Built.** Game menu, "Controller 2: L marks all done", below the debug controls switch. It acts only when
  the debug controls are on. A warning shows in place of the key help while it is selected. It is saved with the
  settings (bit 7 of the flags byte, which is now full).
- **Noclip and moon jump for the glove.** For reaching places quickly while glitch hunting. Recommendation: yes,
  behind a clear "modified" marker on the HUD. **Yes.**
  **Built.** Game menu: "Moon jump", "Noclip" and "Noclip speed" (`practice/src/noclip.c`, `tests/noclip.py`).
  They are switches in the Run struct and are not saved. They have no bindable actions: `BIND_SLOTS` is 12 and
  all 12 hold the quick actions, and the saved settings have no room for more. The moon jump button is L on
  controller 2, or L with the stick held fully up on controller 1. A ball in hand goes with the glove in noclip.
  A free ball stays behind. Enemies can still hurt the glove in noclip, as flag 0x08 stays on. Open question for
  the owner: should the next settings version hold more bindings? Then these two could have actions.
- **Set any action of the glove or ball directly.** Puts the glove in any state from a list. Risk: crashes.
  Recommendation: yes, for glitch hunting. **Yes.**
  **Built.** Game menu, "Set action": a list of 41 glove actions and three ball states (burst, release,
  normal). It calls the game's `ApplyAction`, `BurstBall`, `ReleaseBall` and `DropBall` on the next game frame
  (`practice/src/setaction.c`, `tests/setaction.py`). Left out because they can crash from a state without their
  set up: hopping, frog tongue, cast spell and nothing (reasons in `practice/include/setaction.h`). Duck and
  cartwheel are tested. The others are not tested one by one and may crash; the menu warns.
- **Spawn or move actors.** Place an enemy, a platform or a pick-up anywhere. Risk: crashes, saves with broken
  data. Recommendation: later. **Yes.**
  **Built.** Inspect menu: "Spawn actor" (the third last item) and Z on an actor's details page. Spawned: a garib, a
  garib that gives a life, power-ups, and enemies of the types that the level has. Moved: any actor but the
  camera, the glove, the ball, landscape, water and cameo actors; a platform with no motion becomes a platform
  that the game brings up to date, so its collision follows. Not spawned: platforms, landscape, water, bosses,
  and enemy types that the level has not loaded (reasons in [game.md](game.md#actors-made-and-moved-at-run-time)).
  The HUD shows "MODIFIED actors"; the menu items warn about crashes and the save (`practice/src/spawn.c`,
  `tests/actors.py`).
- **Lag reduction.** Turn off effects (particles, shadows, fog) to remove lag. Risk: times no longer match real
  runs. Recommendation: no for practice timing, maybe as a separate science tool. **Only a fog switch** (on and off); not particles or shadows.
  **Built.** Game menu, "Fog" (the level's fog, or off). It is not saved. See
  [build.md](build.md#the-fog-switch).
- **The version line.** The practice ROM draws its version in the practice menu only, and nothing of its own
  until the player opens the menu or turns on a HUD item. **Yes.** The text comes from the git tags
  ([release.md](release.md)).
- **Trick trainers: which tricks, and their names.** **Yes, built in parts.** The owner has described the tricks.
  Four trainers are specified (docs/game.md, "Tricks"; the models are in `tools/tricks/`):
  (a) L mash timing, (b) the line keeper (a C-left or C-right tap that keeps a diagonal walk straight),
  (c) MBZ-L timing (the Z cancel of the throw aim or the slap, then L) and (d) a Moon Pound aim. All four only
  read the game and draw on the HUD. All four are built (Game menu, "Trainers"). The slap's Z cancel is the Z during the windup, as `tests/slapcancel.py` shows.
- **Show the taps that the game missed.** The game reads the controllers once a game frame, so a tap between two
  reads never reaches it (docs/game.md, "The controllers"). An input display could show such taps if the practice
  code read the controllers more often. Cost: a day. Risk: the serial interface is shared with the game, and
  extra reads change the game's timing and lag, so it cannot be on during faithful practice. Recommendation:
  only as a separate science tool, off by default, with a warning that the timing changes. **Yes, as a science tool, off by default**, with a warning that the timing changes.
  **Built.** Inspect menu, "Show missed taps" (the second last item), with the warning in place of the key help. A
  thread of the practice code reads controller 1 about every 5 ms, and the input display shows the taps that
  began and ended between two game reads. Details and limits in [build.md](build.md#the-extra-controller-reads);
  `tests/missedtaps.py`.
- **PAL timing on an NTSC ROM.** Run the logic at 50 Hz-like timing for PAL practice. Risk: not the same as a
  real PAL console. Recommendation: no, support the PAL ROM instead if wanted. **No.**
- **An auto-mash switch.** The tool presses L on every 2nd game frame. The player sees what the target looks like.
  It changes the input. It needs a "MODIFIED" marker and keeps best times out. Cost: small. Risk: a run on it is
  not a run. Recommendation: a demo only, not a switch in the default build.
- **An auto C-tap assist, a yaw lock and camera swing off.** The assist taps C-left or C-right and keeps a diagonal
  line. The yaw lock stops the camera from turning with the walk. Camera swing off sets `camBehindSpeed` to 0 and
  adds 5 to the mash speed. All change the game. Cost: a day each. Risk: they teach habits that the retail game
  does not allow. Recommendation: no in the default build.
- **A Moon Pound aim assist.** Nudges `hand.pos` by under 0.5 units on the hover passes, or a longer hover. The fall
  column then reaches the hole. Changes the game. Cost: small. Risk: a clip that a player could not make.
  Recommendation: no in the default build.

## Saves and data

- **Save file editor.** Edit levels done, garibs, lives and best times in the game's own EEPROM save. Risk: the
  game saves the edit. Recommendation: yes, with a warning. **Yes.**
  **Built.** Settings menu, last item "Save file" (`practice/src/saveedit.c`, `tests/saveedit.py`). The warning
  "Edits your real save!" shows on the menu item and in the editor; a write asks twice. Details in
  [build.md](build.md#the-save-file-editor).
- **Use a Controller Pak** for practice settings and recordings. Cost: Controller Pak code. Risk: it takes the
  place of a Rumble Pak in that port. Recommendation: only if the EEPROM space is too small. **Yes.**
  **Built.** Settings menu, last item "Controller Pak" (`practice/src/cpak.c`, `tests/cpak*.py`). One note,
  "GLOVER PRACTICE", in the standard layout; other notes are never touched. It holds the full settings, the Hi
  Scores screen choice, the trainer switches, the practice best times and, if it fits, the input recording. Saves
  and deletes ask first; a format is offered only for a pak with no valid ID block or index table. Controller 1 or 2. Not built:
  the load at boot (see the next entry). The hardware path is untested: only the emulator's pak was used.
  [build.md](build.md#the-controller-pak).
- **Load the Controller Pak note at boot.** With a pak in controller 1 and a practice note on it, load the note
  after the EEPROM settings, as the settings load. Cost: one more serial transfer series at boot, before the game
  sets up the serial interface, and a switch to turn it off (the EEPROM block has no free bit: the settings version
  would change or the switch would live only in the note). Risk: a boot that takes a few milliseconds longer with
  a pak, and a pak that is slow to answer. Recommendation: later, if players ask for it. **Open.**
- **Save the trainer switches with the settings.** `run.trainers` is not saved in the EEPROM: the saved block is full (64 bytes). The Controller Pak note does
  save it.
  Saving it needs a settings version bump and a layout change. Cost: small. Risk: old saved settings stop loading,
  or load with a default. Recommendation: later, when the four trainers are done and the owner has chosen which to
  keep on by default.

## Unusual ideas

- **The team's early title menu.** `TitleScreen` (game.c) is an early title menu with continue, new game and a
  level select. Nothing calls it. A practice menu entry could open it, as a piece of the game's history. Cost:
  small; the code is in the ROM. Risk: it may expect state the retail menus no longer set up. Recommendation:
  **Yes.**
  **Built.** Warp menu, "Early title menu" (9th item) and "Back to the normal title" (10th). The code stayed at its
  retail address. It needed no state that the retail menus set up. See [game.md](game.md#the-early-title-menu) and
  [build.md](build.md#the-early-title-menu); `tests/titlemenu.py`.

- **Randomiser mode.** Shuffle level order, garib places or ball types. Cost: heavy. Recommendation: later, as its
  own project. **No** for now.
- **Chaos inputs.** Practise recovery: the tool flips an input or nudges the glove now and then. Recommendation:
  later. **Yes.**
  **Built.** Game menu, "Chaos inputs": off, rare, sometimes or often. See
  [build.md](build.md#chaos-inputs); `tests/chaos.py`.
- **Memory corruption tools.** Write chosen values into actor or level data on a binding, for finding crashes and
  arbitrary behaviour. Recommendation: yes, inside the memory editor. **Yes.**
  **Built.** Inspect menu, "Memory corruption" (the last item), and Z and Start in the memory viewer. The
  bindings are full, so the actions are menu items. See [build.md](build.md#memory-corruption);
  `tests/corrupt.py`.
