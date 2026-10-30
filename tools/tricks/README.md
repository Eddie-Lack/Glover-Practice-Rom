# Models of the speedrun tricks

Standalone Python models of one game frame of Glover (USA), behind the trick trainers
(docs/plan.md, "Trick trainers"; the facts are in docs/game.md, "Tricks"). Each file names the C functions of
gloveworld that it follows. They need Python 3 and, for some, numpy. They do not touch a ROM.

The models were checked against logs from mupen64plus (about 40 runs for the L mash, 1,400 trials for the wall
clip, 277 for the bounce). The logs, savestates and pictures are not in the repository. The self-tests that read
them do not run here. A model whose log is missing says so and stops.

| File | What it models | Used by |
| --- | --- | --- |
| `lmash.py` | the ball and the glove while L switches between holding and standing on the ball (`Sim`, `run_pattern`) | `MashTarget` in `practice/src/trainers.c`; `tests/trainers.py` |
| `mbz.py` | the glove's actions through the Z cancel of the throw aim and the slap windup, and the L after it (`Game`) | MBZ-L trainer |
| `dpad.py` | the pad, the walk, the camera and the C taps | line keeper |
| `dpad_trainer.py` | the line keeper's decision rule | `LineStep` in `practice/src/trainers.c`; `tests/trainers2.py` |
| `bearing.py` | MAZ-R into a wall with the ball bearing | none yet |
| `moonpound.py` | the slam against the collision faces (`CollideActorWithPlane`, `NearEdges`) | `Collide` in `practice/src/moonaim.c`; `tests/trainers_moon.py` |
| `moonpound_trainer.py` | what the Moon Pound aim computes in one game frame | `MoonAimStep`; `tests/trainers_moon.py` |
| `cone.py` | the dead cone of a mesh vertex and the hole candidates | `BuildCands`; `tests/trainers_moon.py` |
| `moonpound_vertices.py` | the same collision test for many columns at once (numpy) | Moon Pound aim |

`moonpound.py` reads a dump of a level's faces (`out/hubN.json`, in `load_faces`: landscape actors and platforms with
their collision faces). The dumps hold level data and are not committed.

## The numbers the trainers use

L mash, diagonal D-pad on flat ground (steady move of the ball a game frame):

| Pattern | Distance a frame |
| --- | --- |
| press every 2nd game frame | 24.92 |
| one stand press | 16.55 |
| walking on the ball | 7 |
| hold of 3 frames | 16.1 |
| hold of 4 frames | 12.7 |

The straight D-pad gives 22.0 with the same rhythm.

MBZ-L from the ground at speed 7, forward D-pad (the trainer test reads the same numbers):

| L frame | Speed after the L |
| --- | --- |
| on time (frame 7 for B, 5 for A) | 10.43 |
| 1 late | 9.09 |
| 2 late | 8.01 |

Check: `lmash.run_pattern(range(0, 300, 2), 300, stick=(79., 79.))` has a mean move of 24.92 over frames 100 to 139.
