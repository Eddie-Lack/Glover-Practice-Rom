# Facts about the game

Facts learned while building the practice ROM. Names are gloveworld's.

## Boot and screens

- With no input, the game shows the logos (level `PRESENT`, 43), then the title screen. The title screen is
  the flythrough (level `FLYTHRU`, 44) in `gameInfo.mode` 4, with "Press Start" over it. Start there opens the
  title menu (`gameInfo.mode` 1). Start during the logos skips them.
- The flythrough is a demo playback: `ReadControllerFromData` writes the recording into `controllerdata`, so
  the player's buttons do not reach the practice code's input layer either.
- `gameInfo.mode` 2 runs `LevelSelectScreen` (game.c), the team's level select. Writing 2 during the title
  flythrough opens it. Written on the title menu, or before the title has faded in, the level select shows but
  does not start a level. It lists 42 levels (`numLevels`); `selectedLevel` is the cursor; Start fades out and starts the
  level. It sets the save slot to 6 and the difficulty to Medium. The harness uses it to reach Atlantis 1.
- Writing 1 to `newLevExitTimer` and an exit type to `gameInfo.exitType` makes `CheckForExits` call
  `ExitLevel` on the next game frame. With exit type 1 (`LEVEL_EXIT`) the end of level screen follows, then the
  next level.

## The in-game timer

- The in-game timer (IGT) is `player.levelFrames`, in game frames: 20 a second on NTSC, 50/3 on PAL.
  `StartNewLevel` sets it to 0. `CheckForExits` adds 1 on each game frame that is not paused, with no exit
  running, once `frameCount > timer321 * 20`.
- `timer321` is the countdown before the timer starts, in seconds. It is 4 at boot. `DoBoss` (boss.c) sets it
  on the first frame of a level: 4 in a Time Trial of a level without a bonus timer, 0 in a level with a bonus
  timer (`levelInfo[].bonusTime` is not -1: the six bonus levels). Nothing else sets it.
- **The bug:** after any level with a bonus timer, `timer321` stays 0. In every later level the timer starts
  on the level's first game frame instead of the 81st, 4 seconds early. It lasts until a Time Trial of a level
  without a bonus timer sets it back, or the console is switched off. Measured in the emulator: after Atlantis
  Bonus, `timer321` is 0; with 0, the retail timer in Atlantis 1 starts on game frame 1.
- Time Trial alone does not cause it: a Time Trial of a normal level sets `timer321` to 4. Time Trials of bonus
  levels do cause it, and so do bonus levels played normally.
- **The fix** in the practice ROM sets `timer321` to 4 at the start of every level (`PracticeSeedRandom`, in
  `StartNewLevel`). `DoBoss` still sets it afterwards for bonus levels and Time Trials.
- A finished level: `ExitLevel(type, pause, 1)` sets `gameInfo.exitFlags` bit 1 and computes `levelTime`
  (seconds, from `player.levelFrames`). `CheckForLevelCompleted` then updates the game's records for
  `gameInfo.prevLevel`: the best time, the level and garib bits. It is called from `EndOfLevelScreen`,
  `EndOfWorldScreen` and the hub exit (in `CheckForExits`). On Hard (the Time Trial) `CheckForExits` does not go to the
  end of level screen. After the exit's switch it sets `bestTimes[]` itself when `levelTime` beats the record and goes
  to the name screen (`PrepareSong(TITLE_TRACK)`), or else starts the Time Trial screen (`InitOverlayList` and
  the list setup). That path never calls `CheckForLevelCompleted`, and the name screen saves the best times
  later (`DoneNameScr`). A restart (`TIMETRIAL_RESTART_EXIT`), a quit and a time-up (`TIMETRIAL_EXIT`) have no
  complete flag. `levelInfo[].bit` is the level's place in `bestTimes` (0 to 29, five a world;
  Space Boss 1 and 2 share 28); levels without a time have 30. Writing an exit type to `gameInfo.exitType` and 1 to
  `newLevExitTimer` ends a level through `ExitLevel(type, 1, 1)`.
- The Hi Scores screen is screen 4 of the title screens (`screenList`; reached from the title menu's fourth entry,
  then the fourth entry of screen 3). `currentHiscoresLevel` is its page: -1 the score table (`hiscoreTable`), 0 to 5
  a world (`printHiscores`, five rows of `bestTimes`). A title screen takes A only some time after it appears.
- `bestTimes[levelInfo[level].bit].time` holds the best times in whole seconds. The game updates it at the end of
  a level on every difficulty (`CheckForLevelCompleted`, and on Hard `CheckForExits`).

## The controllers

- `controllerdata[4]` (libultra's `OSContPad`, 6 bytes each) holds the raw read. `ControllerProc` (the
  controller thread, priority 0x73) reads all controllers once a game frame, then calls
  `ReadController(ActiveController)`, which turns `controllerdata` into `pad[]`. `ActiveController` is always 0.
- `CheckForExits`, `DoFlythru2Screen` and `CheckJalloc` read `controllerdata` directly. A change to
  `controllerdata` before `ReadController` reaches them too.
- The game logic reads only `pad[ActiveController]`: `button[14]`, `pressed[14]`, `holdTime[14]` and the
  stick. Button indices: 0 A, 1 B, 2 Z, 3 Start, 4 L, 5 R, 6 left, 7 right, 8 up, 9 down, 10 C-left, 11
  C-right, 12 C-up, 13 C-down.
- `ProcessInput` (controller.c) reads the difficulty in one place only: the ball walk direction. On Easy the
  stick pushes the ball forward; on Medium and Hard it is reversed.
- Start opens the pause menu in `ProcessInput` (`pauseMode`); `UpdatePause` closes it. `gamePaused` stops the
  game's logic.
- The controllers are read once a game frame: in a level a game frame is 3 retraces (20 a second), in the menus 2
  (30 a second). A lag frame makes the gap longer.
- A press is a change between two reads (`pressed = button && !prevButton`). A tap that starts and ends between
  two reads is never seen, on the console too, and two taps between two reads count as one. Repeated presses need
  2 game frames each: tested in mupen64plus with L (stand on and step off the ball), every press at a spacing of
  2 game frames or more took effect.
- `osContStartReadData` fetches every port, so controller 2 comes with the same read.
- The serial interface (the bus to the controllers and the EEPROM) is shared, and the controller thread owns it.
  The practice code's tool "Show missed taps" makes extra reads of its own, only while the controller thread waits
  for its next frame message ([build.md](build.md#the-extra-controller-reads)).
- `RunGame` calls `ClearText` (and so the practice code's `ActionsGameFrame`) for every message of `main_msgQ`,
  not only for the game frame's (message 9). A once-a-game-frame action must test `frameCount2` as well.
- Taking a practice input away from the game must clear it from `button`, `pressed` and the hold counters before
  the game's logic reads them. The practice ROM clears it in `controllerdata` before `ReadController`, which
  then builds `pad[]` without it.
- `pad[4]` is the team's debug controller: `ProcessInputPersonal` (personal.c) acts on it every frame. Its L
  marks every level and garib of the current save slot as done, and the game may save that. The wiki page
  [Game](../wiki/Game.md) lists the other controls.
- Z while Glover holds the ball releases it (`HAND_RELEASE` needs a Z press), when no other action comes first.
- The D-pad steers like the stick when the stick is centred (up beats down, left beats right). It has no other
  use in the game logic, apart from the debug controller.

## The glove's actions

From the code (hand.c, controller.c), checked where noted.

- Each game frame of a level: `UpdateActor` for the glove and the ball, then `ProcessInput`, `MoveBall` and
  `MoveHand`. Inside `MoveHand`: `noControlTimer` goes down by 1, `MaintainAction` (the current action's own
  checks, which can change the action directly), `actionLock` goes down, then the action table: if
  `actionLock` is 0, the first action (in enum order) that the table allows from the current one and whose
  `ConditionsMet` is true starts. At most one starts this way each frame. Then `actionTime` goes up by 1.
- Measured with the frame log in mupen64plus: the controller is read exactly once between two of the glove's
  frames (`tests/framelog.py`).
- While `noControlTimer` is not 0, the read keeps every button but Start up, and holdTime is not counted. A
  button held through it counts as a new press on the first normal read.
- `latch[]` makes the game ignore a held button until it is released: after the slap's Z cancel (A), the
  snatch (B), the throw aim's cancel (B), and others.
- `actionLock` counts down once a game frame, but a value of 1000 or more is an animation lock: it holds until
  the action's animation ends (the duck, the cartwheel, the lob and others).
- A press is a one-frame event: a press on a frame with `actionLock` not 0, or from an action that does not
  allow the result, is lost. Tests on a held button (the slap's windup, a jump) can still pass on later frames.
- The slap: holding the ball (`HAND_JOINED`), A held for 4 reads (holdTime 3 or more) starts the windup
  (`HAND_PREWHACK`); releasing A then hits (`HAND_WHACK`). Z down on any read during the windup ends it on the
  next frame without a hit: the glove keeps the ball (`HAND_JOINED`, lock 4) and A is latched until released
  (`MaintainAction`, hand.c). Measured: Z pressed on the windup's third frame, with Z not held before the
  glove took the ball, cancelled it (`tests/slapcancel.py`). Z pressed before the windup releases the ball.
- L on the ball, the walls and the other tricks: see "Tricks" below.

## Tricks

Numbers come from Python models of one game frame, replayed against emulator logs. The models are in
`tools/tricks/` (its README lists them). "Code" means read in gloveworld. "Measured" means seen in mupen64plus.
"Model" means the output of a model. The trainers (docs/plan.md, "Trick trainers") use these facts.

### One game frame, for the ball

- `MainGameLoop` saves `tempVect = ball.vel`, runs `UpdateActor` for the glove and the ball, then sets
  `ball.prevVel = tempVect`. `prevVel` is the ball's velocity at the start of the frame. Then come `ProcessInput`,
  `UpdateActorPositionDependencies`, `MoveBall` and `MoveHand`. L, Z, A and B are acted on in `MoveHand`.
  `MoveCam` and `PlayerSpinCamera` run after `MoveHand`.
- `ApplyAction(HAND_JOINED)` (from any action but THROW2CUP and BOUNCE2CUP) sets `ball.vel = ball.prevVel`, moves
  the glove to the ball, sets `hand.vel = ball.vel` and `actionLock = 4`. It restores the ball's velocity only.
  The frame's friction, push, gravity and collision no longer show in it. The position keeps the frame's move.
  (Code.)
- `ApplyAction(HAND_BALLWALK)` adds `hand.vel.xz * hand.mass / (ball.mass < 10 ? 15 : ball.mass * 1.5)` to
  `ball.vel`: two thirds of the glove's horizontal velocity for the normal ball. Measured exact for glove speeds
  4 to 14. (Code.)
- Every `ApplyAction` sets `ball.phys->moveSpeed = 13`. The next `ProcessInput` uses it. `MaintainAction` then sets 7
  (JOINED in the same frame, BALLWALK in the next). (Code.)
- `DealWithMaxSpeed(actor, max, old)` cuts a speed above `max` to `max` only when `old <= max`. Otherwise it
  multiplies by 0.9. In BALLWALK the ball is cut to `moveSpeed` only if it entered the frame at or below it. (Code.)
- Friction: ball 0.94 on the ground with the pad held, 0.76 with no pad on or in the ball, 0.985 in the air.
  Glove 0.6 with a plane under it, 0.95 without. (Code, measured.)
- The distance moved in a frame equals the speed at its start, cut to 26 (ball) or 14 (glove). A speed below 0.1 is
  set to 0. (Code, measured.)

### The pad and the hook

- The controller thread (priority 115) reads the pad and calls `ReadController` once for each frame message. The main
  thread (priority 10) wakes on the same message. `RunGame` calls `ClearText` for every message it gets, not only
  for the frame message.
- Measured over 984 game frames of random presses: the first `ClearText` run of a game frame saw other buttons than
  `ProcessInput` on 664 frames, and other presses on 697. The last run before `MainGameLoop` saw the same pad as
  `ProcessInput` on all 984 frames. (A run of 1,419 frames saw other buttons on 1 frame; docs/testing.md has both runs.) Practice code that needs the
  pad of a game frame reads it in `PracticeProcessInput` and takes the state of the glove and the ball from
  `PracticeClearText`.
- The glove, the ball and the camera hold the state that the last game frame left when `ClearText` runs.
  `ball.vel` is then what `ball.prevVel` becomes in the frame. `ball.pos - ball.prevPos` is the last frame's move.

### The pad's push

- `ReadController` cuts each stick axis to plus or minus 79, subtracts a dead zone of 5 (max 74), and reads the
  D-pad only if both axes are then 0. The D-pad sets each pressed axis to plus or minus 79. `Pad.mag` is the plain
  length: 111.7 diagonal, 79 straight, 74 on one stick axis (up to 104.7 at a stick corner). Pushes are proportional
  to `mag`. (Code, measured.)
- Push per frame: glove `mag / 36`; ball held `1.6 * mag / 120`; ball stood on `1.6 * mag / 180` (reversed on
  Medium and Hard unless the practice setting is on). Glove walking speed on the ground is `min(7, 2.5 * mag / 36)`:
  diagonal D-pad 7.000, straight 5.486, full stick 5.139 (measured). Stood-on speed limit `push / 0.06`: 16.55
  diagonal D-pad, 11.70 straight, 10.96 stick.

### L on the ball

- A stand press (L while holding) adds two thirds of the glove's velocity. A hold press (L while standing) restores
  the start-of-frame velocity and copies it to the glove. Neither tests `actionLock`. A ball in deep water ignores the
  hold press (`ball.flags & 0x200000` and `waterState`).
- A stand press from 7 with the diagonal D-pad gives 7.573, and 7.119 after friction. That is above 7. The cap of 7
  no longer applies, and the speed climbs to 16.55. A push below 0.867 (every pad but the diagonal D-pad and a
  stick corner) falls back to 7.
- An L press every 2nd game frame gives 24.92 distance a frame on the ground with the diagonal D-pad (measured
  24.924), against 16.55 for one press and 22.0 with the straight D-pad. A hold of 3 frames gives 16.1, 4 frames
  12.7. A stand of 3, 4, 6, 8 frames gives 23.80, 22.93, 21.70, 20.87 (model).
- Presses 1 frame apart merge into one. Presses 2 frames apart all register.
- The triple tap (stand, hold, stand, 2 frames apart) gives 10.5 (straight) and 11.3 to 11.5 (diagonal) on the third
  press from 7.0, against 7.1 for one stand press (measured, two studies).

### Momentum BZ and MAZ

- `holdTime[B] >= 5` starts THROWAIM (the 6th read of a held B) from JOINED or BOUNCE. `holdTime[A] >= 3` starts
  PREWHACK (the 4th read) from JOINED. BOUNCE has no PREWHACK: A gives AIRBOURNEWHACK at once.
- In THROWAIM and PREWHACK, `pad.button[Z]` (held) latches the button and calls `ApplyAction(HAND_JOINED)`. With B
  pressed on frame 0: aim after frame 5, cancel on frame 6, L on frame 7. With A: windup after frame 3, cancel on
  frame 4, L on frame 5. (Code; the B timeline measured.)
- A fresh Z press (not held) in JOINED or BOUNCE starts HAND_RELEASE, except while `actionLock` is not 0. The action
  table runs in numeric order. PREWHACK (5) comes before RELEASE (9): a fresh Z on call 3 or 4 of the A timeline
  cancels cleanly. THROWAIM (11) comes after: for B a fresh Z on call 5 or earlier drops the ball.
- L in BOUNCE, THROWAIM and PREWHACK does nothing. L on the cancel call is lost, and so is an earlier L from a BOUNCE
  start. From JOINED an earlier L is a normal stand press, and the aim never starts.
- Speed after the L frame: `(fb + 2/3 * fh) * V + a`. V is the ball's speed at the start of the cancel frame, fb and
  fh are the ball's and the glove's friction, and `a = 1.6 * mag / 120`. Measured: air 8.0 to 14.00, ground 7.0 to
  10.43 (the practice trainer reads 10.43, 9.09 and 8.01 for an L on time, 1 frame late and 2 frames late,
  `tests/trainers.py`). B loses 0.3 a frame late in the air (V 8) and 1.1 to 1.3 on the ground (V 7). The loss
  depends on V and the start state.
- Without Z the aim keeps the glove's slowed velocity: a ball at 8.0 goes 8.00, 2.28, 0.76, 0.25, 0.
- A Z that comes down one frame late cancels one frame late. On the ground the speed after the L is then 1.32 from
  7.0 (measured by the trainer test).

### Walls and the ball bearing

- R while holding the ball (no cheat 0x80, no cancer ball, snow 0.9 or less) starts HAND_SPIN. R alone changes the
  ball type about 8 frames later: rubber, bowling, bearing, crystal, rubber. Power, beach and snow go to rubber.
  Holding R repeats every 20 frames. (Code, measured.)
- The glove's collision flags (0x18) are cleared in JOINED, PREWHACK and BOUNCE and are set at the start of
  `MaintainAction` from the old action. The frame after leaving one of those actions has no wall check for the
  glove. (Code.)
- `CollideActorWithPlane` skips a face when the centre and `prevPos` are both behind it. A glove that has crossed a
  plane in an unchecked frame passes the wall, and walks back through from behind. (Code, measured.)
- MAZ-R clips the bearing (radius 6) through a wall when the cancel frame is 6 to about 23.65 units from it at speed
  7, R is read on the next frame, and the pad is held. The smallest speed that clips lies between 3.0 and 4.0 in the
  emulator (model 3.2). Rubber and crystal balls need a glove speed above 9.5. The bowling and beach balls cannot.
  B windup then Z then R does not clip. (Measured, about 1,400 trials; the ACOURSE wall x = 80 and a synthetic
  wall.)
- The window in numbers (model `tools/tricks/bearing.py`, bearing, head-on, R on the next frame): the top is
  `2.95 * S + 2.99` units from the wall's plane with the glove in the air (`planes[0]` clear) and `2.6 * S + 2.99`
  with it set; the least speed is between 3.1 and 3.2 (air) and between 5.0 and 5.5 (ground). A wall turned away
  from the ball's direction lowers the top: with the speed along the normal in place of S the model agrees to about 1
  unit up to 30 degrees, and gives no clip at 45 degrees or more for low speeds. The wall-clip trainer uses this
  rule. Measured in the emulator (`tests/trainers3.py`, a wall written into Atlantis 1's floor actor, speed 7.0, the
  glove in the air): cancel frames at D 21.0 to 23.53 clipped, 24.03 and more did not (model edge 23.64). The R was
  pressed on the frame after the cancel; the glove's centre went 13 to 17 units behind the plane.
- Other places that may pass a closed face: faces seen from behind are skipped, a collision check stops after 4
  faces, open mesh edges, and platform faces that are out of date. The Moon Pound (below) passes mesh vertices.

### The fist slam (Moon Pound)

- HAND_SLAM starts from JUMP on a Z press with `hand.vel.y > -9` after that pass's gravity. Z pass: action applied.
  Next pass: moves by `vy - g` (`g` is the variable `gravity`, 1.2 by default), then `vel.y = 0`. Two passes hold
  the height. From the 5th pass (R+4, counting the Z pass as the 1st) it falls exactly 14 a pass, with no fall
  damage. Hover height is `y + 2 * vy - g`. The radius grows 13, 13.75, 14.31, 14.73, 15.05, then to 16. (Code,
  measured.)
- Horizontal speed in the air is multiplied by 0.665 a jump pass and 0.3325 a slam pass with the stick neutral.
  One stick read moves the fall column about 0.07 units per raw unit (a D-pad read: 6 to 9 units).
- `CollideActorWithPlane` tests a face by the inside test (tolerance 0.001 of the triangle size) and an edge only
  when the centre projects inside its length. A mesh vertex is never tested. Where every edge at a vertex leads
  downhill, a sphere centre above the vertex touches nothing. When the centre was less than 1.89 units above the
  surface in the previous pass, the 20-step fallback uses `prevPos`, and the hole is about 0.2 times the height
  above the vertex. (Code, model; 26 of 26 fine trials.)
- With the state at the start of a game frame (the pad of that frame is already read), a Z read on the next frame
  gives the hover `y + 3 vy - 3 g`, and a Z read k frames later the same with the state moved k jump passes. The fall
  column is the glove's place after the 4th pass from the hook. The Moon Pound aim (`practice/src/moonaim.c`) agrees with
  the model on 88 of 88 cells over 11 jump frames above the dome, and the glove that follows its shift falls through the
  tip with Z on the cell it names, while the same Z with no shift lands (`tests/trainers_moon.py`). One tap of the stick
  that it shows, 5 frames before the column is read, moved the column to within 14 percent of the aim.
- Seams, steps, ridges and gaps narrower than twice the radius (30.5) do not leak (model).
- The hub's Space portal (`ootwpor`, levels 0 to 7) has its tip at (-7.3284, 60.6500, -1347.3239). A slam within a
  radius of 0.08 to 0.36 of it (by height above the tip) falls to the Space exit platform at (-13, -23, -1344)
  (levels 0 to 5) or (-7, -23, -1352) (levels 6 and 7). `CheckForExit` lets the exit start only if
  `ballStatic != 0`, `hubTarget == hubLevel`, or Space is complete. (Measured by setting the fields.)
- Scans of other levels find vertex holes: 70 in the hub, 17 in Atlantis 1, 20 in Carnival 1, 83 in Space 2A (model;
  29 of 31 spot checks went through).

### Bounces against surfaces

- The ball bounces on the first frame of contact. The new velocity is the reflected unit direction times the 3D
  speed times `phys.bounce` times the terrain bounce. The bounce is refused when the plane was already touched with
  `contactCount >= 1`, when `dot(dir, normal) > -0.3`, or when `dot > -0.8` and the ball was `onGround` on the
  previous frame (a touch of a plane with `normal.y >= 0.75`). (Code.)
- L acts after the frame's collision. A hold press on the contact frame restores the pre-impact velocity. On a floor
  or a gentle slope with a shallow arrival there is no bounce (on a slope, `b + g < 53.1` degrees, about 23 to 32
  degrees for the usual mash descent). On steeper faces the bounce comes one frame late and is 20 to 25 percent
  weaker. A stand press adds forward speed into a sloped face. Which press phase gives the strongest bounce depends
  on the slope: on the 22-degree ramp a hold press at c-1 with a stand press at c+1, on 51 and 64 degrees a stand
  press at c-1. (Measured on three real faces and injected slopes; the 64-degree face is not model-checked.)
- The glove's collision flag follows the action one frame late. It collides in the frame of a hold press and in
  the frame after it. A glove contact sets the glove's velocity to about 0 when the glove arrives with
  `dot <= -0.8`.

### The camera and C taps

- `PlayerSpinCamera` adds 16 units to `camera.vel`, sideways, on every frame a C-left or C-right button is held.
  The camera body has friction 0.26667. One frame of C turns the view `21.82 / D` rad (0.1664 at camera distance
  131). A long hold turns it 0.152 rad a frame. C-right is tested before C-left. Orbit, `destActive`, C-up (control
  method CAMERA) and `overrideTimer` return early, and nothing is added while `fallTime` is not 0. In angle-lock the
  C buttons still add the 16 unit push, and the anchor `focusAngle` is reset while a C button is held (camera.c
  447-481; not tested in the emulator).
- `camera.rot.y` is the angle from the target to the camera. The camera moves only along that line. The view
  line turns toward the target's sideways motion. `MoveCam` also swings the camera behind a moving glove (not the
  ball). Diagonal D-pad yaw rate: glove -0.063, ball held -0.030, stood on -0.030, L mash -0.048 rad a frame.
  Straight up or down: 0. (Code, measured.)
- The game's atan2 is a table (steps of 0.008 rad).
- On a diagonal D-pad the walker heads along the stick turned by the camera's yaw. A fall of the yaw bends the
  path to the right and a rise bends it to the left. The ball's velocity follows the push with a memory of about 16
  game frames (friction 0.94). A path measured while the yaw still turns is not the line that the pad gives.
  Closed loop (tests/trainers2.py, Atlantis 1, ball held, the model's advice acted on one frame after it is known):
  the yaw drifts 0.0002 rad a frame against 0.0242 with no C, C is down on 25 percent of the frames, and the path
  stays 3.8 units from its best line (24 with no C).

### How the camera follows

The camera has two stages (camera.c, `MoveCam`; actor.c, `UpdateActorPositionDependencies`). Both run once a game frame.

- The target point (`cameraInfo.actor.pos`) is what the camera looks at. Each frame it first gains 0.4 of the focus's
  velocity (the focus is the ball or the glove: `cameraInfo.focus`). It then moves toward the focus by at most
  `followSpeed` units, or onto it when it is nearer. `followSpeed` is 2 at a restart and grows by at most 1 a frame to
  20 (`followTimer` holds the growth). At walking speed the target is on the focus every frame.
- The camera's body (`camera`) keeps a distance from the target: `cameraInfo.distance` (it moves to the level's camera distance
  times the zoom) plus `levelHandScale * 10 / 0.05`. Beyond that it is pulled with `min(excess / 2, 10)` a frame, and its speed
  is held at 20 (`phys->maxSpeed` is set to twice the pull limit in `MoveCam`). It keeps 0.26667 of its speed each
  frame, so it cannot close a gap faster than about 13.6 units a frame.
- Every time the glove takes the ball (`ApplyAction`, `HAND_JOINED`, unless the glove comes from a throw to the cup),
  hand.c sets `followSpeed` to 0 and `followTimer` to 1. A mash takes the ball on every hold press, which is every second
  game frame. During a mash `followSpeed` is 0 and 2 on alternate frames, so the target gains 0.4 of the ball's
  velocity and 0 to 2 units, and the camera falls behind by about 11 units a game frame at the mash's 22 units a frame.
  (Measured, `tests/camspeed.py`: the target is 800 units behind after 110 frames.)
- Above 13.6 units a frame (a bounce MBZ-L reaches 14) the body is the limit: at 14 units a frame the camera ends 42 units
  behind its rest distance (51 at most).

### Where the start of Atlantis 1 is

- The glove starts at (-653, -18, 335) in water. The ball waits at (0, 8, 122.3) on a flat floor. The tests of the
  trainers put the glove next to the ball there.

## The bosses

The windows below were measured ("Measured": seen on the retail ROM in mupen64plus; "Code":
read in gloveworld, not isolated by a test). A "pass" is one run of `MainGameLoop` (20 a second). "Pass N" is the run
in which `frameCount` is N. No boss reads `difficultyLevel`. Every length below is a constant.

### How a touch becomes a hit

- `CheckActorCollisions` sets the flag `0x04000000` on an actor that the ball (or a shot, which is a ball-type actor)
  touches. `UpdateEnemies` clears it at the end of the enemy's turn. Code.
- A touch made by the ball's own update is read by `DoBoss` in the same pass (Prehistoric boss) or by the enemy's
  script in the next pass (Atlantis, Space). Code.
- A button that is down while `frameCount` is X is the one read for pass X + 1. Measured.
- Animations step in the draw thread. A drawn frame that is missed can move an animation based window by one pass.
  Code.

### Atlantis boss (level 13)

- Health: `enemies[0].state->hits`, 5 at the start. The whale takes 3 hits, the crab 1 and the fish 1. The third whale
  hit splits it (the crab and the fish start). Code.
- A whale hit needs the ball to touch the whale while the ball is in the air and not held, with the whale's
  `delayTimer` at 0. Code.
- After a hit the whale spins (`rotVel.y` 0.6, times 0.95 each pass). It can be hit again when the spin is below
  0.05: **49 passes (2.45 s)** after a hit. Measured: contact 48 passes after a hit gave no hit, 49 gave one. A touch
  before that costs nothing. Before the first hit the whale can be hit at any time. Measured.
- The crab dies to a second hit (a slam while it lies on its back) in the 219 passes after the flip. Measured with the
  game's own flag written, not with a real slam.
- The fish is stunned for 100 passes (5 s) after its landing. Measured. The slam that kills it was not run.

### Prehistoric boss (level 28)

- Health: 3. Only a ball hits it, in `DoPrehistBoss`. A hit needs the boss's animation to be neither 12 nor 3, a ball
  radius above 9 (a snowball) and a ball speed above 3 (x and z). Code.
- Every touch shatters the ball and gives it back to Glover, hit or not. Code.
- After a hit the boss plays animation 12, then 3, then 5. A touch counts when the animation is 5: **34 passes (1.7 s)**
  after the hit. Measured: contact 33 passes after a hit gave no hit and lost the snowball, 34 gave a hit.

### Space fight (level 40, the second Space boss)

- Glover is fixed and the stick aims. A fires a shot: no shield, fewer than 8 bubbles and a reload of 15 passes
  (`spbReloadCounter`). The shot reaches the boss about 22 to 25 passes after the press. Measured.
- Health: `portionHealth` (left shoulder, right shoulder, head), 3 each. A shoulder that reaches 0 takes one from
  `hits`. With both gone (`hits` 1) the head takes the last 3 hits. Code.
- A shoulder reads a shot as `path->moving` set on its platform. The head reads the flag `0x04000000` while `hits` is
  1. Every such touch uses the shot up. Code.
- After a hit to a part, that part counts the next touch from **20 passes** later (1.0 s). The parts are separate.
  Measured with the game's own contact result written 17 to 22 passes after a hit: 17 to 19 were thrown away, 20 to 22
  counted.
- A head hit while the boss's `scratch` is under `spbflytime / 2` sets the laser count `scratchF` to 0. In the head
  phase every attack is a laser. After `scratchF` reaches 0 the boss flies away for up to 360 passes (18 s). Code.
- The intro runs to about pass 700 (`cameoActive`, then control). Measured.

## The EEPROM

The game uses blocks 0 to 55 of the 4 kbit EEPROM: the cart ID (0), the hiscore table (1 to 6), the best times
(7 to 30), six save slots (31 to 51) and their CRCs (52 to 55). Blocks 56 to 63 are never used. The game checks
only block 0 and each structure's own CRC, so other data in 56 to 63 does no harm. All EEPROM work runs in the
controller thread, after its read, one request a frame (`eepromMessageQueue`).

The requests are numbers that `PostEepromMessage` puts in `eepromMessageQueue` (six places, `eepromMessageNum`
counts them) and the controller thread does, one a frame: 1 and 2 save and load the hiscore table, 4 and 5 the best
times, 6 and 7 the six save slots, 8 checks the cart ID, 9 saves it, 10 and 11 load and save the CRCs. `SaveCRC`
(11) computes every CRC from the RAM copy first: `UpdateCRCData` is a CRC-32 with the polynomial 0x04C11DB7, start
value 0 and no inversion, over each slot's 0x1C bytes, the 0x30 hiscore bytes and the 0xC0 best time bytes, and
writes the eight words at block 52 (0x34). At boot the game blanks (`blankSlot`, first byte 13, rest 0 except
difficulty 1) any structure whose CRC is wrong. All values are big-endian.

The save slot (`SaveSlot`, 0x1C bytes; `saveSlot[6]` is the practice warp's own slot and is never saved):

| Offset | Field | Meaning |
| --- | --- | --- |
| 0x00 | `name[3]` | the file's name (a blank file starts with 13) |
| 0x04 | `levels` | bit n: the level with `levelInfo[].bit` n was finished (set by `CheckForLevelCompleted`). Bits 5w to 5w+4 are world w's levels 1 to 3, the boss and the bonus level (the boss is the bit that makes a world complete) |
| 0x08 | `allGaribs` | bit n: every garib of that level was collected in one visit. One flag, not a count |
| 0x0C | `score` | the score plus 10 for each garib |
| 0x10 | `lives` | |
| 0x11 | `difficultyLevel` | 0 Easy, 1 Medium, 2 Hard |
| 0x12 | `hubTarget` | crystals won |
| 0x13 | `hubLevel` | crystals returned to the fountain (7 plays the ending) |
| 0x14 | `visited` | bit n: that level was started (`StartNewLevel`); nothing reads it |
| 0x18, 0x1A | `sfxVolume`, `musicVolume` | the option volumes, copied when a game starts |

`bestTimes[30]` (6 bytes each: `s16` time in whole seconds, `name[3]`, one pad byte) is indexed by `levelInfo[].bit`
and is shared by all files. A new game's blank table (`bestTimesCopy`) holds real times, not a "no time" value. The
practice ROM's save file editor changes these fields ([build.md](build.md#the-save-file-editor)).

## Frames and lag

- The scheduler sends the game frame message (9) every `desiredFrameRate` video frames (3 on NTSC) when no
  graphics task is pending. RunGame, ControllerProc and DrawGraphics each act on it once.
- `frameCount` counts game frames since the level started and stops while paused; `frameCount2` does not
  stop. `dispFrameCount` counts drawn frames.
- The draw thread builds a frame at the start of a game frame's message, before that message's logic runs. The
  glove and ball it sees therefore have the places they had after the previous game frame's logic, which is what a
  sample taken at `frameCount` in `ClearText` holds (the ghost, build.md, relies on it: the draw thread's glove at
  `frameCount` N equals the place kept at index N, and the test found the next game frame's place to be 8 units
  away).
- In mupen64plus a game frame takes 5 video frames (gloveworld's `tools/emu` notes), so a lag counter there
  counts 2 for every game frame.

## The draw thread

- `DrawGraphics` (main.c) builds a frame's display list in `dynamic[draw_buffer]` during one frame and sends its
  tasks at the start of the next (`SendLastFrame`, tasks 1 to `currentTask - 1`). So the draw thread always holds
  one built frame.
- It waits on `gfx_msgQ` only at the top of its loop; messages 9 (a frame) and 2 (a task done, `gfxTasks--`)
  come there.
- `StopDrawing` (banks.c) clears `codeDrawingRequest` and waits until the draw thread has cleared
  `gfxIsDrawing`, which it does at the start of a frame with no task pending. From then on its frames only clear
  the screen. The game calls it before it frees or builds a level; `StartDrawing` turns the world back on.

## Drawing

- Sprites such as garibs are texture rectangles at a place projected with the frame's camera (`PrintSprite`,
  overlay.c). They are not actors.
- Text from `NEW_TEXT` is made again every frame. The font has upper case letters only.
- The game leaves out objects outside its own 4:3 view.
- The level's fog is the struct `fog` (`on`, the colour, `min` and `max`; parse.c reads `on`, the colour and
  `min` from the level's fog token and keeps a copy in `originalFog`, which the water code fades back to).
  Only the drawing code tests `fog.on`: `SetRenderMode` (the fog geometry mode and the fog render modes),
  `RenderObjects` (the fog colour and position), the shadows, footprints, lightning and the sprite overlays.
  The logic writes it when a level loads. Atlantis 1 has fog on. The fog colour also fills the frame buffer
  before a frame is drawn (`ClearFrameBuffer`).

## The free camera

What the game draws depends on its own camera, not on the view matrix. The free camera (build.md) changes the
matrix only, so these limits hold:

- `CreateObjects` (object.c) sets each actor's `distSq` to its distance to `camera.pos`, and `IsActorVisible`
  drops an actor when `(radius + farPlaneDist)^2 < distSq`. Every drawn object is an actor (the landscape's
  pieces, platforms, enemies, debris, bullets), so the free camera shows only what lies within the far plane of the
  game's camera. The only other test in `DrawActor` is `alpha`. I found no test against the view's angle in the
  game's code; the RSP clips to the view.
- Sprites (garibs and others): `PrintSprite` (overlay.c) drops one that is farther from `camera.pos` than the far
  plane or nearer than the near plane, and sizes it by that distance. It places it with `cameraView` times the
  projection. A sprite sits where it should and has the size it would have from the game's camera.
- Fog, the highlights (`guLookAtHilite`) and the collision view follow the free eye. The collision view's own
  range is around the glove.
- Widening: `farPlaneDist` is read by the game's logic (enemy.c, game.c, light.c) as well, so setting it from the
  draw thread would change the game. Moving `camera.pos` for the draw would race with the logic. A cheap widening is
  not available; keeping the free camera within the far plane (1000 to 3000 units in the levels seen) of the game's
  camera shows everything.

## Collision

- The landscape's collision faces belong to the actors in `collisionList` (type `LANDSCAPE_ACTOR`). Each
  `CollFace` holds its plane (`n.p + d = 0`) and its triangle's corners in world space (`edgeStart`).
- A platform's faces are in world space too, but they are brought up to date (`RecalculatePlatformInfo`) only
  when something comes near the platform. A moving platform far from Glover keeps the faces of its last check.
- `CollFace.terrain` is a row of `terrain[]` (14 rows of slip, bounce, slide and gravity values). Read from the
  ROM: slip above 1 in rows 2, 8 and 11 (slippery), slip below 1 in rows 1, 3, 5, 9 and 10, gravity below 1 in
  rows 7 and 13; row 12 is water (water.c). The game's onGround test takes a normal with y of 0.75 or more as
  floor.
- The collision spheres are in `ActorObject.spheres`; `CalcCollisionSpheres` places each one at the actor
  (flag 0x20), at its own world position (0x10), or turned by the actor's rotation from it.
- The collision mesh of a landscape actor or a platform has a face i for each mesh face i, with corner j from
  vertex `mesh->faces[i][j]` (`InitPlanes`, `RecalculatePlatformInfo`). The meshes of Atlantis 1 are small (11 to
  164 faces for the landscape, 6 to 28 for the platforms) and have each corner once (no vertex twice), so two
  faces share an edge when they share two vertex numbers. `CheckForInnerEdges` sets `edgeLen` to -1 only for an
  edge shared with a coplanar face; an edge on the mesh's border or a crease keeps its length, so the game has no
  mark for "open".
- A platform's faces are brought up to date by `RecalculatePlatformInfo` (not for flag 0x20084). It runs from
  `CheckCollisions` for a platform within the sum of the checked actor's radius and the platform's radius, and
  from `UpdatePlatform` for a platform that sparkles. `Platform.lastUpdate` (u16) gets `frameCount`. In
  Atlantis 1 the bobbing platforms (flag 0, a path) move up to 5 units in y but their faces stay at the last
  check (for example y 58.00 against 53.98) until the glove is near enough for `CheckCollisions` to reach them.
- The enemies of Atlantis 1 are two lionfish (type 12), the general (11) and a mallet (10), all with flag 8; the
  scenery fish (type 47) have flag 4 only and no collision with actors.

## Moving the glove

- Each game frame, `MainGameLoop` calls `UpdateActor(&hand)`, then (in game modes other than the cameo)
  `ProcessInput`, `UpdateActorPositionDependencies`, `MoveBall` and `MoveHand`.
- `UpdateActor` adds `vel` to `pos` first (`UpdateActorPosAndRot`, which also keeps `pos` in `prevPos` and scales
  `vel` by the friction), then `UpdateActorCollisionAndGravity`. In that function flag 0x10 of `Actor.flags` runs
  `CheckCollisions` (the landscape and the platforms), flag 0x08 runs `CheckActorCollisions`, and the function
  returns before gravity when flag 0x01 is clear. The glove normally has 0x01, 0x08 and 0x10 (`hand.flags` is
  0x40091D at the start). Other code clears and sets these bits in some states (climbing, the catapult, the
  spells in hand.c). The practice ROM gives back only the bits it cleared.
- `UpdateActor` caps the vertical speed at 15 for actors without flag 0x1000000, and the fall speed at 15
  (-3.75 in water). Gravity is `gravity` (1.2 in a level), halved by flag 0x40.
- The practice code's start-of-frame hook runs before `UpdateActor`, so a velocity it sets is the one that
  moves the glove in that frame. A speed of 8 up moves the glove about 7 units a frame in game time.
- The stick turns into a direction in `ProcessInput`: forward is (sin a, cos a) and right is (-cos a, sin a),
  with a the camera's `rot.y` (`atan2(stickX, stickY)` gives the stick's own angle).
- With a ball in hand (`HAND_JOINED`) the ball is put at the glove by `UpdateActorPositionDependencies`, so it
  goes wherever the glove goes. A ball that is free keeps its own physics and stays behind.

## Demo recordings

The game's recordings (`.moo`, `LoadRecordKeyFile`) are a size word that counts itself, then pairs of a frame
number and a button word, ended by -1. Only the buttons are recorded, not the stick. The 44 files lie one after the
other in the ROM from 0x77BBE0 to 0x77DE20 (`recordKeyFilesRomStart` and `recordKeyFilesRomEnd`); file n belongs to
level n.

Which files are real (checked in the retail ROM):

| Files | Size | What |
| --- | --- | --- |
| 10, 15, 16, 20, 25, 30, 35 | 304 to 448 bytes | the recordings of Atlantis 1, Carnival 1, Carnival 2A, Pirates 1, Prehistoric 1A, Fear 1A and Space 1 (31 to 60 seconds each) |
| 39 | 208 bytes | the Space boss cutscene (`SPACEBOSS_TWEEN`) that the game plays itself after Space boss 1 (31 seconds) |
| 41, 42, 43 | 96 bytes | one short file, three times |
| 0, 24 | 16 bytes | empty: the first key frame is 0xFFFF, so the playback never ends |
| every other level | 176 bytes | one placeholder, the same bytes each time |

The title's attract mode (`DemoKeyRun`, title.c) cycles `demoLevel` 0 to 5: Atlantis 1, Carnival 2A, Pirates 1,
Prehistoric 1A, Fear 1A, then the flythrough. The game never shows Carnival 1 and Space 1.

How a demo starts (`DemoKeyRun`): save slot 6, Medium, `ActivateCheat(22, 0)` (all cheats off), `cheatCount = 1`,
`StartNewGame`, `autoPlayback = 2`, then `StartNewLevel(level)`. `StartNewLevel` loads the file. `ControllerProc`
calls `ReadControllerFromData` in place of `ReadController` while `autoPlayback` is 2. It gives the game the
recording's buttons and a centred stick. It leaves the demo on A, B or Start (not in the cutscene, not during a
fade), and at the end of the file after 20 game frames. The way out is `ExitLevel(QUIT_EXIT)`, which leads to
mode 1 and the title (`CheckForExits`); the cutscene leaves with `SUB_LEVEL_EXIT` for level 40 instead. With
`autoPlayback` 2 the level does not start its song, and the pad overlay (`controllerOL`) is drawn.

The Warp menu plays these recordings ([wiki/Warp.md](../wiki/Warp.md); `practice/src/demo.c`). A demo started from
a level uses the Time Trial's restart exit and sets the demo up when mode 8 starts the level. The practice code
sees a demo end by `autoPlayback` going back to 0 inside the playback hook. Playing the cutscene alone ends in
mode 8 with level 40, where the practice code goes to the title as a quit does. The 176-byte placeholder is
not offered: it is the same 18 seconds of a few presses in every level, a leftover, not a recording of that level.

## Checkpoints

- A checkpoint ("restart gate") is a platform with a `restartGate` (`TOKEN_RESTART` in parse.c). The level's
  platforms are a circular list, `platformList`; gate 0 is the level's start. `handControl.checkpoint` is the
  gate where Glover comes back.
- `RestartPlayer` (platform.c), called every game frame, brings Glover and the ball back when the gate's
  `restartTimer` is 65: the camera, the enemies, spells and the ball are reset there. `LoseLife` only sets that
  timer. The pause menu's checkpoint choice (UpdatePause) sets `handControl.checkpoint` and the timer; a gate
  never touched gets a camera place 100 units behind it. The practice ROM does the same.
- `StartNewLevel` rebuilds the gates; only gate 0 is touched again.

## Cheats

`cheatActivated` holds one bit per cheat (cheat n is `1 << n`; cheats 11, 12 and 22 act once and have none).
Many cheats act once when turned on (a ball type, gravity, the field of view), so the practice ROM toggles them
with the game's own `ActivateCheat(n, 1)`, which also checks `CheatAllowed` and shows the cheat's name.
`LoadLevel` clears the bits of Enemy ball, Low gravity, Big ball, Fish eye and Froggy. On Easy the game turns on
Locate garibs at the start of each level (game.c), and allows it after the game is won.

| n | Cheat | n | Cheat | n | Cheat |
| --- | --- | --- | --- | --- | --- |
| 0 | Locate garibs | 9 | Big ball | 18 | Boomerang ball |
| 1 | Call ball | 10 | Fish eye | 19 | Vanish ball |
| 2 | Checkpoints | 11 | Camera rotate R | 20 | Froggy |
| 3 | Infinite lives | 12 | Camera rotate L | 21 | Secret cheat |
| 4 | Power ball | 13 | Mad garibs | 22 | All cheats off |
| 5 | Control ball | 14 | Death spell | 23 | Level select |
| 6 | Infinite energy | 15 | Frog spell | 24 | Open portals |
| 7 | Enemy ball | 16 | Hercules | 25 | Open levels |
| 8 | Low gravity | 17 | Speed up | | |

## Random numbers

`Random` (maths.c) steps `rseed` with a shift and an exclusive or, and returns `rseed % lim`. `SeedRandom` sets
`rseed` to 0x6151F at the start of every level, so the numbers depend only on how many calls came before.

The draw thread's code calls `Random` too (for example `PrintStats` in a level). So the order of drawing and game
frames decides the numbers. The practice code that runs on the draw thread (the HUD, the menu, the collision
view) never calls `Random`, nor a game function that changes the game's state.

## Starting and finishing a game

- The difficulty screen (frontend2.c) sets `toStartGame` to 1 on the frame the player chooses Easy or Medium.
  It stays 1 until a later menu screen clears it. After the fade, `ActStartGame` calls `StartNewGame` and starts
  the intro (`INTRO`, level 45, mode 5). The intro's end leads to the hub. Hard is only for Time Trials.
- `saveSlot[].hubTarget` counts the crystals won; `hubLevel` counts those returned to the fountain. The
  fountain is in the cave (level 8), entered from the hub through exit type 12 (`CAVE_EXIT`). In the cave,
  `DoCaveStuff` adds one to `hubLevel`; at 7 it plays the ending and leaves through `GAME_WON_EXIT` to the
  outro.

## Pick-ups

`ActivateCollectable(type, time)` (collect.c) gives a pick-up as the levels do: spells 0 to 6, ball types 7 to
14 (enums.h, `Pickup`). `player.powerups[type]` holds its time left, or -1 for "until changed". Type -1 gives
back the rubber ball. The cheats give spells for 1000 game frames.

A ball type given in the first game frames of a level does not stay: the game sets the rubber ball again before the
level's intro ends (`player.levelFrames` still 0; seen in Atlantis 1 from the title). Given after the in-game timer
starts, it stays. The Warp menu's power-up waits for that (docs/build.md, "Warp with lives, garibs and a power-up").

## Sound

`musicVolume` and `sfxVolume` are the option volumes (0 to 160). Every game frame, `UpdatePause` moves the live
volumes (`musVol`, `sfxVol`) a tenth of the way towards them and calls `MusSetMasterVolume`. So the practice
ROM's mute sets the option volume to 0 and puts the old value back. `StartNewGame` copies the option volumes
into the save slot.

## Memory

- The heap runs from `jallocHeapStart` (0x802B0D10) to 0x80400000. `JallocInit` only resets the block list,
  which is in `jallocControl` (bss). Static blocks are taken from the top of the heap down. The heap's memory is
  untouched until the first allocation, which comes after MainProc's `CreateMessageQueues`.
- `dynamic` holds two `Dynamic` structs of 0x2B230 bytes, one for each frame buffer. Each has the frame's
  display list at offset 0x4100 (20,000 commands). `CleanupAndSendDisplayList` refuses a task of 20,000 bytes
  or more.
- `osMemSize` (0x80000318) is 0x800000 with the Expansion Pak and 0x400000 without it.

## ROM

- The retail ROM's data ends at 0x77DE20, after the demo recordings (`recordKeyFilesRomEnd`). The rest of the
  8 MiB, 0x821E0 bytes, is zero, and the game never reads it. The practice ROM puts the loader and the practice
  segment there ([build.md](build.md#the-free-end-of-the-rom)).
- Every asset address in the code and data is a symbol: the assets could move without breaking the game.
- The data holds 1,280 pointers into the rodata that splat writes as numbers, for example the language string
  tables (`startString`, `pauseString`). The rodata must not move.

## Glove actions and ball states

- The glove's action is `hand.state->mode` (`HandAction`, 45 values). `ApplyAction(action)` (hand.c) is the
  game's one function for starting an action: it saves the old mode in `prevMode`, sets the mode, clears
  `actionTime`, and does the action's setup in a `switch`. It is called from many places (the ball, enemies,
  pick-ups, the level exit), also outside `MoveHand`. It returns at once when `actionLock` is set during a cameo.
- `ConditionsMet` and `ActionAllowed` decide which action `MoveHand` asks for. `ApplyAction` itself checks
  neither, so it works from any state. The next `MoveHand` may leave the action again: duck and cartwheel hold it
  with a long `actionLock` (1036, 1029); idle and walk end at once when the stick says otherwise.
- Setup that reads things that may not exist: hopping reads `hand.link` (the enemy being ridden), frog tongue reads
  the bullet that `FireBulletFromActor` returns, cast spell reads `player.castSpell` and allocates two lights.
  These four are not offered. Release also calls `ReleaseBall` and a collision probe, so it needs the level to
  be running.
- The ball: `ballControl.state` is 0 normal, 1 held, 2 burst, 5 held special. `BurstBall` sets 2 (a bowling,
  bearing or crystal ball shatters instead). `DropBall` sets it back to 0. `ChangeBallInto(type)` changes the
  ball's type; the Power-ups menu gives types through `ActivateCollectable`.

## Actors made and moved at run time

The practice ROM's "Spawn actor" and "move actor" tools (`practice/include/spawn.h`) use these facts.

- A garib is not an actor. It is a sprite in a garib group (`garibGroupList`). When an enemy dies and drops one,
  `KillEnemy` makes the garib at run time in `NMEGaribGroup`: the group is made on the first drop, has a radius of
  30,000 (every garib in it can be collected from anywhere) and has `suspended` 0, so its garibs fall and
  bounce. The spawn tool makes a garib the same way. A garib of type 2 gives a life.
- `numGaribs` is the level's garib total. The level file adds each garib to it, and an enemy that drops a garib
  (`enemyStats[type].flags & 0x40`) too. `CheckForLevelCompleted` sets the level's all-garibs mark when
  `player.garibs >= numGaribs`. The spawn tool adds one for each spawned garib.
- A pick-up is made by `CreateNewPickup(type, pos, respawnTime, duration)`, the call that the level file's pick-up
  token makes. It is an actor of type 61 and also an entry of `collectableList`. It looks its model up with
  `FindObject(..., vital 1)`. A model that no object bank holds sets `gameInfo.mode` to 13 ("LEVEL UNPLAYABLE").
  The tool looks first with vital 0, which only looks. Atlantis 1 has nine of the fifteen models: stun, death,
  frog, suction, Hercules, speed up, rotor blades, boomerang ball and vanish ball. The level files never make
  pick-up 7 or 9.
- An enemy is made by `LoadEnemy` (enemy.c), which fills `enemies[numberOfEnemies]`. The array holds 30 enemies of
  0x300 bytes (0x802996E8 to 0x8029F0E8) and the game does not check the count. `LoadEnemy` ends in
  `RestartEnemy`, which starts the first instruction (`NMEGetNewInstruction`) when `frameCount` is not 0. At a
  level's load the instruction lists come from the level file after `LoadEnemy` returns, and `frameCount` is 0.
  The tool runs `LoadEnemy` with `frameCount` set to 0 and interrupts off, copies the lists from an enemy of the
  same type (each instruction in a new `JallocAlloc` block, which the level's free loop releases), then starts
  the first instruction. `MakeUniqueActor` gives the second enemy of a type its own copy of the model. The tag is
  -1, so no puzzle waits for it, and the confine box is off (`area.size.x` -1), as the level file does for an
  enemy with no box.
- `JallocAlloc` takes the static allocator when `frameCount` is 0 and the dynamic one otherwise. Both check the
  free bytes as `heapSize - staticUsed - dynamicUsed`. The static blocks grow down from the top of the heap and the
  dynamic ones up from the bottom, and neither allocator checks the gap between them. Holes left by freed dynamic
  blocks count as free bytes but are not part of that gap, so an allocation that passes the check can overlap the
  other region. A static block made in the middle of a level (the tool's `LoadEnemy` runs with `frameCount` 0)
  is exposed to this. An allocation that fails stops the game for good: `Crash("JALLOC")`, then a loop with
  interrupts off. The heap also has only 0x500 blocks. Atlantis 1 has 896 of them free and a gap of about
  380,000 bytes at its start. A garib or pick-up takes 2 to 4 blocks and up to 600 bytes. An enemy takes up to 23
  blocks and about 2,100 bytes. Before each spawn the tool checks `jallocControl`: the blocks between
  `lastStatic` and `firstDynamic`, and the gap from the lowest static block to the end of the highest dynamic
  block, must cover the estimate for the kind and a reserve (32 blocks, 0x8000 bytes). Otherwise it shows "Not
  enough memory to spawn". A level also allows 100 spawns. The tool does not use the dynamic allocator for the
  enemy's blocks: `RestartEnemy` tests `frameCount` too, so `LoadEnemy` needs it at 0 as well.
- `LoadEnemy` runs on the game thread with interrupts off in the tool, and `FindObject` (vital 0) runs on the menu
  thread when the view opens, which is allowed only while a level runs and the game is not paused. Nothing they
  call waits on a message queue.
- What is left out, and why:
  - Platforms. `ParseLandscapeFile` builds one from a series of tokens: the actor, its collision faces
    (`Plat_Plane`), the flags, then each feature (path, spin, orbit, parent, tag, exit). A final pass over the
    whole list gives flag 0x20000 to the platforms with no motion. Rope bridges and puzzles refer to platforms by
    tag. A platform made without all of this has no faces or a wrong state.
  - Landscape and water actors. `InitPlanes` builds their faces after the level file is read, and water has none.
  - Boss and scripted enemy types: 35 to 42 (the Selwyn bosses, the robots, Spank, baby Spank, the evil glove) and
    58 (the frog an enemy turns into). Their fights run from `enemies[0]` to `enemies[2]` and from the level's
    script (nmeprocess2.c, boss.c).
  - Enemy types that the level has not loaded. The model is missing, and the instruction lists that the tool
    copies do not exist.
- Moving an actor writes `pos`, `prevPos` and `platPrevPos` (the last is what carries things that stand on a
  platform), and sets the velocity of a non-platform to 0. The camera, the glove, the ball, landscape, water and
  cameo actors are not moved: the first three have their own code, and the faces of landscape and water are built
  once.
- Platforms' collision faces are built from `Actor.pos` by `RecalculatePlatformInfo`, which `CheckCollisions` and
  `Altitude` call for a platform near the checked actor when `lastUpdate < frameCount`. It returns at once for a
  platform with a flag in 0x20084. The end of `ParseLandscapeFile` gives 0x20000 to every platform with no path,
  motion, orbit, push, scale, link, crumble or parent, so its faces are built once. `bonus.c` and `game.c` clear the flag
  for a platform that starts to move. The move tool clears it too and sets `lastUpdate` to 0. A platform with a
  path goes back to its path: `MovePlatform` accelerates it from where it is to its next point. A platform with
  a spin or an orbit is placed by that motion again. Only a platform with no motion is tested.
- The save: `CheckForLevelCompleted` writes `player.score + player.garibs * 10` to the save slot. A collected
  spawned garib adds to the garib count and the score, a collected power-up adds 300 to the score
  (`UpdatePickups`), and a beaten spawned enemy adds its score. These stay in the save once the level ends.

### Editing an actor

Inspect, Actors, an actor's page, Start (`practice/src/actorview.c`) changes the actor's `pos`, `vel` and `rot` floats
(steps 0.01 to 100) and the `hits`, `damage` and `mode` of its state in place, while the menu holds the game. A float that
is not safe for the FPU (`safe.h`) is replaced by 0 on the first change. The edit calls `ActorsModified` (the HUD's marker
until the level ends) and taints the run with `BT_ACTORS`. `tests/actoredit.py` checks each kind of field on an enemy of
Atlantis 1.

## Code that nothing calls

`TitleScreen` (game.c, 0x5BC bytes), `ScreenShot` (personal.c, 0x27C bytes) and `ClearStaticMemory` (main.c,
0x74 bytes) are never called, and no pointer to them exists. The practice ROM's boot hook takes the place of
`ClearStaticMemory` ([build.md](build.md#hooks)). `TitleScreen` stays where it is, and the practice ROM calls it
(below).

## The early title menu

`TitleScreen` (game.c) is the team's first title menu. It is a replacement for `DoMenu`, which `RunGame` calls once a
game frame while `gameInfo.mode` is 1.

- Every call sets `gameInfo.mode` to 1. When `levelSelectCount` is 0 it starts: `LoadLevel(7)` (the level numbered
  7, `HUB8`) without freeing anything, `fog.on` 0, a fade in, and the version string "V25 08AUG98" at the top. The
  level shows with `TitleScreenCameraSpin`; the world is drawn once `frameCount` passes 25. It calls `UpdatePause`.
- Three entries from `menuStrings[0..2]`, which the retail game words "START", "PRACTICE" and "OPTIONS". The stick up
  and down choose, any debounced press fades out and takes the entry. Entry 0 runs `ContinueGame` and
  `StartNewLevel(gameInfo.level)`: it continues the current save slot at its hub level (after a warp from the
  title, slot 6, which is empty, so hub 1). Entry 1 runs `StartNewGame` and starts `INTRO`. Entry 2 sets mode 2:
  `LevelSelectScreen`. The string "OPTIONS" does not match what the entry does.
- It needs no state that the retail menus set up. It needs a mode 1 with `levelSelectCount` 0, and a freed level
  (or none) before it starts. It works from the title flythrough, from a level that was left through the Quit exit, and
  from the normal title once `KillTitle` freed what `DoMenu` made.
- In mode 1, `levelSelectCount` is 0 until `DoMenu` or `TitleScreen` has set up, and again after the exit that leads
  to the title and after `KillTitle`. Nothing is loaded then: `KillTitle` and `FreeLevel` must not run.
- It does not choose music: the song that played before goes on.
- `FreeLevel` called when no level is loaded never returns. The normal title (`DoMenu`) has no level: the exit that
  led to it freed the level. `DoMenu`'s own objects and overlays are freed by `KillTitle`, which every start of a
  level from the title menu calls first (`ActContinueGame`). The practice ROM's Warp used `FreeLevel` there and hung
  until this was found.
- The game's way back from the flythrough to the normal title: `CheckForExits` sets mode 1, `currentScreen` 1,
  `booting` 0 and `disablePause` 0 when a level exit ends in `FLYTHRU`.
