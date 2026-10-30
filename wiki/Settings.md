# Settings

_Back to the [Home page](Home.md)_

| Option | What it does |
| --- | --- |
| Save settings | Save the settings to the cartridge |
| Load settings | Load them |
| Default settings | Go back to the defaults |
| Status | Shows `saved, loaded, no EEPROM, none saved` |
| Pause game in this menu | On (default): the game pauses while the menu is open. Off: the game keeps running |
| Menu opacity | `clear, 25%, 50%, 75%, 85%, solid`. Default 85% |
| Menu position across | `left, centre, right`. Default centre |
| Menu position down | `top, middle, low`. Default middle |
| Trainer log length | How many results the trainer log shows `off, 5, 6, 8, 10`. Default 5 _(see [Trainers](Trainers.md))_ |
| Trainer result hold | How long a trainer's panel keeps an attempt's result `off, 1 s, 2 s, 3 s`. Default 1 s |
| Hi Scores screen shows | `practice bests` or `game records` |
| Clear practice best times | Forget every practice best time |
| Save best times to card | Write the practice best times to the SD card file `BESTS.GRB`. WIP: SummerCart64 only |
| Load best times from card | Replace the practice best times with that file |
| Save file | _Sub menu_, see below |
| Controller Pak | _Sub menu_, see below |

Saved settings load when the console starts. Your game save isn't touched

The Game menu toggles (noclip, moon jump, camera speed, chaos, free cam, ghost and trainers) aren't saved to the cartridge. You can keep the trainer toggles on a Controller Pak, see below

The trainer log length and result hold are saved with the other settings

Long menus (cheats, HUD layout) scroll

## Practice best times

Finish a level the normal way, or in a Time Trial, and your fastest in-game time is kept, with hundredths

The game's Hi Scores screen shows them on the world pages. Open it from the title: Options, then Hi Scores

With the timer or the last level time on the HUD, the end of a level shows if it's a new best

Best times are lost when the console is switched off, unless you keep them on a Controller Pak or on the SD card _(see [Savestate](Savestate.md))_

A run doesn't count if it used any of these:

- noclip, moon jump or chaos inputs
- a camera speed other than `normal`
- memory edits, set action, spawn, move or edit actors
- go to checkpoint, or "Last checkpoint" where the pause menu wouldn't offer it
- a power-up from the menu, or changing lives or health during the level
- a warp with Lives, Garibs or a Power-up set _(see [Warp](Warp.md))_
- a frozen or changed rng seed
- controller 2's debug controls
- a cheat other than level select, open portals and open levels
- frame advance, slow motion or a savestate load

## Save file

Edit the game's own save. Edits your real save!

- For each of the six save files: lives, crystals won and returned, and difficulty
- For each level: done, all garibs, and best time (whole seconds). All save files share the best times

Edits stay in the editor until you choose "Write to the cartridge", which asks once more. "Reload from the cartridge" drops your edits

Note: the editor refuses a save with bad checksums

## Controller Pak

Put a Controller Pak in controller 1 or 2, and choose the controller in the first row

- "Save to the pak" keeps the settings, the Hi Scores choice, the trainer toggles, your best times and, with "Save the recording too" on, the input recording. They go in one note named "GLOVER PRACTICE"
- "Load from the pak" brings them back. A recording only plays on top of the savestate it was recorded from
- "Delete the practice note", "Repair the pak" and "Format the pak" do what they say. Format only works on an unformatted pak

Save, Delete, Repair and Format ask first: A confirms, any other button cancels

Other games' notes are never changed. A Rumble Pak doesn't work. Saving and loading can lag the game for a moment

Note: the pak code was only tested in an emulator
