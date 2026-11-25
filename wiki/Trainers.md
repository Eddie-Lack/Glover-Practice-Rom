# Trainers

_Back to the [Home page](Home.md) or the [Game](Game.md) page_

WIP - six trainers for tricks, in the Game menu under "Trainers"

Trainers only read the game and draw on the screen. They don't change anything

They go quiet in noclip, moon jump, free cam, a pause and an exit

A trainer's panel shows only while its trick is under way, or while it holds a result. The panels stand in a column on each side of the screen, clear of the game's HUD, the timer and each other

You can keep them on a Controller Pak _(see [Settings](Settings.md))_

## The tricks

| Trick | What it is |
| --- | --- |
| L mash | L pressed again and again while Glover is on the ball: he switches between standing on it and holding it, and the ball rolls faster than when he walks on it |
| MBZ-L | While Glover holds the ball: B (the slap) or A (the throw's aim), cancelled with Z, then L on the right frame. The ball's speed goes from 7 to about 10.4 |
| Line keeper | Walking with the D-pad on a diagonal, with C-left or C-right taps that keep the line straight |
| Moon Pound | A slam (Z in the air) over a gap between the floor's faces, which falls through the floor. The best known one is at the hub's Space portal |
| Wall clip | MAZ-R with the ball bearing: A (the throw's aim), cancelled with Z, then R on the next frame, with the ball close to a wall. The ball goes through the wall |
| Boss windows | A boss that was hit ignores the next touch until a wait is over. The trainer counts the wait |

## Trainer log

A list of your last attempts, newest first, at the top right of the screen. In the boss fights it's under the boss panel

Each line starts with the trainer: `LM, MB, LK, MP, BW, WC`

Its place is set in _HUD, Layout_ ("Trainer log x" and "y", default 152 and 22): x is where the lines end, y is the top line. At any other place the log can cover the panels. The place is not saved

How many lines show is set in _Settings_ ("Trainer log length", `off, 5, 6, 8, 10`, default 5)

With MBZ-L timing or Wall clip on, the log stops at 5 lines. A line that would run into a panel is left out

An attempt is:

| Trainer | One attempt | The line |
| --- | --- | --- |
| L mash timing | A run of 3 or more L presses. It ends 12 game frames after the last | `LM 14P 11 ON TIME 9.8 GOOD`: presses, how many were on time, the peak speed, and `GOOD` (90% or more on time), `OK` (70% or more) or `SLOPPY` |
| MBZ-L timing | One B or A start, once the speeds are in | `MB B 7.0>10.4 PERFECT`: the start button, the speed before and after, the verdict |
| Line keeper | One walk on a diagonal of 20 game frames or more | `LK 87F OFF 3.1 R`: the frames, how far you drifted in units, and to which side |
| Moon Pound aim | One slam. The line is what the aim said about the Z you pressed | `LAND 2.4 FROM A HOLE` (red), `CLIP` (green), `NO Z` or `NOT CHECKED` |
| Boss windows | One event: a hit, a lost touch or an early touch | `BW W HIT 49 ON TIME`, `BW P LOST TOO SLOW` |
| Wall clip | One cancel, 8 game frames after it | `WC IN 15.0 R+1 CLIPPED`, `WC TOO CLOSE 4.8 NO R NO CLIP`, with the panel's words `IN`, `TOO FAR`, `TOO CLOSE` and `TOO SLOW` |

Green is good, yellow is close, red is lost

The log stays when you reset or load a savestate. It clears when the level changes, and when you turn the trainer off

## Result hold

When an attempt ends, the trainer's panel keeps showing it for a moment. The trainer keeps running behind it

How long is set in _Settings_ ("Trainer result hold", `off, 1 s, 2 s, 3 s`, default 1 s)

If another attempt ends during the hold, its result takes the panel and the time starts again. Holds don't queue

## L mash timing

Shows how well you mash L on the ball:

- Each L press (stand or hold), the gap since the last one, and a verdict
- The last 24 game frames
- The ball's speed, its peak, the average of the last 20, and the target for a perfect mash

It warns you when the stick is too weak, the camera turns, you let go of the stick, the ball is in the air, or it's not the rubber ball. Warnings show only during a run of presses, at most 3

The strip of game frames: `J` (blue) is a frame where Glover holds the ball, `B` (orange) one where he stands on it, and a white bar marks a press. A press after a long pause is `REST`

AVG20 is green from 90% of the target and yellow from 70%

## MBZ-L timing

For B or A while Glover holds the ball. Shows where Z, the aim or windup, and L fall against the window, the speed before and after, the best speed, and a verdict

- The grid has a row each for B (or A), Z and L, one cell per game frame. The green cells are where Z and L belong
- `Z ARMED`: Z was already down when the attempt started. `Z FRESH`: Z was pressed during the attempt. The verdict `Z FRESH ON n: BALL DROPPED` means that press let the ball go
- `AIM n  L n  BEST n`: the frame of the aim or windup, the frame of your L, and the best frame for L
- `BEST 10.4  +3 10.7  +6 10.8`: the best speed you could reach, and the ball's speed 3 and 6 game frames later

The panel shows from the aim until the speeds are in, then for the result hold. Older attempts are in the log

## Line keeper

For walking on a diagonal D-pad:

- A bar with the heading error, and its number in degrees
- An arrow (C-RIGHT or C-LEFT) for when to tap C to keep a straight line
- The last 36 game frames, with a tick for each frame with C down
- How many of the last 20 frames had C down, against the usual number (EXP)
- How far your path has drifted (OFF, in units): green under 2, yellow under 6

It's quiet when the camera can't turn, you stand still, or the stick isn't on a diagonal

## Moon Pound aim

While Glover jumps, shows what a slam would do if you press Z on each of the next 8 game frames (+1 to +8). Green falls through the floor, red lands

It also shows:

- AIM: the direction to the nearest hole in the floor, in units, across and up the screen
- DIST: the distance to it, in units
- A map of it
- PHASE: the height phase. The green band (0.2 to 1.9) is where a slam can clip
- STICK: the stick values that close the gap with one tap

Flags after the name: `SLOW` (the check ran out of its 4 ms), `FULL` (the face buffer is full), `HOLES?` (the list of holes may be incomplete), `BUILD` (the faces are being read again)

It's quiet when Glover isn't jumping, and in water

Grey cells: A is still held and the jump still lifts, the action lock is running, the slam reaches too far, or there was no time to check it

"NO EXIT" shows in the hub when the Space exit wouldn't open

This one can add lag (up to 4 ms a game frame)!

## Wall clip

For MAZ-R into a wall with the ball bearing. It works while Glover holds the ball, and shows what Z would do on this game frame:

- The ball's distance to the wall in front of it, and its speed towards the wall
- The window where the clip works. In the air: from 3.2 to 2.95 times the speed plus 3 (about 23.6 at speed 7). On the ground: from 5.5 to 2.6 times the speed plus 3 (about 21.2 at speed 7)
- A bar with the window in green and the ball in white
- "Z NOW: IN (3.1 TO SPARE)", "TOO FAR BY 4.1", "TOO CLOSE BY 1.2" or "TOO SLOW, NEEDS 3.2"

When you cancel with Z it keeps the numbers of that frame:

- "CANCEL AT 15.0: IN"
- "R +1 ON TIME" (R is read on the frame after the cancel)
- "CLIPPED (3.7 BEHIND)" or "NO CLIP"

It's quiet with another ball, or with no wall ahead. When a line is too long, "Z NOW:" is left out

"WALL AT AN ANGLE: A GUESS" shows when the wall is turned more than 30 degrees from your direction

Walls count within 60 units, and only walls (not floors or slopes)

Note: it's only for the ball bearing

This one can add lag (up to 3 ms a game frame)!

## Boss windows

For the boss fights where a hit only counts after a wait: the whale (Atlantis), the Prehistoric boss, and the last Space fight (level 40)

The panel is on the right, with the trainer log under it. The top line shows the boss and its health, and HITS counts the hits made. In the Space fight it also shows the left shoulder, right shoulder and head

- **Window:** "WINDOW IN 12" counts the frames until a touch counts. "WINDOW OPEN +3" means a touch counts, and has for 3 frames. "OPEN" means no hit yet, so any touch counts. In the Space fight each part has its own row
- **Last event:** "HIT 49 ON TIME" is 49 frames after the last hit, right when the window opened. "HIT 52 +3 (0.1S)" is 3 frames late. Green is on time, yellow up to 3 frames late, red later
- **Misses:** "TOUCH 1 EARLY" (whale) is a harmless early touch. "LOST 2 EARLY" (Space) and "LOST BOSS HURT 4" (Prehistoric) are touches the boss ignored, and the shot or snowball is gone. "LOST TOO SLOW" and "LOST NOT SNOWBALL" are a Prehistoric snowball that's too slow (3 or less) or too small (9 or less). "FIRST" is a hit with nothing before it to measure from
- **History:** the last 8 events, newest first, at the top of the screen. The letter is the part: `W whale, C crab, F fish, P Prehistoric boss, L left shoulder, R right shoulder, H head`. Then the frames since the last hit and how late ("W49 +0"). "X" is a lost touch, "E" an early one
- **Atlantis:** "CRAB KILL WITHIN n", "CRAB GETS UP", "FISH STUN n", "TOUCHING WHALE", and "TOUCH: BALL NOT FREE" (the ball is on the ground or held, so it doesn't count)
- **Prehistoric:** "SNOWBALL READY", or the ball's size and speed
- **Space:** "RELOAD n" (frames to the next shot), "SHOTS n" (in the air), "LASERS n" (lasers left before the boss leaves), "AWAY n" (frames until the boss is back)

Windows are 49 frames for the whale, 34 for the Prehistoric boss and 20 for each Space part (20 game frames a second)
