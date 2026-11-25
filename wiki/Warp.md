# Warp

_Back to the [Home page](Home.md)_

| Option | What it does |
| --- | --- |
| Level | Choose a level. Hold Z and press left or right to jump to the previous or next world |
| Difficulty (from the title) | Easy, Medium or Hard. Used when you warp from a title screen. A warp from a level keeps the difficulty of the game you play |
| Go | Warp to the chosen level, from a level or the title screen. Quick action |
| Reset level | Restart the level. Quick action. It works only in a level, also with the game's pause menu open |
| Last checkpoint | Go back to the last checkpoint. Quick action |
| Checkpoint | Choose a checkpoint number. 0 is the level's start, and the number stops at the level's last checkpoint |
| Checkpoints | Shows the chosen checkpoint and the number of the last one: `0 of 1` means the start and one checkpoint |
| Go to checkpoint | Go to the chosen checkpoint. Quick action |
| Early title menu | Open the dev team's early title menu, see below |
| Back to the normal title | Leave the early title menu |
| Reset | How "Reset level" works: `load the level` or `instant`, see below |
| Demo | Choose one of the game's demo recordings, see below |
| Watch demo | Play the chosen recording |
| Watch all demos | Play all eight recordings back to back |
| Lives | `as is, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9`. The lives you start the warped-to level with |
| Garibs | `0` to `99`. The garibs you start the level with |
| Power-up | `none`, `Rubber ball`, a spell or a ball type _(same list as in [Game](Game.md))_. You start the level with it |

Warps and resets happen when the menu closes. The menu closes by itself when you choose one

A warp with Lives, Garibs or a Power-up set doesn't count for a best time. Only Go gives them: Reset level restarts the level without them. To practise a level with them on every attempt, bind the Warp quick action and use it in place of Reset level _(see [Bindings](Bindings.md))_

Note: the power-up arrives once the level's intro is over and the timer runs

"Last checkpoint" where the game's pause menu wouldn't offer it doesn't count for a best time

## Reset: load the level or instant

`load the level` is the game's own restart, with its fade out and in

`instant` loads a savestate of the level's first frame instead:
- No fade, and the same rng, positions and timers as a real start
- Takes about 0.25 seconds
- It needs a state of the level's start, which is saved when the level starts with `instant` on. So the first reset in a level, or the first one after you turn `instant` on, is the normal one
- The music keeps playing, sound effects stop
- Uses up to 0.3 MB of the state pool _(see [Savestate](Savestate.md))_
- Only works while the level is running. In a pause, a cameo or while Glover dies, you get the normal reset

The choice is kept with Save settings _(see [Settings](Settings.md))_

## Early title menu

The dev team's first title screen, with three options over a view of level 7. The game never uses it. The stick or the D-pad moves, A chooses:

- START: continue the current save slot
- PRACTICE: start a new game, with the intro
- OPTIONS: open the level select

"Back to the normal title" takes you back to the normal title screen

Opening it from a level uses the game's Quit exit, which may update the hi scores and write your save. It may crash!

Savestates and recordings don't work on any title screen

## Game demos

Watch the game's own demo recordings, the way the title screen plays them

| Choice | Shown on the title screen |
| --- | --- |
| ATLANTIS 1 | yes |
| CARNIVAL 2 | yes |
| PIRATES 1 | yes |
| PREHIST 1 | yes |
| FEAR 1 | yes |
| CARNIVAL 1 | no |
| SPACE 1 | no |
| SPBTWEEN | no: the Space boss cutscene, which plays after Space boss 1 |

"Watch all demos" plays all eight in this order, about 7 minutes in total

A, B or Start leaves the demo and stops "Watch all demos". When a demo ends, you go back to the title screen

The practice menu works during a demo, and the demo waits while it's open

Note: a demo turns all cheats off, never counts for a best time, and "Go" or "Reset level" ends it. The full-game timer keeps running
