# Game

_Back to the [Home page](Home.md)_

Tools that change or read the game itself. Items that change how the game plays make the HUD show a red "MODIFIED" line, and the run does not count for a practice best time (see [Settings](Settings.md)).

| Option | What it does |
| --- | --- |
| Cheats | _Sub menu_ |
| Power-ups | _Sub menu_ |
| Lives | Set the lives (0 to 9) |
| Health | Set the health (1 to 3) |
| Random numbers | _Sub menu_ |
| Mute music | Turn the music off or on |
| Mute sound effects | Turn the sound effects off or on |
| Controller 2 | A sub menu |
| Fog | Draw the level "as the level" has it, or without fog. |
| Movement and camera | A sub menu |
| Set action | A sub menu |
| Chaos inputs | Random disturbances, see below |
| Ghost | A ghost of a recorded run, see below |
| Trainers | WIP - _Sub menu (see [Trainers](Trainers.md))_ |

Lives and health changed during a level, a frozen or changed random seed, a power-up given from the menu and any cheat other than level select and open portals and levels all stop a run from counting for a best time

## Random numbers

| Option | What it does |
| --- | --- |
| Freeze random numbers | Keep the random number seed at its value now. Select again to let it run |
| Random numbers: one call | Step the seed by one call of the game's random function, on the next game frame |
| Random numbers: level start | Set the seed to the value of a level's start, on the next game frame |
| Random numbers: set (frozen) | Freeze the seed and edit its value in the memory view, digit by digit |

## Controller 2

| Option | What it does |
| --- | --- |
| Controller 2: debug controls | Controller 2 drives the game's debug controls, see below |
| Controller 2: L marks all done | Set the L button of controller 2 to the debug controls (gives 100% completion). Edits your save |

## Movement and camera

| Option | What it does |
| --- | --- |
| Moon jump | Hold L on controller 2, or hold the stick fully up + L on controller 1 |
| Noclip | Fly through the level, see below |
| Noclip speed | The base speed: 2, 4, 8, 16, 32 or 64 units a game frame |
| Free camera | See below |
| Camera speed | `normal, fast, faster`. The camera keeps up with Glover when you go fast, see below |

## Camera speed

The camera can fall behind Glover when you go fast, like with bouncing MBZ-Ls

`fast` lets it follow twice as fast. `faster` lets it follow three times as fast

The camera decides which way the stick and the D-pad move Glover, so a faster camera can change a route

!! Modified game !! A run with it on doesn't count for a best time

A small camera icon shows at the top right, above the score, while it's on. The HUD doesn't say MODIFIED for it

## Cheats

Toggle the in-game cheats: 
`Locate garibs, Call ball, Checkpoints, Infinite lives, Power ball, Control ball, Infinite energy, Enemy ball, Low gravity, Big ball, Fish eye, Camera rotate R, Camera rotate L, Mad garibs, Death spell, Frog spell, Hercules, Speed up, Boomerang ball, Vanish ball, Froggy, Secret cheat, All cheats off, Level select, Open portals and Open levels. The list is longer than the panel, so it scrolls: the top right of the panel shows the place in the list.`

"Camera rotate R" and "Camera rotate L" don't show anything until you close the menu (unless you have [`Pause game in this menu`](https://github.com/Eddie-Lack/Glover-Practice-Rom/blob/main/wiki/Settings.md) set to Off) 

## Power-ups

Choose a ball type or spell, then "Give": 
`(Rubber ball, Stun, Death, Frog, Suction, Hercules, Speed up, Rotor blades, Bowling ball, Power ball, Ball bearing, Beach ball, Snow ball, Crystal ball, Boomerang ball, Vanish ball)`

## Moon jump and noclip

The HUD shows "MODIFIED" while either is on

With noclip on:
- the stick flies Glover where the camera looks
- D-pad up and down move up and down
- Holding R makes you move four times faster
- Glover ignores walls, floors and gravity
- The game doesn't register buttons except Start and the C buttons
- The quick actions are off while noclip is active 

## Free camera

Choose a speed (2, 4, 8, 16 or 32 units a game frame). Use controller 2 to fly the camera:

- The stick moves forward and back
- D-pad up and down to lift
- The C buttons turn and tilt
- R is four times faster, Z is four times slower

It also works while the game waits (frame advance, the menu).

Quick actions on controller 2 that use these buttons do not fire while it is on. The game only draws what's near its own camera

## Ghost

Each input recording also records where the glove and the ball were on every game frame. With the switch on, a see-through yellow glove (with a white nose that shows its heading) and a pink ball are drawn at those places, in step with your run. 

Game frame N of your run shows the ghost of game frame N since the recorded state. Load the state, run again and race it

A new recording of the level replaces its ghost. You can keep up to four levels, each up to ~7 minutes. 

Ghosts are lost when the console is switched off. _See [Savestate](Savestate.md) for recording_

## Set action

Choose a glove action from a list (idle, walk, duck, cartwheel and so on) and use "Set glove action". Choose a ball state and use "Set ball state" to burst, release or drop the ball

The action starts on the next game frame

The HUD shows "MODIFIED set action" for three seconds.

Some actions need a ledge, a block or a power-up. They may end at once or, rarely, crash the game. The menu warns "Modified game! May crash".

## Chaos inputs

Practice having dropped or random inputs. This one is awesome

Choices: `off, rare, sometimes, often.`

Now and then the tool adds a one-frame press of a random button, takes away your next press, or nudges Glover's place or speed a little

Rare, sometimes and often mean about every 10, 3 or 1 seconds

The HUD shows "MODIFIED chaos" while it is on. 

Chaos doesn't do anything while you record or play back an input recording

## The game's debug controls on controller 2

These are from the real game - some were modified to not need the dev cable. Turn them on with "Controller 2: debug controls":

- C-left and C-right move Glover to the previous or next platform
- C-up lifts Glover or the ball, C-down pulls it down
- R turns the game's inertia off and on. With it off, Glover has no momentum and no gravity, and 3 times the acceleration
- Start hurts the ball: it gets a plaster each time, and bursts when it has too many
- A + B goes back to the title menu
- Z changes the timing bars
- D-pad left and right change the music
- D-pad up makes puffs of smoke around Glover
- D-pad down, and D-pad right while held, turn the level's light
- The stick and A and B move an invisible helper box. With Z held, they change its size
- *off by default* L marks every level and garib as done in the current save slot

The L button is not active, unless "Controller 2: L marks all done" is on. The menu warns "Edits your save!"
