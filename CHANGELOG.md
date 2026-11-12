# Changelog

## Not yet released

- Inspect menu: "Hide objects" hides one kind of object at a time: the landscape, scenery, water, platforms, the
  glove, the ball, enemies and their shots, pick-ups and garibs, debris, the sky, the HUD's counters, shadows,
  footprints, trails and lightning. "Show everything" brings them all back. A run with anything hidden doesn't count
  for a practice best time.

## 0.9.10 (2026-11-04)

- Removed the L mash metronome trainer.

## 0.9.9 (2026-11-01)

- HUD menu: "Controller display" shows the game's own controller picture from its demos while you play, for
  controller 1 or 2.
- Fixed: each Reset level or Warp from a level added the level's garibs to the garib total again (0/50, then 0/100).
- The wiki's list of controller 2's debug controls now covers R (inertia), D-pad up and down, and Z with the stick.

## 0.9.0 (2026-10-28)

The first version for players, a beta. It needs the Expansion Pak.

Note: the SD card and Controller Pak tools haven't been tested on a console yet

The controls and every feature are described in the `wiki/` folder (start at `wiki/Home.md`), with one page for each
menu. The README links to it.

### Menu and settings

- Hold all four C buttons to open or close the practice menu. The game waits while it is open.
- Long menus have sub pages (Savestate > SD card, HUD > Counters, Game > Random numbers, Controller 2 and Movement
  and camera, Inspect > Collision view). A menu shows at most 14 items and fits the screen.
- The Settings menu sets the menu's opacity (clear to solid) and its place (left, centre or right; top, middle or
  low). Both are saved with the other settings.
- The settings are saved on the cartridge, next to the game's own save, and load when the console starts.
- Quick actions on button combinations (the stick held fully in a direction plus the D-pad), each one
  changeable in the menu, also to controller 2.

### Practice tools

- Watch the game's demo recordings, one or all in order (Warp menu), including two the title never shows.
- Warp to any level, from a level or from the title screen, with a chosen difficulty. A warp from the game's title
  menu (not only from the flythrough) works.
- The original team's early title menu (Warp menu): continue, new game and level select over a rotating view of a
  level. The game never calls it. "Back to the normal title" returns.
- Reset the level; go back to the last checkpoint or to any checkpoint.
- Frame advance, one game frame at a time, and slow motion.
- Savestates in ten slots: choose a slot in the Savestate menu, save the game's state into it and load it again, also
  from another level, with the level's song. States are compressed. The first state in a level makes a base of
  about 0.6 MB; every further state in that level takes some kilobytes. Up to three levels have a base. The menu shows
  the slot's level and size and the free pool; a slot or all slots can be cleared. The bindings for save and load use
  the chosen slot. A save or a load stops the game for 0.4 s (1.5 s for the first state in a level; times from an
  emulator's clock), and the frame before shows "saving..." or "loading...". A save that does not fit empties its slot
  and says so.
- Savestates on the SD card of a SummerCart64: up to 100 files (`GLOVERPR/STATE001.GST` to `STATE100.GST` on a FAT16
  or FAT32 card), each one state that needs no other file, about 0.63 MB. Seven items at the end of the Savestate menu:
  the card's status, Check card, the card slot, the card file, Save slot to card, Load from card and Delete on card. The
  game stops during a save or a load, a bar shows the progress, and the message gives the size and the time. Nothing
  is written to the card except by Save and Delete, and the cart is not touched until one of the items is used. A load
  checks the whole file before the game changes. Not tested on a console. There is no driver for other flash carts.
- Input recording: Record saves its state into a slot of its own and does not touch the chosen slot. A save that does
  not fit says "recording not started: pool full". A recording keeps up to 109 minutes (it was 3.6 hours).
- Instant reset (Warp menu, last item, "Reset"): the state of each level's first game frame is saved on its own, and Reset
  loads it in 0.25 s. No fade, the same random numbers, places and timers as a real start. The music keeps playing and
  the sound effects stop; the game's save is kept as it is. It acts only in a level that runs: not in a pause, a
  cameo or while Glover dies. Off by default, and nothing is saved at a level start while it is off. Saved with
  the other settings.
- The game's cheats as switches; power-ups, lives and health; mute the music or the sound effects.
- The original team's debug controls on controller 2. The one that marks every level as done in your save has its
  own switch, off by default.
- A fog switch: draw the level without its fog.
- Moon jump and noclip for Glover, with a "MODIFIED" marker on the HUD while either is on.
- Free camera (Game menu): controller 2 flies a second camera, also while the game waits. The game
  and its camera do not change. The HUD shows a "free camera" note. Quick-action bindings on controller 2 do not
  fire while it is on.
- Ghost of a recorded run (Game menu): a see-through glove and ball are drawn where the glove and
  the ball were on the same game frame of an input recording. One ghost for each of four levels, kept until a new
  recording of the level replaces it.
- Chaos inputs (Game menu): now and then add a one-frame press, take away your next press, or
  nudge Glover's place or speed a little; rare, sometimes or often. The HUD shows a "MODIFIED" marker.
- Set action (Game menu): put the glove in any of 41 actions by name, or burst, release or drop
  the ball.
- Ball controls: Easy at every difficulty, or Normal as in the original game.
- Trick trainers (Game menu, Trainers; off at start): L mash timing shows each L press as stand or hold with its gap and
  a verdict, the ball's move and the target of a perfect mash, and warnings. MBZ-L timing shows where Z, the aim or
  windup and L fall against the window, the speeds and one verdict. The line keeper shows when to tap C-left or
  C-right to keep a straight line on a diagonal D-pad. The Moon Pound aim shows which of the next 8 frames a Z press
  would slam through the floor, and where the nearest hole is. The boss windows trainer works in the Atlantis boss,
  the Prehistoric boss and the last Space fight. It counts the game frames until a hit counts (49 for the whale, 34
  for the Prehistoric boss, 20 for each Space part), says how late each hit was and why a touch was lost, and shows
  the boss's own timers. All five only read the game.
- Trainer log and result hold (Settings): a list of the last 5, 6, 8 or 10 results of the active trainers at the
  side of the screen, newest first. When an attempt ends, the trainer's panel keeps its result for 1, 2 or 3 seconds.
  Both are saved with the other settings.
- Camera speed (Game, Movement and camera): `normal, fast, faster` make the camera keep up with Glover in a mash or a
  bounce MBZ-L. A run with it on doesn't count for a practice best time. A camera icon shows above the score.

### Controller Pak storage

- Settings, last item, "Controller Pak": save the full settings, the Hi Scores screen choice, the trainer
  switches, the practice best times and the input recording (if it fits) to a Controller Pak in controller 1 or 2,
  and load them back. One note, "GLOVER PRACTICE", in the standard layout; other notes are not changed. Save,
  delete and format ask first, and a format works only on a pak that is not formatted. With the pak unused
  nothing changes. Tested in an emulator only.

### Save file editor

- Settings, last item, "Save file": edit the game's save files (lives, crystals won and returned, difficulty) and
  each level's done flag, all-garibs flag and best time. "Write to the cartridge" asks first, then writes the files
  and the best times with the game's own save requests and checksums. "Reload from the cartridge" discards the
  edits. The menu warns "Edits your real save!". With the editor unused nothing changes.

### Practice best times

- The best in-game time of each level, with hundredths, kept apart from the game's save and shown on the game's
  Hi Scores screen. Runs with noclip, moon jump, chaos, pokes, set action, spawned or moved actors, Go to
  checkpoint, a power-up given from the menu, Lives or Health set, a frozen or changed random seed, controller 2's
  debug controls, cheats, frame advance, slow motion or a savestate load are not kept. The Settings menu items "Hi Scores screen shows" and
  "Clear practice best times" choose what the screen shows and clear the times. They are lost at power-off.

### HUD

- The in-game timer, with hundredths and game frames.
- An input display with the last presses; the glove's and the ball's position and speed; the glove's action, and
  whether the glove is on the ground.
- A frame counter, a lag counter, the random number seed, and the last level's time.
- A full-game timer in real time, with a split at each level or each crystal.
- A watch list of memory words.

### Game science

- The collision view: the faces the game collides with near Glover, coloured by kind, and the collision spheres
  of the glove and the ball.
- Collision view, three more switches: the collision spheres of the enemies, platforms drawn where they are now
  (also far from Glover), and the open edges (edges of one face only).
- The list of the level's actors, with details, and a move of Glover to any of them.
- Spawn actor (Inspect): a garib, a power-up or an enemy of a type that the level has, made in front of Glover. On
  an actor's page in the actor list, Z brings that actor to Glover; a platform's collision follows it. The HUD
  shows "MODIFIED actors".
- Show missed taps (second last item of the Inspect menu, off by default, with a warning): extra controller reads show
  the taps that start and end between two of the game's reads, in orange on the input display. It changes the
  game's timing.
- A memory viewer and editor.
- Memory corruption (last item of the Inspect menu): mark words as pokes in the memory viewer (once, freeze, random
  bits, flip a bit), and flip one random bit of the glove, the ball, a level actor or a range of memory. Writes
  go only to RAM, never to the practice code. The HUD shows "MODIFIED pokes".
- The glove's and the ball's timers, counters and contacts, live.
- The frame log: each game frame's buttons and the glove's action, lock and timers, for checking a trick frame
  by frame.
- Freeze the random numbers, step them one call, or set them to a level's start or to any value.

### Fixes

- The in-game timer starts at its normal point in every level. In the original game it starts at once, about 4
  seconds early, in every level after any bonus level.
