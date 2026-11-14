# Testing

## The emulator harness

`tests/harness.py` runs a ROM in mupen64plus under script control. It builds on gloveworld's scripted emulator
(`gloveworld/tools/emu/`): run its `setup.sh` once, which installs mupen64plus, builds angrylion's software
renderer and the input plugin, and starts a virtual display on :99.

```sh
python3 tests/harness.py TEST [--rom ROM] [--elf ELF] [--no-pak] [--gfx rice|angrylion] [--features off|on]
```

A test is a module in `tests/` with a generator `run(emu, sym, args)`. Each `yield N` lets N video frames pass.
Between yields it holds buttons, reads and writes RDRAM, and takes screenshots (see gloveworld's `emu.py`).
`sym(name)` gives an address from the ROM's ELF, so one script runs on the retail ROM and the practice ROM.
`--no-pak` turns the Expansion Pak off. A test module with `TEST_BUILD = True` runs on the test build
(`build/test/glover-practice-test.z64`, from `make testrom`), which has the SD card test mailbox; without `--rom` and `--elf`
the other tests run on `build/glover-practice.z64`.

| Command | What it checks |
| --- | --- |
| `make test` (`tests/run.py`) | all of the tests below except the page test; `--quick` skips the faithfulness test |
| `python3 tests/romlayout.py` | the ROM is 8 MiB with a right header checksum; the retail ROM is zero after its data and no game symbol names a ROM address past it; the loader and the practice segment in the ROM are the ELF's, with zero bytes around them; a link with a ROM one byte too small fails with a message, one that just fits links. No emulator |
| `harness.py boot --gfx angrylion` | the practice segment loads, `PracticeInit` runs, the version line is in the frame buffer |
| `harness.py nopak --no-pak --gfx angrylion` | without the Expansion Pak the message is in both frame buffers and the game never runs |
| `harness.py menu --gfx angrylion` | all four C buttons open the menu, the game pauses and sees none of the input, B closes it |
| `harness.py hud --gfx angrylion` | every HUD item on in Atlantis 1: the timer is drawn and counts, the input layer reads the buttons and the stick; writes `hud.png` |
| `harness.py tools --gfx angrylion` | the menu on the title flythrough; warp from the title; reset, frame advance and frame step, last checkpoint through the default bindings; a cheat through the menu |
| `harness.py settings --gfx angrylion` | the menu sets Normal ball controls, the L switch, Menu opacity 25% and the menu at the right and top, and saves: the EEPROM file holds the settings in blocks 56 to 63, with the menu look in the bytes of two HUD slots |
| `harness.py settingsload --save-from build/tests/settings/save` | the saved settings, with the menu look, load at boot |
| `harness.py settingsold --save-from build/tests/settings/save` | a block with the menu look's bytes zeroed (as an older ROM wrote it, with a right checksum) loads, and the menu look is the default |
| `harness.py menupages --gfx angrylion` | every menu and sub menu (25 pages, 31 checks with the ends of the six scrolled lists, the Splits page with 20 splits written into `gameTimer`; found by walking the `ITEM_SUBMENU` items with the menu's own input; `tests/menunav.py` reads the menu tables from RAM): the panel ends at least 12 pixels above the bottom, every listed row (at most 14) and the hint line are in the frame buffer, a longer page scrolls to its last item; the Splits page when its count drops to 3 and to 0 (cursor and scroll stay inside; it has no empty rows); Menu opacity: with each of its six steps the panel is nearer its solid colour; Menu position: the four edges of the panel at each of the nine places, and Warp and Game (14 rows) at the lowest place (`menupages_low.png`) |
| `harness.py savestate --gfx angrylion` | in Atlantis 1 with the route's random input: a save asked for on game frame 149 is made on game frame 150, where the input changes, and loaded on that game frame again: 200 game frames, the same state on every game frame; a second load stays in the level; a warp to a level with another song and a load from there: back in Atlantis 1, with its song; a save on the title is refused |
| `lzhost.py` | the savestate compressor and hash built for the host with the address sanitizer: round trips of made-up data (zeros, random bytes, records, runs, shifts, sparse changes) of ten sizes; no write past a limit, and a limit at the exact size of the output is enough; streams that ask for literals or a match past the block's end or an offset before its start are rejected, and cut or damaged streams write nothing outside the block; `LzHash` differs where the plain sum is equal (+1, -2, +1, swapped words, a move between words) |
| `fathost.py` | the FAT code (`practice/src/fat.c`) built for the host with the address sanitizer and run by `tests/fathost.c` on 43 cases (the disk images are mapped, so sparse images of 2 GB work): FAT32 and FAT16, with and without an MBR, made by mkfs.fat (if installed) and by the formatter of `tests/fatimage.py`; clusters of 0.5 to 32 KB; scenarios `basic` (a folder, 100 files of up to 700 KB, larger, smaller and empty replacements, deletes, a file made again, a file in the root, a folder deleted as a file refused, a long name refused), `full` (files until the disk is full, a replacement that does not fit leaves the old file, a replacement after a delete works), `fragment` (scattered free clusters, a 3 MB file over them), `powerfail` (the device stops at every write of a replacement and of a new file: after each stop the file is the old one or the new one, whole, and the other file is whole) and `transient` (each write of a replacement and a new file fails once, the others work: the old or the new file is whole, the other file is untouched, no two files share a cluster, no cluster of a file is unmarked). Images that other programs would leave (built by `fatimage.Builder` before the code runs): long-name entries, deleted entries, a volume label, a folder with a file, a full FAT16 root (a folder is refused with "no room" and leaves no lost cluster; a folder that is there takes files), a FAT32 root of several clusters whose last cluster is full (the folder needs a new one), the volume as the second partition behind a Linux one, FSInfo blocks with a hint into the used clusters, beyond the volume and in the middle of scattered files, a FAT32 volume whose second FAT is the only one in use (the first is stale and must stay as it is), a folder entry with cluster 0 and a folder whose chain loops (both reported, nothing hangs), no scratch memory and 3 sectors of it. The first save on a populated 2200 MB card (3200 files, 300,000 clusters in use) prints the sectors and commands it took. After each run a reader written apart from fat.c (`tests/fatimage.py`) checks every file's content, what was there before, the folder's list, equal FAT copies (when mirrored) and no lost cluster, and `fsck.fat -n` must be clean (when it is not installed, a SKIP line is printed and the reader's checks still run). No emulator |
| `harness.py slots --gfx angrylion` | savestate slots with a steady stick: three states in Atlantis 1 (a base of about 0.6 MB, then 9 and 10 KB each; the third has words changed so that the plain sums of their chunk stay equal, and loads back with them), two in level 20 and one in level 30 (a base each); each save waits a frame to show "saving..." and is decoded again at once against the game's memory, chunk by chunk of every kind (`stateVerify`; no word outside the audio code's buffers may differ); every slot loaded in a mixed order gives its level and the same game frames; a save into a used slot in another level; clearing two slots in the menu frees a base and moves the states behind it, which then load as before; ten slots filled; a fourth level takes the fourth base (`STATE_BASES` is 4); a fifth level, when the four bases are taken, does not fit (the pool is 2837 KB); a save that does not fit says "pool full, slot 7 emptied" and then "pool full (slot 7)"; sizes and times printed |
| `harness.py instantreset --gfx angrylion` | the instant reset: with the setting off nothing is saved at a level start and Reset goes through the exit; the Reset item in the Warp menu turns it on; the first Reset (no state yet) is the usual one and the new level start saves the state; three instant resets with no fade (the game stays in the level, no exit flag) give the level's first 60 game frames again (same random seed, timers, places) and save nothing; a second real start in the level saves its state without compression (up to 320 KB) and the reset to it is the same; the game's copy of the save, the best times, the CRCs and the Hi Scores table survive a reset with other content than the state's; a reset in a pause or while Glover dies is the normal one; a start whose state was not saved does not load an older one; level 20 too; the setting is in bit 15 of the saved block's `hudOn` and loads again; off again, Reset goes through the exit |
| `harness.py recording --gfx angrylion` | Record with the route's random input for 150 game frames in Atlantis 1, then Play with nothing held: the same state on every game frame. A recording whose count is set to three below `RECORDING_MAX` (36000, 30 minutes) stops at the limit with "recording stopped: 30 minutes is the limit" and writes nothing behind the buffer's last game frame |
| `harness.py gametools --gfx angrylion` | the Game menu in Atlantis 1: a bowling ball from Power-ups, one more life, the frozen random numbers, one step of them, a level's start, and a set through the memory view |
| `harness.py gameswitches --gfx angrylion` | the Game menu's two switches in Atlantis 1. With the debug controls on and the L switch off, L written into `controllerdata[1]` (the emulator has no controller in port 2) reaches `input` but not `pad[4]`, and the save slot stays as it is; with the switch on, `pad[4]` has L and the slot holds every level and garib (the test puts it back and ends before the game can save). The Fog switch: with it off, the frame's display lists have no fog colour command and no fog geometry mode, `fog.on` stays 1 between frames; back on, both are in the lists again |
| `harness.py paddisplay --gfx angrylion` | the HUD menu's controller display in Atlantis 1. Off: the level starts without its overlays (`controllerOL[0]` has no bitmaps). Controller 1 and a reset: the overlays are set up, `printController` stays 0 and the picture changes; A held lights the A picture (255) and leaves Z and C-up dim (100); the stick moves the stick picture right. Controller 2: C-up in `controllerdata[1]` lights C-up, A on controller 1 does not light A. Off: nothing lights, and the next reset leaves the overlays freed |
| `harness.py gametimer` | the full-game timer starts when `toStartGame` becomes 1 and stops at the last crystal's cave exit (written into RAM) |
| `harness.py inspectview --gfx angrylion` (renamed from `inspect`, which hid the Python module of that name) | the actor list and its details page open in Atlantis 1 (`actors.png`, and `actor0.png` and `actor1.png` from the two frame buffers), and A there moves Glover above the actor; the memory view adds `timer321` to the watch list and changes it; Glove and ball opens and turns to its second page (`glove0.png` and `glove1.png`, `ball0.png` and `ball1.png`, from the two frame buffers) |
| `harness.py collview --gfx angrylion` | the collision view's cache in Atlantis 1, the whole level: it is built over a few frames (`CollStats.state`); every face that the RSP would draw for a landscape actor, worked out from the cache's vertex loads, triangles and the actor's matrix, is one of the game's `CollFace` triangles within 1 unit, and every game face that is not degenerate is drawn; the faces drawn stay the same over 60 frames while Glover stands still; each platform matrix in the frame's scene is at its platform's place; a radius of 250 draws fewer faces, and leaving out floors, walls, ceilings, landscape or platforms draws fewer; the game goes on while Glover walks, see-through and on surfaces, through every colour and back face mode (`see1.png`, `surf1.png`) |
| `harness.py collview2 --gfx angrylion` | the collision view's switches in Atlantis 1, read from the lists it builds: the mallet's spheres in the enemy colour; open edge lists with pink vertices, called from the scene, and none drawn with the switch off; back faces "faint" draws the scene twice (the second time with the front faces culled), "hide" once with back faces culled, "same" once with no culling, in both views; colours by kind give at most 3 colours, by object at least 6; the cache is built again after a reset of the level and after a warp to Atlantis 2 (`enemies1.png`, `edges1.png`; the other frame buffer is often a frame being cleared) |
| `harness.py collinfo --gfx angrylion` | face info and the wireframe in Atlantis 1. Face info alone draws the practice text on every frame; the glove's face is one of the faces it touches (`ActorObject.planes`), its actor holds it at the number given, and its terrain, normal and kind are the face's; the face at the screen's centre is the nearest that the frame's ray meets, worked out by the test over every face of every collision actor (no bounding spheres), at Glover's start and at three other places; in the air the glove's face is kept as an earlier frame's. The wireframe: off draws the scene twice in the faces' render mode (faint back faces), "over faces" adds a third pass in the outline mode, "alone" draws only that pass, on surfaces with Z compare; every render mode is set with shift 3 and length 29 (`info1.png`, `wire1_1.png`, `wire2_1.png`) |
| `harness.py overlay --gfx angrylion` | the overlay list's peak use in Atlantis 3, Carnival 1, Pirates 3 and Out of this World 3 with every HUD item, face info and every collision view switch on, and the menu open on 19 pages and views (`Practice.overlayPeak`): under 80% of `OVERLAY_SIZE`, and the list is never cut (`Practice.overlayFull` is 0); then a combined frame with every trainer and the frame log on, which must leave 150 commands free |
| `harness.py hide --gfx angrylion` | Hide objects in Atlantis 1: with nothing hidden the practice code notes nothing; each kind on its own turns drawing commands into no-ops and the game goes on (the landscape, the glove, the ball, the garibs, the shadows and the HUD must hide something at the level's start; the others are printed); every kind at once leaves no triangle and under half of the list calls in the 3D objects' task (the fade and star transition stay), the game goes on while Glover walks, and the run no longer counts for a best time; the frame comes back after; the menu's Glove item toggles its bit and Show everything clears all (`land0.png`, `all0.png`, `all1.png`) |
| `harness.py actors --gfx angrylion` | Spawn and move actors in Atlantis 1: a garib through the menu (it joins the garib group and `numGaribs` goes up by one); every other kind in the view's list (power-ups and enemies): one new actor of the right type appears about 70 units from Glover and the game goes on; an enemy moved with Z from the actor list; a platform with no motion moved (its flag is dropped and its first face's corner moves with it); the MODIFIED marker is not drawn before the first use, is drawn after it, and a level restart clears it (`actors_*.png`, each with `_0` for the other frame buffer) |
| `harness.py missedtaps --gfx angrylion` | Show missed taps in Atlantis 1, with pulses of A (below): off, the game sees all 30 and the tool reads nothing; on, the tool takes nearly all of them (seen + missed = 30), makes about ten reads a game frame, skips none, the game keeps its pace, a held A is seen as usual, and the display counter is set; off again, the reads stop |
| `harness.py chaos --gfx angrylion` | Chaos inputs in Atlantis 1: off, nothing counted and no marker; "rare" with a drop and an added press set by hand (the game does not see the dropped A; it sees the B that the test never pressed, and `input` does not); "often": within 1,200 video frames the counters show added and dropped presses and nudges, the game saw presses the test never made, the marker is drawn and the level runs; while a recording is made (set by hand) the counters stand still; off again, the counters stop |
| `harness.py corrupt --gfx angrylion` | Memory corruption in Atlantis 1: off, nothing running and no marker; in the memory view Z marks timer321's word and Start, right, C up and down, R and L change the poke; a frozen word that the test clears is written again, the marker is drawn, and it stays after a reset of the level until "Clear all pokes"; "Apply once pokes" writes a word and then a byte over it, a state load clears the marker, aligns an address, and leaves pokes on the practice segment, below RAM and past RAM unwritten; random changes only the low 8 bits, flip only the low 4 bits, every game frame and every 5th; with the game stopped (frame advance), "Flip one random bit" for the glove, the ball, the level's actors and a 16-byte range changes exactly one bit (never in an actor's bytes 0x10 to 0x1B, which the draw thread rewrites), the one the menu reports (`poke_page.png`, `corrupt_menu.png`); "Clear all pokes"; the game keeps running |
| `harness.py demos --gfx angrylion` | the game's demo recordings (Warp menu): Watch demo from the title and from a level (autoPlayback 2, slot 6, a recording loaded, the glove moves), the practice menu in a demo, A leaves, Go in a demo starts a normal level, Watch all demos plays the eight levels in order to the title (the test sets `exitCount` to end each one through the game's own code), the hi-score table stays as it was, and A stops Play all |
| `harness.py titlemenu --gfx angrylion` | the early title menu: opened from the title flythrough (mode 1 over level 7, `TitleScreen` called every game frame, `early1_*.png`), back to the normal title (`normal1.png`), opened from Atlantis 1 through the Quit exit, its third entry (stick down twice, A) gives the level select and back works from there too, a warp from the normal title menu, A on its first entry starts a level (`continue1.png`), a warp from the early title (its texts leave the text list), a second request during the exit is refused, and a state loaded during the exit abandons it |
| `harness.py freecam --gfx angrylion` | the free camera in Atlantis 1, flown by writes to controller 2's `controllerdata` entry: off, no free view and no text; on (speed 8) it starts at the game's camera, the stick moves it 8 units a controller read along its view direction, `camera`, `actualCamPos` and the glove stay put and the game runs, the camera matrix of both frame buffers (decoded from `dynamic`) differs from the game's by more than 0.5; C right turns the view to the right, C up and C down tilt it, D-pad up lifts it, R is four times faster; in frame advance the game's `frameCount2` stands still and the camera still moves; off, no views are built, the note is gone and the matrix is the game's again (within 0.05: the water sways the game's up vector). A quick-action binding on controller 2 (A, hide the HUD) does not fire while the camera is on, and fires with it off. `freecam_*.png` show the four stages (`_0` and `_1` for the two frame buffers) |
| `harness.py ghost --gfx angrylion` | the ghost in Atlantis 1: a recording of 150 game frames with the route's random input keeps a place for each game frame, which matches the glove and the ball of the live run (within 1.8 units); with the switch off, loading the state draws nothing (no draws, no list); the switch on through the Game menu, the state loaded and the glove held still: a ghost is drawn on every game frame, at index `frameCount - start`, at the stored place, and runs 740 units away from the still glove; its list has 41 commands (4 matrices, 2 vertex loads) and the game's list calls it; played back, the ghost lies on the live glove (0.4 units on average, at most 0.7) and on the ball; past the end and with the switch off nothing is drawn; a second recording replaces the level's ghost. `ghost_*.png` |
| `harness.py framelog --gfx angrylion` | the frame log in Atlantis 1: one entry per game frame of the glove, in order; A held on consecutive entries and pressed on the first; the action changes after the press; it reports the controller reads between two frames of the glove; the view opens (`log1.png`) |
| `harness.py saveedit --gfx angrylion` | the save file editor: from the title, Settings, Save file; file 1's lives, crystals won and returned, the first level's done flag, all-garibs flag and best time are edited in the editor's own copy (the game's copy and the EEPROM file are unchanged until Write, also after a cancelled question); lives start at 3, crystals returned stop at crystals won; the difficulty of the file in play (`currentSaveSlot`) does not change in the editor, and a write keeps it when another file was in play at the edit; Write merges the edits into the game's copy and puts the edit into blocks 31 to 51 and 7 to 30 and every CRC (six files, hiscores, best times; CRC-32, polynomial 0x04C11DB7, no inversion) is valid, and blocks 56 to 63 are untouched; Reload asks first, drops an unwritten edit and puts the cartridge's data into the game's copy; with the game's request queue busy, a reload waits and a second question is refused. `file_page.png`, `levels_page.png`, `confirm.png`, `written.png` |
| `harness.py saveeditload --save-from build/tests/saveedit/save` | run after `saveedit`: the game's boot accepts the edited save (it blanks a file with a wrong CRC) and file 1 holds the edit in RAM |
| `harness.py saveeditbad --save-from build/tests/saveedit/save_bad` | run after `saveedit`: a save with a wrong CRC in file 2. The game blanks file 2 at boot. With the game's copy of file 1 and of the first best time changed, Reload (asked and confirmed) reads the cartridge, finds the wrong CRC and sets status 4. The game's copy, the CRC table and the editor's edits stay as they were, and no request stays pending |
| `harness.py besttimes --gfx angrylion` | the practice best times: a fresh boot's Hi Scores page is the game's (its five times in "m:ss"); Atlantis 1 and 2 finished through `newLevExitTimer` (the game's own `ExitLevel` with the complete flag) keep their times in game frames, the same count as the timer; a slower run keeps the old time, a faster one replaces it; a Hard run (a Time Trial) is noted both when it beats the game's record (name screen) and when it does not (title); Go to checkpoint, Lives and Health taint the run; a noclip run and a run with a state load are not kept and the reason bits show; a normal run after them is kept; the game's own `bestTimes` follows its rules (whole seconds), `hiscoreTable` is unchanged, EEPROM blocks 56 to 63 stay erased; the Hi Scores page shows the practice times with hundredths and dashes, the game's table with the setting on game records, and the game's table again after the clear (menu); showing the screen leaves the game's records as they were. `hiscores_*.png` |
| `harness.py cpak --gfx angrylion` | Controller Pak storage, a save to a fresh pak. The emulator keeps a pak in controller 1 (`PAKS = "1"`) and formats it. Settings, Controller Pak: the check finds no note (123 free pages, 16 free notes); a save asks first and a decline moves no block; A writes it. The .mpk file is decoded in Python (`pakfile.py`, from the public layout): valid ID blocks, two equal index tables with the right sums, one note "GLOVER PRACTICE", and in its data the settings, the Hi Scores choice, the trainer switches, the 30 best times and a recording of 616 frames, all equal to what RAM held. `cpak_view.png`, `cpak_confirm.png`, `cpak_saved.png` |
| `harness.py cpakload --save-from build/tests/cpak/save` | run after `cpak`: a new emulator boots with the default settings and loads the note from the Controller Pak view; settings, HUD place, binding, best times, Hi Scores choice, trainer switches and every frame of the recording are those saved, and the .mpk file is unchanged |
| `harness.py cpakforeign --gfx angrylion` | other games' notes: two foreign notes on scattered pages. A save without the recording (1 page), a save with a recording of 2800 frames (many pages), a save without it (the note shrinks and its pages are free again) and a delete leave every foreign note's table entry, pages and index entries byte for byte as they were, and the free page count is right each time. Then a save, a change and a load on controller 2 (read back through the driver) |
| `harness.py cpakedge --gfx angrylion` | cases that must not write: an all-zero pak in controller 1 is "not formatted", a save writes nothing, a format asks first and a decline writes nothing; a confirmed format passes the checks of `pakfile.py` and a save to it works; Format does not ask on a valid pak. Controller 2's pak has 16 notes: a save says "no room", a load "no note", and the pak is unchanged |
| `harness.py cpaklong --gfx angrylion` | a good note whose recording has 36001 game frames (one over `RECORDING_MAX`): Load gives "The recording is longer than 30 minutes. Nothing was loaded."; the settings, the recording's count and its buffer (a marker word behind the limit and the first words) are unchanged |
| `harness.py cpakmax --gfx angrylion` | a good note whose recording has exactly 36000 game frames loads: every game frame is in the buffer and the marker word behind the last one stays |
| `harness.py cpakfull --gfx angrylion` | little room: controller 1's pak has 2 free pages. A save with a recording that does not fit leaves the recording out and takes 1 page; one with a short recording takes 2. Controller 2's pak has no free page: "no room", unchanged |
| `harness.py cpakbad --gfx angrylion` | a practice note with a wrong sum: the check finds it, a load says "damaged" and the settings stay |
| `harness.py cpaknone --gfx angrylion` | no pak: no command is sent until the view is used (`cpak.commands` and `cpak.transfers` are 0 after 600 frames); then the check and a save say "no Controller Pak", each with one status command and no data block |
| `harness.py cpakid --gfx angrylion` | the first of the four ID blocks is damaged: the pak counts as formatted, the check offers a repair and no format, a save works and keeps the other note, the repair asks first and then writes the good block to all four places |
| `harness.py cpakindex --gfx angrylion` | the first index table is damaged and its backup is good: the check reads the backup and offers a repair; a save works and writes both tables, and the strict checks pass |
| `harness.py cpakcross --gfx angrylion` | the practice note shares its pages with another game's note: save and delete refuse and the .mpk file is unchanged |
| `harness.py cpakdupe --gfx angrylion` | two practice notes: save and delete refuse and the file is unchanged |
| `harness.py cpakrumble --gfx angrylion` | a Rumble Pak (`RUMBLE = "1"`): the check, a save and a format say "not a Controller Pak"; no format is offered |
| `harness.py cpakfail --gfx angrylion` | a save cut after 16 different numbers of blocks (`cpak.failAt`, the pak "pulled"): after each cut the file is a valid pak, the other note is unchanged and the practice note is intact, the old one before the entry write (block 61) and the new one after it; then a load and a save work |
| `harness.py cpakfailfull --gfx angrylion` | a save over the note's own pages (5 free pages, a note of 3 pages, a new one of 5) cut while it frees the old pages: "the note may be damaged", a valid pak (the backup index table stands in), the other note unchanged, no practice note; the check offers a repair, and after the repair the strict checks pass |
| `harness.py sdcard --gfx angrylion` | savestates on the SD card, with a test mailbox in place of a cart (the test build, `make testrom`; `practice/src/sd.c`; `tests/sdutil.py` answers it from a 64 MB FAT32 image in a thread, because a save stops the picture and the harness's frame callback with it). With no cart: Check card says "no cart" and the mailbox is not used. With the mailbox: no card, a card of zeros and a formatted card with no folder are told apart, and no sector was written. Three states in Atlantis 1 (one with words changed that keep the chunk's sums) and one in another level go to the pool; the first save to the card goes through the menu (the progress bar is seen in a frame buffer, 357 times from 0 to 234 pixels); files 1, 42 (twice: replaced) and 100 and a file 7 that the menu deletes; Check card through the menu lists 1, 42 and 100 with their levels. The image is read apart from the ROM: the folder, the sizes, the header, the CRC, every record decoded by the reader's own decoder of lz.h's format (3.10 times smaller), file 1 holds the original words of the test and file 42 the changed ones, `fsck.fat` clean (SKIP line if it is not installed). With the game stopped, a state is saved to file 50 and decoded: every word that did not change between two snapshots of the game's memory is the file's. `sdcard_menu.png`; writes `traces.json` for the next test Open problem: on 2026-10-23 the check "the file differs from the game's memory" fails on the draw thread's matrices (`D_802848AC`, `D_80284D74`), also on the commit before the boss trainer. |
| `harness.py sdcardload --gfx angrylion` | run after `sdcard`: a new emulator in another level loads files 1, 42 and 100 and file 1 again; the level is right and the game frames after the load equal those that the first run saw after the save (as `slots.py` does), file 42's changed words are back. A file that is not there says so. A file with one byte changed is refused and the game goes on. So is a file with a wrong magic, version, header size, layout hash, chunk count, state size or level (the CRC made right again) and a file of 40, 67 or 5000 bytes. A load through the menu. No sector is written by any of it |
| `harness.py sdcardfull --gfx angrylion` | an 8 MB FAT16 card: files are saved until it is full ("the card is full"); the saved files decode and the failed save left no lost cluster; a replacement on the full card fails and leaves the old file; after a delete it works |
| `harness.py sdcardbig --gfx angrylion` | the first save to a populated 2200 MB FAT32 card (`fatimage.make_large_card`: 3200 files, a 1.2 GB file in front, 4 KB clusters): the file is whole, no lost cluster, `fsck.fat` clean; the sectors and commands asked for and the console's own time are printed ([build.md](build.md#savestates-on-the-sd-card) quotes them) |
| `harness.py sdrecording --gfx angrylion` | a recording and the best times on the SD card (test build). In Atlantis 1 a recording of 120 game frames is made with real inputs (stick forward, then forward and right), played once (the game frames are kept), the best times of three levels go to BESTS.GRB through Settings, and the recording to RECORD.GRC through Savestate, SD card. The reader of `sdutil.py` decodes RECORD.GRC (header, CRC, 145 chunks, the runs) and finds the game's input; BESTS.GRB has its magic, three times and a good CRC; `fsck.fat` clean. Writes `recording.json` |
| `harness.py sdrecordingload --gfx angrylion` | run after `sdrecording`: another emulator, another level. A BESTS.GRB with a changed byte is refused, the good file brings the three times back. A RECORD.GRC whose header says 36001 game frames (over the limit) is refused before the game changes, with "the recording file is longer than 30 minutes: not loaded"; the buffer is untouched. A RECORD.GRC with a changed byte is refused and the level stays. The good file loads through the menu: Atlantis 1, 120 game frames, the same input, and Play recording gives the first run's game on each of the 119 game frames. The card code writes no sector |
| `harness.py warpgive --gfx angrylion` | Warp with lives, garibs and a power-up: from the title (7 lives, 12 garibs, the power ball), from a level (2 lives, 3 garibs, speed up, Atlantis 2), then a plain warp that changes nothing and does not mark the run modified |
| `harness.py actoredit --gfx angrylion` | Inspect, Actors, an enemy's page, Start: pos x with steps of 1, vel x with steps of 10 and 0.1, the state's hits and mode, the run marked modified, and Start again leaves the editing |
| `harness.py trainers --gfx angrylion` | the trick trainers in Atlantis 1 (`TRAINERS_ONLY=pad,quiet,lmash,mbz` runs some of the checks): the pad at the hook against the pad at `ProcessInput` over 1,000 game frames of random presses (below); off, no hook work and no text; noclip and moon jump make the view empty; L mash patterns (2,2; 3,2; 2,3; 4,2; random; the straight D-pad) with the verdict of every press compared with the one computed from the frame log, MOVE equal to the ball's displacement, AVG20, PEAK and TARGET (the model's 24.92 for the diagonal D-pad, 22.00 straight, 25.50 in the air), the warnings; MBZ-L from the ground with Z armed (B on time, 1 and 2 late, Z late, Z fresh on 4, no Z, L on 5 and on 6, Z held; A on time, Z fresh on 3, L late): verdict, aim and cancel frames, speeds 7.00 to 10.43, 9.09 and 8.01; B from a bounce (the glove holds the ball, B tapped, the ball flies at 8): 8.00 to 14.00, an L 1 late 13.68, an L on frame 5 lost. `trainers_lmash.png`, `trainers_mbz.png` |
| `harness.py trainers2 --gfx angrylion` | the line keeper in Atlantis 1: with random C taps on a diagonal D-pad, E, E + 2 Edot and the advice agree with `tools/tricks/dpad_trainer.py` on every frame (to 2e-7, 0 advice differences on 53 frames; the C buttons are the ones the frame log says the game read); walking with no C against walking with the model's taps (drift 0.0242 against 0.0002 rad a frame, 24 against 3.8 units of bend). `trainers2_line.png` |
| `harness.py trainers3 --gfx angrylion` | the wall clip in Atlantis 1. One face of the start floor's actor is rewritten as a wall at x = 100; with the ball bearing (R presses), A x4, Z and R on the next frame, the trainer's cancel distance is the ball's distance after the frame before, 17 cancels from D 14 to 28 give the game's result (clipped up to 23.53, not from 24.03; the trainer's top is 23.64), R at +1, the live line follows the ball and names no wall 200 units away, another ball gives no line, noclip is quiet; the scan took at most 134,720 counts. `trainers3_clip.png` |
| `harness.py trainerlog --gfx angrylion` | the trainer log and the result hold (`TRAINERS_ONLY=mbz,lmash`). MBZ-L: two attempts, the second started during the first one's hold (3 s): the panel keeps the first result while the second runs, the second's end replaces it and moves the end of the hold to 60 frames after its own end (one hold, not two), the log has both lines newest first, after the hold the panel follows a new attempt, and with the hold off nothing is held. L mash: 11 runs of 5 presses give 11 writes and 10 lines (`LM 5P 4 ON TIME 0.0 GOOD`), the panel's strip stays still for the 20 frame hold, a savestate load keeps the log, the number of lines drawn (counted in the frame buffer by the lines' green) is 0, 5, 6, 8, 10 for the five settings and 5 with the MBZ-L panel on, and turning a trainer off takes its lines out. The other trainer tests check that their own lines appear (`LK`, `MP`, `BW`, `WC`). About 1 minute. `trainerlog.png` |
| `harness.py collholes --gfx angrylion` | the hole painter at the hub's Space portal: with the view on and the painter off nothing runs or is drawn; with it on the list is the model's candidates (`tools/tricks/cone.py` over faces read from RAM) within 72 units of the glove, 16 of 16 with no extra, and the tip is one; 150 units away the list follows (1 of 1); the view draws a post for each (and violet pixels are in the frame buffer); the longest step is under 140,000 counts (103,990 measured); off with the switch and with the view. `collholes.png` |
| `harness.py trainers_moon --gfx angrylion` | the Moon Pound aim at the hub's Space portal (level select to hub level 1): the strip, column, hole centre and distance on 11 jump frames against `moonpound_trainer.py` and `cone.py` (0 of the cells that were not left grey for time differ, the vertex is the dome's tip; a grey cell must come with the slow flag); a savestate run in which the glove moved by the aim's shift and Z pressed on the cell that the model names falls through the tip, and the same without the shift lands; grey cells (A held, action lock 5), silent in water and with Hercules, nothing computed when off; the stick advice moves the column within 30 percent (14 measured). `trainers2_moon.png`, `trainers2_moon_inside.png` |
| `harness.py trainers_moon_cost --gfx angrylion` | the cost of the Moon Pound aim: the glove jumps at 10 spots around the dome (88 faces, 20 candidates); the work of every game frame (`MoonView.counts`) stays under 250,000 ticks (5.3 ms; the budget is 4 ms; 206,000 measured), The stick is moved on every frame, which stops the reuse of slams. `MOON_COST_LEVEL=n` tries another level; levels 12 and 15 show spikes that are other threads' time |
| `harness.py bosstrainer --gfx angrylion` | the boss windows (`BOSS_ONLY=atlantis,prehist,space,quiet` runs some parts): in each boss a contact is put at a chosen game frame after a hit, as the research did, and the trainer's view is read. Atlantis: a touch at H+48 gives "TOUCH 1 EARLY" and no hit, H+49 a hit read as HIT gap 49 on time; the window line counts 48 down to 0. Prehistoric: a snowball at H+33 is LOST (boss hurt) and at H+34 a HIT gap 34; a ball of radius 8 reads NOT SNOWBALL and one of speed 1 reads TOO SLOW (the touch is the game's flag, written; the floor changes a placed ball's speed). Space: the game's contact result written 19 and 20 frames after a hit on the left shoulder and on the head gives LOST 1 EARLY and HIT gap 20. Quiet: nothing in Atlantis 1, in a pause or with the switch off (no hook work), and the menu's fifth entry turns it on. Pictures `bosstrainer_*.png` |
| `harness.py slapcancel` | a fact of the game, with the frame log: Z pressed during the slap's windup ends it, the glove keeps the ball and A is latched |
| `harness.py setaction --gfx angrylion` | Set action in Atlantis 1: duck and cartwheel from the menu; the glove's action mode in RAM becomes each one, the game goes on for three seconds, the MODIFIED marker is drawn and goes out; the ball state burst sets `ballControl.state` to 2; no text is drawn before the first use |
| `harness.py timerfix --gfx angrylion` | with `timer321` at 0, as after a bonus level, the in-game timer of Atlantis 1 still starts on game frame 81 |
| `faithful.py [--with-features] [--reuse-retail]` | the retail ROM and the practice ROM give the same game, frame by frame (below) |
| `perf.py --survey` | the highest use of the collision view's fixed buffers in each of the 42 levels (whole-level view, every switch on, Moon Pound aim on): `CollStats` peaks and the cache's size; writes `build/perf/baseline.md` (or `--md`); about 1 minute a level |
| `perf.py [--quick]` | the practice code's time and the game's pace in the heaviest levels (below); `--quick` is part of `make test` and fails over fixed limits |
| `node tests/patchpage.js` | the patch page turns a `.v64` copy of the ROM into the same 8 MiB practice ROM as `make`, and says "8 MiB" |

The page test needs Playwright (`npm install playwright`, or `NODE_PATH` set to a folder that has it) and
`make patch`.

## What the harness needs to give the same run every time

- **The same video plugin.** The plugin changes the game's timing in the emulator. A run with Rice and a run
  with angrylion reach the same screens on different video frames. Compare runs from one plugin only.
- **angrylion with one thread.** With its worker threads on, two runs of the same ROM can differ by a few video
  frames during loads. The harness turns `Parallel` off.
- **An empty EEPROM.** The game writes its save while it boots and at the end of each level, and the time that
  takes depends on what is in the EEPROM. mupen64plus keeps one save per ROM, so a save left by an earlier run
  changes the timing. The harness gives every run its own empty save folder.

With these, runs of the same ROM give the same state on every video frame (checked with three runs of the
retail ROM).

Other facts about the emulator:

- mupen64plus calls the frame callback only when the video plugin shows a new frame. The Expansion Pak screen
  does not count as new frames, so `nopak.py` reads RDRAM from a timer instead (`SECONDS` and `check()` in a
  test). A screenshot works only from the frame callback; `emu.fb_png()` writes a frame buffer from RDRAM
  instead, which needs angrylion.
- Rice draws the title menu and the level select black, and does not draw the game's timing bars. Use
  angrylion to look at them.
- IS-Viewer text shows in mupen64plus's log as `IS64:` lines at the verbose level.
- A frame buffer drawn by the CPU is not shown by Rice or by angrylion in mupen64plus, even when the video
  registers point at it. The Expansion Pak message is drawn by the RDP for this reason.

## The route

`tests/trace.py` plays a fixed route and records the game's state on every video frame: `frameCount`,
`gameInfo.mode`, `gameInfo.level`, `gameInfo.exitFlags`, `rseed`, `player.levelFrames`, and the position and
speed of the hand and the ball. Each step waits for the game to reach a state, not for a fixed video frame:

| Step | What happens |
| --- | --- |
| logos | no input until the logos (`PRESENT`, level 43) reach game frame 200 |
| start | one short Start, which skips the logos; on the title flythrough (`FLYTHRU`, level 44) at game frame 60, `gameInfo.mode` = 2: the game's level select |
| select | `selectedLevel` = 10 (Atlantis 1), then Start |
| level | Atlantis 1 with pseudo-random input from a fixed seed (stick, every button), a new input every 1 to 8 game frames; at game frame 620, `newLevExitTimer` = 1 and `gameInfo.exitType` = 1 end the level as through its exit |
| exit | the end of level screen, A now and then |
| next | Atlantis 2, to game frame 120 |

The route turns every binding off in both runs (the offsets are in `tests/layout.py`): its random input holds the stick fully in a direction with D-pad
presses, which would fire the default bindings.

In a level the input is chosen by the level's game frame, as in a recording, so both ROMs get the same input
on the same game frame even when a load ends a video frame earlier or later. Every 50 game frames of a level it
also records the lower 4 MB of RAM.

The level select only starts a level when it opens from the title flythrough. Opened from the title menu, or
before the title has faded in, it shows but stays on its screen.

## Faithfulness

`faithful.py` runs the route on the retail ROM and on the practice ROM with every practice feature off (every
HUD item off, the retail ball controls), then checks for each level:

- **logic:** the state on every game frame is the same;
- **lag in play:** every game frame falls on the same video frame, counted from the level's first game frame. A
  shift would be input lag or a stutter;
- **the picture:** a coarse picture of each frame buffer (`cfb_16_a`, `cfb_16_b`) on every video frame: the mean red,
  green and blue of 8 by 6 blocks of 40 by 40 pixels. A block may differ by half a 5-bit level at most, by the same
  rule as the logic. Only with the features off: the features draw on the screen;
- **RAM:** the same outside the areas that record time and the picture's buffers (`tests/ramcheck.py`). A word
  whose two 16-bit halves each differ by 1 at most is listed, not failed (see below).

It also reports on which video frame each level starts in both ROMs, which shows how long the loads took.

A game frame's state is sampled once a video frame. At the start of a level the game can rest in the middle of a
game frame for several video frames, and the practice code's few cycles move the point where it rests. So a game
frame counts as the same when any of its samples match, and a difference that is gone on the next sampled game
frame counts as such a sample. A real difference stays.

The practice code costs a few CPU cycles on each frame. The game's logic does not see this, but some values
record time, and they differ. These areas are masked:

| Area | Why it differs |
| --- | --- |
| the code from `main_TEXT_START` to `main_DATA_START` | the hooks (see [build.md](build.md)) |
| every thread's context and stack, the scheduler, `__osThreadSave`, the PI manager's thread and stack (`piMgrThread`, `piMgrStack`) | saved registers, which include return addresses and counts |
| `printTimer` to `timers` | the debug timing bars (`StartTimer`, `EndTimer`) store CPU counts |
| `frameStart`, `__osBaseCounter`, `__osCurrentTime` to `__osBaseTimer` | the CPU counter and the OS's timers |
| `audio_memory`, `__am`, `D_802AB010`, `D_801F4170`, `D_801F4174` | the audio thread's progress at the moment of the check |
| `__osEepPifRam`, `piMessageQ`, `piMessageBuf` | the state of the EEPROM and PI requests at that moment |
| `zbuffer`, `rdp_output` (to `audio_memory`), `cfb_16_a`, `cfb_16_b` | the drawing: the picture check covers it |

The draw thread runs beside the game thread. The practice code's few cycles move the point where one interrupts
the other, and the draw thread's vertex data can then differ by 1: some texture coordinates in `objectsVtx` and
in a level's vertex data on the heap. That moves a texture by 1/32 of a texel. The coarse picture does not change
by more than a quarter of a level, while the HUD's timer changes it by several levels. The test lists such words
(for example "objectsVtx (72), main_VRAM_END (42)") and fails on any larger difference.

During a load the decompressor's progress at a given video frame also differs by a few bytes, so the RAM is
compared only while a level runs.

### Results

Run with mupen64plus 2.5.9 and angrylion with one thread.

- **Features off:** in Atlantis 1 (631 game frames) and Atlantis 2 (115), the same state, the same picture and
  the same video frame on every game frame, and the same RAM on all 14 checks. Both levels start on the same
  video frame as in retail.
- **Features on** (the default settings: the timer shown, Easy ball controls): the same state and video frames.
  The picture differs on most game frames (382 of 630 in Atlantis 1), as it should: the timer is drawn. This shows
  the picture check works.
- With the missed-taps and chaos tools: 72 words of `objectsVtx` and 42 words of vertex data
  on the heap differ by 1 (above). The largest difference of a block of the coarse picture is a quarter of a level.
- Loads can end a video frame earlier or later than in retail. The practice code's cost decides this; see
  [build.md](build.md#cost).

Not yet tested: lag on hardware, and runs with other video plugins. mupen64plus does not trap on FPU faults and
does not model the data cache, so the checks for those (safe floats, cache write-back) are checked by review
only.

## Performance

`tests/perf.py` runs `tests/perflevel.py` in the emulator once for each of Atlantis 3, Carnival 1, Pirates 3, Out of this
World 3 and the hub, and once for each on the retail ROM, then writes a Markdown report (`build/perf/baseline.md`).
Run it under `flock /tmp/emu.lock`; it takes about 50 minutes (Rice, `--gfx rice`, is much faster than angrylion here).
Each scenario writes a switch in RAM, waits 30 interrupts and measures 120 (40 game frames).

- **Time.** `practice/src/perf.c` and `perf.h` count ticks of the CPU counter (46.875 MHz) for five stages: the game
  thread at the start of a message and around `ProcessInput`, the draw thread after the game's `PrintTimers` and for the
  whole list, and the controller thread without the game's own read. They only record. A thread that runs above another
  adds its time to the other's maximum.
- **Pace.** The harness polls `frameCount` and `__osViIntrCount` on every video interrupt, on both ROMs, and counts the
  video interrupts between game frames (3 when nothing is late). This is the count of late game frames. The practice
  ROM's own histogram (`perf.interval`) is taken where the practice hook runs in the frame. That point can move by one
  interrupt from frame to frame, which gives pairs of 2 and 4 with no late frame; it is not used for the count. The
  report lists the retail ROM's late frames of the same level beside the practice ROM's. The emulator gives the same
  result on most runs, so a rare late frame needs many windows to see (`PERF_REPEAT`, `PERF_WINDOW`).
- **Input lag.** C-up is held and the trial counts the interrupts and game frames until `pad[0]` shows it. The harness's
  callbacks are phase-locked to the game frame, so only whole frames of lag show.
- **Others.** Savestate save and load ticks, the collision cache's build after a restart, and the views' own counters.

The emulator counts instructions and does not model the RSP, the RDP or memory waits. A console is not measured.

## Pulses: taps shorter than a game frame

The emulator's frame callback comes in bursts: five calls with no game time between them for every three video
interrupts (one game frame). A script cannot hold a button for less than a game frame. `tests/pulseinput.c` is
gloveworld's `scriptinput` plugin with a pulse added: `emu.pulse("A", polls=1)` presses the buttons for the next
poll of the controller, by whoever polls, and `emu.pulse_left()` tells whether it is still waiting. Every test gets this plugin (`harness.py`
builds it into `build/tests/emu`). With the tool off, the game is the
only one to poll, so it sees every pulse. A game read after a poll shows the answer of the poll before it, so pulses
need about two game frames between them to be counted one by one.

## Runs repeat exactly

mupen64plus moves the PI and SI interrupts by a random amount by default (`RandomizeInterrupt`), with a seed
from the clock. The game's logic did not change, but the picture did, depending on when each run started. The harness
turns this off.

angrylion's dither noise comes from one random sequence that runs on from boot. A frame drawn more or less during
a load then changes the noise of every later picture. The harness builds its own copy of angrylion
(`build/tests/emu`, from the source that gloveworld's `setup.sh` fetched) that starts each display list from the
same seed: a picture then depends only on its own drawing. Two runs of the same ROM with the same input give the
same RAM and picture.

Run the emulator tests one at a time: they share the machine's CPUs.

## Trainers

The trick trainers (docs/build.md, "The trick trainers") read the glove's state at the start of a game frame and the
pad of that frame. The pad was the open point. `RunGame` calls `ClearText` for every message of the main queue, and
the controller thread (priority 115) and the main thread (priority 10) wake on the same frame message. The
controller thread starts the serial read and waits for it. The main thread can run in the meantime.

`tests/trainers.py` compares the pad at the hook with the pad at `PracticeProcessInput` over 1,000 game frames of
random presses of A, B, Z, L and the D-pad (counters in `trainerDiag`):

| Run | Game frames | First hook run of the frame differs | Last hook run before the frame differs |
| --- | --- | --- | --- |
| first | 1,419 | 921 (presses: 969) | 1 (presses: 1) |
| second | 984 | 664 (presses: 697) | 0 |

The first run of a game frame sees the pad of the frame before in about two thirds of the frames. The last one
before `MainGameLoop` sees the right pad, but the hook cannot tell which run is the last. So the trainers take the
state at the hook and the pad in `PracticeProcessInput`. That pad is the one that `MoveHand` reads, unless the
controller thread reads again in between (not seen in these runs).

These numbers come from mupen64plus. A console is not measured.

Some facts about the harness matter for the trainer tests.

- A button asked for in `step()` number k is read by game frame k + 1. The frame log shows the pad of each frame, and
  the tests of the line keeper use its buttons, not the ones asked for.
- A game frame that takes longer than its video frames makes the next one run at once. The frame counter can move by 2
  between two polls. `step_len()` in `tests/trainers_moon.py` allows that, and the moon test finds the state of each
  view by its hover height.
- After a teleport the hook of the next frame has already run. That frame's view is for the old state.
- Python that runs between two `yield`s stops the emulator. The model runs after the frames are recorded.
- The boss test (`tests/bosstrainer.py`) works in game passes: `newpass()` waits until `frameCount` changes, and a
  view read after it was made at the hook of the pass before. A ball or a flag written between two passes is read by the
  next pass. The ball's speed in the pass of a touch is the game's own, so the Prehistoric ball cases write the game's
  touch flag with the ball far away. In the Space fight Glover never has control, so the test waits for `frameCount` 700
  and no cameo only.
