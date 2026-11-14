# Savestate

## States in the slots are lost when the console is switched off!

_Back to the [Home page](Home.md)_

Savestates are a snapshot of the level exactly as it was, with the in-game timer and its song. 

Savestates can't be saved or loaded on the title screens

States in the slots are lost when the console is switched off! Card files stay.

| Option | What it does |
| --- | --- |
| Slot | Choose one of ten slots |
| Save state | Save the game's state into the chosen slot. Quick action |
| Load state | Load the chosen slot, also from another level. Quick action |
| Record input | Save a state, then record the controller input. Quick action; press again to stop |
| Play recording | Load the recording's state and play the input back. Quick action |
| Recording | Shows the recording's length in frames, or "recording" or "playing" |
| This slot | Shows the slot's level and size |
| Pool | Shows how much of the state memory is free and how many of the ten slots are used |
| Clear this slot | Empty the chosen slot |
| Clear all slots | Empty every slot. The menu warns "Forgets every saved state" |
| Level start state | Shows the level of the start state that the "instant" Reset saved, or "none" (_see [Warp](Warp.md))_ |
| SD card | _Sub menu_ |

## SD card

| Option | What it does |
| --- | --- |
| SD card | Shows what the last Check card found: "not checked", "no flash cart found", "no card in the cart", "not FAT16 or FAT32", "no folder yet", "save writeback pending, try again", "card read failed", or the cart's name with the number of files and their size |
| Check card | Look at the card and list the card files. (Nothing is written) |
| Card slot | Choose a card file, 1 to 100. Z held changes it by 10 |
| Card file | Shows the chosen card file's level and size, "empty", or "use Check card" |
| Save slot to card | Write the chosen pool slot (the Slot item) to the chosen card file. A file that's already there is replaced. The menu warns "Writes the card file: stops the game for seconds" |
| Load from card | Load the chosen card file into the game, also from another level. The menu warns "Stops the game for seconds" |
| Delete on card | Delete the chosen card file |
| Save recording to card | Write the input recording and its state to the card file `RECORD.GRC`. The menu warns "Writes RECORD.GRC: stops the game for seconds" |
| Load recording from card | Load that file: the game goes to the recording's state, and the recording is ready for Play recording. The menu warns "Loads its state: stops the game for seconds" |

_The default bindings for the four quick actions are on the [Controls](Controls.md) page_

## Time and memory

States are kept compressed in a pool of the Expansion Pak's memory

- The first state saved in a level makes a base for that level, about 0.6 MB, and takes about 1.5 seconds.
- Every further state in the same level is kept as its difference from the base. It takes some KBs and about 0.4 seconds.
- A load takes about 0.4 seconds.
- Up to four levels have a base. A state in a fifth level takes about 0.45 MB, or does not fit.

The game stops meanwhile, and the frame before shows "saving..." or "loading...". A save that does not fit says "pool full". If the slot held a state, the save emptied it. Clear slots to make room.

## States on the SD card

### Only the SummerCart64 works for now - in progress

States can be kept on the SD card of a flash cart. They stay when the console is switched off. The card must be FAT16 or FAT32 (a card of more than 32 GB is usually exFAT: format it as FAT32 or give it a FAT32 partition). The cart must not be in use by its menu: boot the ROM from the cart as usual.

- The files are in the folder `GLOVERPR` on the card, named `STATE001.GST` to `STATE100.GST`. Save makes the folder. The practice ROM only writes to the when you choose Save slot to card or Delete on card
- Each file holds one state. It can be loaded after the pool was emptied or on another day. A file is about 0.63 MB, so 100 savestates take about 63 MB.
-   Note: A file made by another version of the ROM may be refused ("not a usable state").
- A save takes the pool slot chosen with Slot. Save the state into the pool first, then to the card.
- The game stops while the card is read or written, and a bar near the bottom of the screen shows the progress. The frame before shows "saving to card..." or "loading from card...", and the message afterwards gives the size and the time, for example "saved to card 1: 619 KB, 1.34 s". The console's own work is about 0.7 s for a save and 1.5 s for a load (an emulator's clock). The card's time comes on top. A load reads the file twice: the first time checks it. A damaged file is refused before the game changes. The first save on a large card that holds many files can take several seconds longer.
- A save needs room on the card for the new file while the old file of that number is still there. Replacing a file on a nearly full card can say "the card is full" although the new file would fit in the old one's place. Delete the card file first (Delete on card), then save. A save that fails leaves the old file as it was.
- A card that is not in the cart, a cart without a driver and a card that is not FAT16 or FAT32 each have a message. A cart that still has a save to write back to the card ("save writeback pending, try again": wait a moment and use the item again) and a card whose test read failed ("card read failed") also have messages.
- The ROM touches the cart only when you use one of these items.

_The driver has not been tried on a console yet. The tests use a stand-in for the cart._

## Input recording

Record input saves a state, then records what the game reads from the controller, for up to 30 minutes. At the limit, the recording stops and the game says so. Play recording loads the state and plays the inputs back

The recording's state has a slot of its own. The slot chosen in the menu is not touched, and Load state still loads the chosen slot. If the state does not fit, the game says "recording not started: pool full".

Recording and playback need the game at full speed. Frame advance, slow motion or the practice menu stops them. Chaos inputs do nothing while you record or play back.

The recording also keeps where the glove and the ball were on every game frame, for the ghost _(see [Game](Game.md))_. 

A recording can be kept on a Controller Pak _(see [Settings](Settings.md))_.

### Recording on the SD card

Save recording to card keeps the recording with its state. It stays when the console is switched off

- The file is `RECORD.GRC` in the folder `GLOVERPR`. Saving again replaces it
- Load recording from card needs a level, not a title screen. It takes the recording's slot, so Play recording works at once
- A damaged file is refused before the game changes. The ghosts are cleared when a recording is loaded
- A file is about 0.63 MB, like a state file
- The practice best times go in `BESTS.GRB` in the same folder _(see [Settings](Settings.md))_
