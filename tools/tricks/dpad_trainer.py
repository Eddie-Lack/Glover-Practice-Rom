"""The decision rule of trainer (b), written so that the model and the emulator runs use the same code.

It needs, after every game frame, only values the game keeps in RAM:
  camera.rot.y, camera.vel (x, z), camera.pos (x, z), cameraInfo.actor.pos (x, z) and the C-left / C-right buttons
  the player used in the frame before.

practice/src/trainers.c (LineStep) is a port of this file. tests/trainers2.py feeds both the same frames.
"""
import math

CAM_FRICTION = 0.26667     # the camera body keeps this part of its speed each frame (camera.c)
TAP_UNITS = 21.82          # 16 / (1 - CAM_FRICTION): the camera's travel after one frame of C; the view turns TAP_UNITS / D


def wrap(a):
    return (a + math.pi) % (2 * math.pi) - math.pi


class Trainer:
    def __init__(self, thr=0.10, look=2.0, smooth=4):
        self.thr, self.look, self.smooth = thr, look, smooth
        self.ref = None
        self.prevE = None
        self.hist = []
        self.E = 0.0
        self.Edot = 0.0
        self.lead = 0.0

    def arm(self, crot):
        """Sets the reference yaw (the line to keep)."""
        self.ref = crot
        self.prevE = None
        self.hist = []

    def pending(self, crot, cvel, cpos, tpos):
        """The yaw the camera adds by the velocity it already has (to the right of its view is positive)."""
        right = (-math.cos(crot), math.sin(crot))           # PlayerSpinCamera: C-right pushes the camera this way
        lat = cvel[0] * right[0] + cvel[1] * right[1]
        d = math.hypot(cpos[0] - tpos[0], cpos[1] - tpos[1])
        return lat / (1.0 - CAM_FRICTION) / d

    def update(self, crot, cvel, cpos, tpos, tapped):
        """tapped: 'R', 'L' or None, the C button the player held in the game frame before the hook's: its tap is
        already in cvel. Returns the arrow."""
        if self.ref is None:
            return None
        d = math.hypot(cpos[0] - tpos[0], cpos[1] - tpos[1])
        tap = {"R": TAP_UNITS / d, "L": -TAP_UNITS / d}.get(tapped, 0.0)
        E = wrap(crot - self.ref) + self.pending(crot, cvel, cpos, tpos)
        if self.prevE is not None:
            self.hist.append(E - self.prevE - tap)
        self.prevE = E
        h = self.hist[-self.smooth:]
        self.Edot = sum(h) / len(h) if h else 0.0
        self.E = E
        self.lead = E + self.look * self.Edot
        x = self.lead
        return "R" if x < -self.thr else ("L" if x > self.thr else None)
