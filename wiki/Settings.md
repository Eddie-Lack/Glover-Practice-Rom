# Settings

_Back to the [Home page](Home.md)_

| Option | What it does |
| --- | --- |
| Save settings | Save the settings to the cartridge. Changes you don't save are lost when the console is switched off |
| Load settings | Load them |
| Default settings | Go back to the defaults. Hold A. Your saved settings stay on the cartridge: Load settings brings them back |
| Status | `saved` or `loaded` while the settings match the cartridge's. It goes blank once you change a setting, and is blank before the first save or load. `none saved`: Load settings found none. `no EEPROM`: see below |
| Pause game in this menu | On (default): the game pauses while the menu is open. Off: the game keeps running |
| Menu opacity | `clear, 25%, 50%, 75%, 85%, solid`. Default 85% |
| Menu position across | `left, centre, right`. Default centre |
| Menu position down | `top, middle, low`. Default middle. The position moves the list pages, not the tools that fill the panel, like Memory or Save file |
| Trainer log length | How many results the trainer log shows `off, 5, 6, 8, 10`. Default 5 _(see [Trainers](Trainers.md))_ |
| Trainer result hold | How long a trainer's panel keeps an attempt's result `off, 1 s, 2 s, 3 s`. Default 1 s |
| Hi Scores screen shows | `practice bests` or `game records` |
| Clear practice best times | Forget every practice best time. Hold A |
| Save best times to card | Write the practice best times to the SD card file `BESTS.GRB`. SummerCart64 only |
| Load best times from card | Replace the practice best times with that file. Hold A |
| Save file | _Sub menu_, see below |
| Controller Pak | _Sub menu_, see below |

Saved settings load when the console starts. Your game save isn't touched

Menu opacity also sets the background of the tools' panels, like Face info in the collision view

`no EEPROM`: the ROM saves to a 4 kbit EEPROM, like the original game. If your flash cart or emulator doesn't give the ROM one, set the save type to "EEPROM 4K" by hand in its settings

## What is kept

| What | Kept by |
| --- | --- |
| HUD items and their places, Text shadow, Timer in frames too, Input history, the full-game timer's splits | Save settings |
| Bindings, Ball controls, the Reset kind (Warp), Pause game in this menu, the menu's opacity and position, the trainer log length and result hold | Save settings |
| Controller 2: debug controls, and Controller 2: L marks all done (Game) | Save settings |
| The trainer toggles, the Hi Scores choice, the practice best times, the input recording | A Controller Pak note, see below |
| Savestates, the input recording, the practice best times | Card files on a SummerCart64's SD card _(see [Savestate](Savestate.md))_ |
| Everything else: savestates in the slots, ghosts, cheats, noclip, moon jump, camera speed, chaos, free camera, fog, mute, the random numbers, the controller display, the collision view, hidden objects | Nothing: they are lost when the console is switched off |

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

From the first of these, the HUD shows MODIFIED until the level starts again. A cheat typed in the game's own menu doesn't show it

## Save file

Edit the game's own save. Edits your real save!

- For each of the six save files: lives, crystals won and returned, and difficulty
- For each level: done, all garibs, and best time (whole seconds). All save files share the best times

Edits stay in the editor until you choose "Write to the cartridge", which asks once more. "Reload from the cartridge" drops your edits

Values you edited but didn't write yet are orange. An empty save file's lives, crystals, difficulty and level flags are grey and can't change; the best times still can

The question takes A only after half a second, so a double tap doesn't write by mistake. Any other button says no. After a write, "Waiting for the game's own save..." shows until the game's EEPROM is free

Note: the editor refuses a save with bad checksums

## Controller Pak

Put a Controller Pak in controller 1 or 2, and choose the controller in the first row

- "Save to the pak" keeps the settings, the Hi Scores choice, the trainer toggles, your best times and, with "Save the recording too" on, the input recording. They go in one note named "GLOVER PRACTICE"
- "Load from the pak" brings them back. A recording only plays on top of the savestate it was recorded from
- "Delete the practice note", "Repair the pak" and "Format the pak" do what they say. Format only works on an unformatted pak

Save, Delete, Repair and Format ask first: A confirms, any other button cancels. The question takes A only after half a second

A grey row can't act on this pak. A on it checks the pak, and the status line says why

Other games' notes are never changed. A Rumble Pak doesn't work. Saving and loading can lag the game for a moment

Note: the pak code was only tested in an emulator
