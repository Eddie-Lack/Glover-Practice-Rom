# HUD

_Back to the [Home page](Home.md)_

The HUD is what the practice ROM draws over the game. Only the in-game timer is on by default

| Option | What it shows |
| --- | --- |
| Timer | The in-game timer |
| Timer in frames too | Adds the game frame count to the timer (on by default) |
| Input display | The buttons and the stick that the game reads |
| Input history | The last presses. Each shows the button, the level's game frame of the press and how many game frames it was held |
| Glove position and speed | Where the glove is and how fast it moves |
| Ball position and speed | The same for the ball |
| Glove action | The glove's current action |
| Counters | _Sub menu_ |
| Full-game timer | _Sub menu_ |
| Text shadow | Shadow behind the HUD text |
| Layout | A sub menu that sets the x and y place of each item. Places are in units of 2 pixels, x up to 160 and y up to 120 |
| Hide HUD | Hide or show the HUD. Quick action |

A "MODIFIED" line in red shows while a tool that changes the game is on. "free camera" shows while the free camera is on _(see [Game](Game.md))_

## Counters

| Option | What it shows |
| --- | --- |
| Frame counter | Game frames since the level started |
| Lag counter | Video frames that the game took beyond its normal frame rate |
| Random seed | The random number seed, with the number of calls since the level started |
| Last level time | The time of the last level played |

## Full-game timer

The full-game timer times real time for a whole run. Realtimes Realtimes Realtimes

- Starts the first frame after you select the difficulty to start a new game
- Stops when Glover enters the fountain with the last crystal
- Splits at each level or at each crystal (set this in "Splits")
- Note: Time with the practice menu open doesn't count
- Note: Timer only runs when it's enabled

| Option | What it does |
| --- | --- |
| Show the full-game timer | Turn it on |
| Splits | Split at "each level" or "each crystal" |
| Start now | Start the timer |
| Stop and clear | Stop the timer and clear it |
| Time | Shows "stopped", "running" or "done" with the time |
| Splits (sub menu) | The last level of each split, and its time |

## The in-game timer

The in-game timer always starts at its normal point. 

Note: In the original game, after playing a bonus level, the timer starts instantly (about 4 seconds early) in every level after. The practice ROM fixes this in every case.

## Practice best times

When the timer or the last level time is on the HUD, the end of a level shows whether the time is a new best.

_See [Settings](Settings.md) for the best times_
