# Inspect

_Back to the [Home page](Home.md)_

Views of the game's state

| Option | What it does |
| --- | --- |
| Actors | The list of the level's actors, see below |
| Memory | A memory viewer and editor, see below |
| Glove and ball | The glove's and the ball's timers, counters and contacts |
| Frame log | The frame log, see below |
| Frame log records | Record the frame log |
| Show the watch list | Show the memory watch list on the HUD |
| Collision view | _Sub menu_ |
| Hide objects | _Sub menu_, see below |
| Spawn actor | Spawn an actor, see below |
| Show missed taps | Show taps that the game does not read, see below |
| Memory corruption | _Sub menu_ |

## Actors

Lists the level's actors with each actor's position, speed, rotation and state. Each row shows its number, type, model and x, y, z. Garibs aren't actors, so they aren't in the list. Hold Z to move 10 rows. The chosen actor stays chosen when you open the list again

A page for one actor has options to:

- move Glover to the actor (A),
- move the actor to Glover (Z on its page),
- edit the actor (Start on its page)

Moving only works while a level runs, and Z not while the game is paused

Editing: `Up` and `Down` choose a field, `Left` and `Right` change it, `C up` and `C down` change the step. Start or B leaves the editing

| Field | Step |
| --- | --- |
| `pos x, pos y, pos z` | `0.01, 0.1, 1, 10, 100` |
| `vel x, vel y, vel z` | `0.01, 0.1, 1, 10, 100` |
| `rot x, rot y, rot z` | `0.01, 0.1, 1, 10, 100` |
| `hits, damage, mode` (the state) | whole numbers, at least 1 |

Editing is a modified game! It can crash

The moved actor goes in front of Glover. The HUD shows "MODIFIED actors" until the level restarts or changes. That goes for edits too

## Spawn actor

Spawn an actor that the level has (a garib, a power-up or an enemy) in front of Glover. The HUD shows "MODIFIED actors" until the level restarts or changes

A spawned garib, power-up or enemy that Glover collects or beats adds to the score and to the garib count that the game saves at the end of the level

The menu has a warning - "Modified game! May crash. Edits your save"

## Memory viewer

Shows and edits memory. Edits make the HUD show "MODIFIED". Z marks a word as a poke (see **Memory corruption** below). A watch list on the HUD can show chosen words

| Button | What it does |
| --- | --- |
| Left, Right | Choose a digit of the address |
| Up, Down | Change that digit |
| R | Edit the value at the cursor. The value has its own digit, which starts on the lowest. R again goes back to the address |
| C left, C right | The size of the value at the cursor: 4, 2 or 1 bytes. The top line shows it in hex and as a signed number |
| A | Add the value at the cursor to the watch list (4 at most). This turns "Show the watch list" on |
| L | Clear the watch list |
| Z | Mark the value at the cursor as a poke |
| Start | The list of pokes |

In the grid, watched words are green and poked words are orange. The practice code's own memory can't be edited: its values show grey while you edit

"Random numbers: set (frozen)" in the Game menu opens a small editor of the frozen seed instead: Left and Right choose a digit, Up and Down change it. It is not a poke, and it only stops the level's run from counting for a best time

## Frame log

For each game frame of the glove, the log records:

- the buttons the game read (presses in yellow; buttons the game ignores until they are released in grey),
- the glove's action and how long it has lasted,
- the lock that keeps a new action from starting,
- the time in which the game reads no buttons

Turn on "Frame log records", do a trick, then open Frame log to see on which frame each input landed

The log keeps the last 256 game frames (about 13 seconds). Each line is numbered with the level's game frame, the number the input history uses. Left and Right turn 16 lines at a time

## Collision view

The sub menu "Collision view" has these options:

| Option | What it does |
| --- | --- |
| Collision view | off, see-through or on surfaces |
| Radius | Which faces are drawn: whole level, 1000, 500 or 250 units around Glover |
| Colours | by terrain, by kind or by object |
| Back faces | How a face seen from behind is drawn: faint, hide or same |
| Faces shown | _Sub menu_: show or hide each kind of face |
| Wireframe | Outline every face: off, over faces or alone |
| Face info | Show the face under the glove and the face in the middle of the screen |
| Faces drawn | How many faces are drawn right now |
| Collision spheres | Draw the glove's (cyan) and the ball's (blue) collision spheres |
| Enemy collision spheres | Draw the spheres of the enemies, in orange |
| Open collision edges | Draw edges that belong to one face only, as pink bands |
| Moon Pound holes | Draw a violet post on each hole where a falling glove touches no face |

It draws the faces that the game collides with: the level's landscape and its platforms. Moving platforms are drawn where they are on each frame

"See-through" draws the faces over everything. "On surfaces" draws a face only where nothing in front of it hides it

When you enter a level, the view takes a second or two to read the level's collision. "Faces drawn" shows "building" until it's done. Outside a level it shows nothing

"Radius" counts an object when any part of it comes that close to Glover, so faces further away can show up too

### Colours

"By terrain":

| Colour | Surface |
| --- | --- |
| green | floor |
| yellow-green | floor too steep to stand on: Glover slides |
| blue | wall |
| red | ceiling |
| cyan | water |
| pale blue | slippery ground |
| magenta | sticky ground |
| orange | low gravity |

"By kind" draws floors green, walls blue and ceilings red, whatever the terrain

"By object" gives each object its own colour, never yellow or white, which mean the touched and the centre face. Use it to see where one object ends and the next starts

In every mode:

| Colour | What it is |
| --- | --- |
| white | a face that Glover or the ball touches |
| yellow | the face in the middle of the screen, with "Face info" on |
| pink band | an open edge |
| violet post | a hole _(see below)_ |

A floor is a face that can carry Glover. A face steeper than about 73 degrees is a wall, and a face that faces down is a ceiling

### Back faces

A face is solid from one side only. You see its back when you look at it from the inside of a wall or from below a floor

- "faint" draws the back of a face more see-through
- "hide" doesn't draw it
- "same" draws it like the front

### Faces shown

Hide floors, walls, ceilings, water, slippery ground, low gravity ground, the landscape or the platforms. Everything is shown by default

### Wireframe

Draws the edges of every face as thin lines, in the face's colour

- "over faces" draws the lines on top of the faces
- "alone" draws only the lines

### Face info

A panel near the top right shows two faces:

- "Glove": the face Glover stands on. In the air it shows the last one in grey
- "Centre": the face in the middle of the screen, under the small cross. With the collision view on it's drawn yellow. In big levels it takes a few frames to catch up when the camera turns

For each face:

| Line | What it shows |
| --- | --- |
| first | the kind (flat floor, steep floor, wall or ceiling) and the slope in degrees. "(back)" if you see it from behind |
| second | its terrain number and type, and for "Centre" how far away it is, in units |
| third | landscape or platform, the object's model name, and the face's number in that object |

The panel hides while the practice menu is open. Its background follows the Menu opacity setting

"Centre" only finds faces that the view shows: the "Faces shown" menu and "Back faces: hide" leave faces out. It also works with the collision view off. With the free camera it looks where the free camera looks

### Open edges

An open edge belongs to one face only: the side of a platform, the rim of a floor with nothing below it

In the biggest levels there isn't room for every open edge. The view draws the ones nearest to Glover first. Turn "Open collision edges" off and on again to draw the edges around where Glover is now

## Moon Pound holes

Some mesh vertices have a tiny hole above them. A glove that falls on it touches no face and goes through the floor _(see [Trainers](Trainers.md), Moon Pound aim)_

"Moon Pound holes" marks each one with a violet post, 24 units tall. The post stands on the hole's centre

- It shows the holes within about 50 units of Glover, and up to 64 of them
- It needs the collision view to be on
- It finds the holes over a few game frames, when you move 24 units or more, and every 100 game frames
- It also works in noclip

It can add lag (up to 3 ms a game frame)!

Note: the hole itself is about 0.4 units wide, so aim at the post's base 

## **The collision view can add lag!**

A whole level of faces is a lot of see-through triangles. If the game slows down, use a smaller radius or hide some kinds of faces

The wireframe draws every face a second time, so it adds more. Face info adds a little too

## Hide objects

Leave kinds of objects out of the picture, one at a time. Each item shows `shown` or `hidden`; A switches it

| Option | What it hides |
| --- | --- |
| Landscape | The ground and walls that Glover collides with |
| Scenery | Landscape that Glover doesn't collide with, background pieces and cameos |
| Water | Water surfaces |
| Platforms | Platforms, moving or not |
| Glove | Glover |
| Ball | The ball, its plasters and its extra copies |
| Enemies and shots | Enemies and the things they shoot |
| Pick-ups and garibs | Power-ups, the power-up that circles the glove or ball, garibs (also the garib that the Locate cheat shows) |
| Debris | Debris and particles |
| Sky | The sky behind the level |
| HUD | The game's counters and their numbers: garibs, lives, health, score. Text and the pause screen stay |
| Shadows | Every shadow |
| Footprints | Footprints and blast rings |
| Trails | The trails behind fast things |
| Lightning | Lightning |
| Show everything | Show every kind again |

- The game's logic is the same. Only the picture changes _(see the last point about lag)_
- Hidden objects still collide. Use the collision view to see what Glover touches _(see above)_
- With the landscape hidden, "On surfaces" in the collision view also shows faces that the landscape would block
- The game has less to draw, so it can lag less. That can change some random numbers. A run with anything hidden doesn't count for a practice best time

Hidden kinds are not saved: they come back when the console restarts

## Show missed taps

This needs the input display _(see [HUD](HUD.md))_

The game reads the controller once a game frame. If a button tap starts and ends between two reads, it never reaches the game. With this on, the console also reads controller 1 about every 5 milliseconds. The input display shows the buttons of missed taps in orange, with a count

This changes the game's timing slightly. Use it for study and not for timed practice. If a read fails, it turns itself off. The menu has a "Timing changes! Needs the input display" warning

## Memory corruption

`"Modified game! May crash".`

The HUD shows "MODIFIED pokes" while pokes are on. After that, "MODIFIED" stays until the level starts again

| Option | What it does |
| --- | --- |
| Pokes | The list of eight pokes, with how many are set and running. In the memory viewer, Z marks the word at the cursor as a poke, and Start lists the pokes |
| Apply once pokes | Write the pokes in mode "once" |
| Flip region | Where "Flip one random bit" works: the glove's actor, the ball's actor, a random actor of the level, or the memory view range |
| Range size | 16, 256, 4096 or 65536 bytes from the viewer's address |
| Flip one random bit | Flip one bit in the chosen region |
| Last flip | The address, the bit and the count of the last flip |
| Clear all pokes | Remove every poke |

Each poke has a mode:

- once: written by "Apply once pokes"
- freeze: written every game frame
- random: the low bits are set to random bits
- flip: one random bit of the low bits is flipped

C left and right set the poke's size. C up and down set the bits. A C button counts when it is let go on its own, so the menu's four C buttons don't change a poke. L and R set how often it acts. Writes only go to RAM, never to the practice code and its data. The game will probably crash
