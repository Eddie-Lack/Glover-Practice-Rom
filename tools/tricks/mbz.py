"""A standalone model of the glove and the ball for the MBZ study (Glover USA, retail code).

It follows the order of one game frame in MainGameLoop (main.c) for the glove and the ball:
    ReadController -> UpdateActor(hand) -> UpdateActor(ball) -> ProcessInput -> UpdateActorPositionDependencies
    -> MoveBall -> MoveHand (MaintainAction, the action lock, the action table).
Only what the MBZ, MAZ and MBZ-L studies need is modelled. Each function names the C function it follows.

Not modelled: collisions with the level (the model has a flat floor or none), platforms, water, enemies, spells,
the glove's rotation, the camera. The camera's yaw is a parameter. The glove snaps to the ball each frame when a
stick or D-pad direction is held (MoveHandToBall with scale 1).

`python3 mbz.py` is a self-test against emulator scenarios in data/. The repository does not hold them (README.md).
"""
import json
import math
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
PI = math.pi

# HandAction (enums.h)
(IDLE, WALK, JUMP, JOINED, THROW, PREWHACK, WHACK, SKIDDING, DUCK, RELEASE, GETUP, THROWAIM, THROW2CUP, FALLING,
 BOUNCE, BOUNCE2CUP, AIRWHACK, THROW2LAND, SLAM, SLAM2ST, BALLWALK) = range(21)
NAMES = ("IDLE WALK JUMP JOINED THROW PREWHACK WHACK SKIDDING DUCK RELEASE GETUP THROWAIM THROW2CUP FALLING "
         "BOUNCE BOUNCE2CUP AIRWHACK THROW2LAND SLAM SLAM2ST BALLWALK").split()
# pad button indices
A, B, Z, START, L, R = 0, 1, 2, 3, 4, 5
DLEFT, DRIGHT, DUP, DDOWN = 6, 7, 8, 9
BTN = {"A": A, "B": B, "Z": Z, "START": START, "L": L, "R": R, "LEFT": DLEFT, "RIGHT": DRIGHT, "UP": DUP,
       "DOWN": DDOWN}

GRAVITY = 1.2
LEVEL_HAND_SCALE = 0.05


def F(x):
    """Round to a 32-bit float, as a store into a game variable does."""
    return float(np.float32(x))


def aabs(a):
    """Aabs (maths.c): an angle into [0, 2 pi)."""
    while a < 0:
        a += 2 * PI
    while a >= 2 * PI:
        a -= 2 * PI
    return a


def len2(v):
    return math.sqrt(v[0] * v[0] + v[2] * v[2])


class Actor:
    def __init__(self, maxSpeed, moveSpeed, mass, accel, phys_friction):
        self.pos = [0.0, 0.0, 0.0]
        self.prevPos = [0.0, 0.0, 0.0]
        self.vel = [0.0, 0.0, 0.0]
        self.prevVel = [0.0, 0.0, 0.0]
        self.friction = phys_friction      # Actor.friction
        self.phys_friction = phys_friction  # ActorPhysics.friction
        self.maxSpeed = maxSpeed
        self.moveSpeed = moveSpeed
        self.mass = mass
        self.accel = accel
        self.planes0 = False   # obj->planes[0] != NULL (touching a surface)
        self.onGround = False
        self.flags_gravity = True
        self.half_gravity = False
        self.radius = 9.0


class Pad:
    """ReadController (controller.c): the pad[] of the active controller."""

    def __init__(self):
        self.button = [0] * 14
        self.prevButton = [0] * 14
        self.pressed = [0] * 14
        self.latch = [0] * 14
        self.holdTime = [1000] * 14
        self.sincePress = [999999] * 14
        self.stickX = 0
        self.stickY = 0
        self.mag = 0.0
        self.prevMag = 0.0
        self.prevMag2 = 0.0
        self.angle0 = 0.0

    def read(self, down, stick=(0, 0)):
        """DOWN: the set of button indices held on the controller; STICK: the analog stick after the dead zone."""
        self.prevMag2 = self.prevMag
        self.prevMag = self.mag
        self.stickX, self.stickY = stick
        for i in range(14):
            self.prevButton[i] = self.button[i]
            self.button[i] = self.pressed[i] = 0
            if i in down:
                if not self.latch[i]:
                    self.button[i] = 1
            else:
                self.latch[i] = 0
            if self.button[i] and not self.prevButton[i]:
                self.pressed[i] = 1
            if self.prevButton[i] == self.button[i]:
                self.holdTime[i] += 1
            else:
                self.holdTime[i] = 0
            if self.pressed[i]:
                self.sincePress[i] = 0
            else:
                self.sincePress[i] += 1
        if self.stickX == 0 and self.stickY == 0:
            if self.button[DUP]:
                self.stickY = 79
            elif self.button[DDOWN]:
                self.stickY = -79
            if self.button[DLEFT]:
                self.stickX = -79
            elif self.button[DRIGHT]:
                self.stickX = 79
        self.mag = F(math.sqrt(self.stickX * self.stickX + self.stickY * self.stickY))
        if self.mag > 0:
            self.angle0 = aabs(F(math.atan2(self.stickX, self.stickY)))


class Game:
    """The glove, the ball, the pad and the glove's action machine."""

    # defaultBehaviour[type] of the ball (actor.h ActorPhysics): mass, accel, maxSpeed, moveSpeed, friction, radius
    BALL_TYPES = {0: (10.0, 0.5, 26.0, 13.0, 0.94, 9.0),    # rubber ball
                  1: (14.0, 0.26455, 26.0, 13.0, 0.97, 14.0),  # bowling ball
                  2: (7.0, 0.75, 26.0, 13.0, 0.90, 6.0),     # power ball
                  3: (10.0, 0.5, 36.4, 18.2, 0.95, 6.0),     # ball bearing
                  4: (3.0, 0.5, 26.0, 13.0, 0.70, 25.0)}     # beach ball

    BOUNCE_MAX = {0: 8.0, 1: 7.0, 2: 8.0, 3: 7.0, 4: 6.0, 5: 8.0}   # BallBounceMaxSpeed (ball.c)

    def __init__(self, cam_yaw=PI, ground=False, floor_y=None, easy=True, ground_slip=1.0, ball_type=0):
        self.cam_yaw = cam_yaw
        self.ground = ground          # True: both actors rest on a flat floor (planes[0] set, no gravity)
        self.floor_y = floor_y        # air only: the floor's height, to flag a landing
        self.easy = easy              # saveSlot difficulty Easy: the ball is not reversed under the glove
        self.slip = ground_slip
        self.hand = Actor(maxSpeed=14.0, moveSpeed=7.0, mass=10.0, accel=5.0, phys_friction=0.8)
        self.hand.radius = 13.0
        self.hand.friction = 0.6      # handInertia
        mass, accel, maxs, move, fr, rad = self.BALL_TYPES[ball_type]
        self.ball = Actor(maxSpeed=maxs, moveSpeed=7.0, mass=mass, accel=accel, phys_friction=fr)
        self.ball.radius = rad
        self.ball_type = ball_type
        self.pad = Pad()
        self.mode = JOINED
        self.prevMode = JOINED
        self.lock = 0
        self.atime = 0
        self.bounceQueued = 0
        self.joinTimer = 0
        self.inputAccel = [0.0, 0.0, 0.0]
        self.cancerTimer = 0
        self.frame = 0
        self.events = []     # notes on what happened this frame
        self.landed = False
        self.ended = None
        self.collide = True   # hand.flags & 0x10, set at the start of MaintainAction from the mode then
        self.hand.planes0 = ground
        self.ball.planes0 = ground
        self.ball.onGround = ground
        self.hand.onGround = ground

    # ---------------------------------------------------------------- helpers
    def note(self, s):
        self.events.append(s)

    def hand_holds_ball(self):
        return self.mode in (JOINED, PREWHACK, THROWAIM, THROW2CUP, BOUNCE, BOUNCE2CUP, AIRWHACK)

    # ---------------------------------------------------------------- actors (actor.c)
    def update_actor(self, a, is_hand):
        """UpdateActor: caps, the move and the friction (UpdateActorPosAndRot), gravity, the caps again."""
        a.prevVel = list(a.vel)
        sp = len2(a.vel)
        if sp > a.maxSpeed:
            k = a.maxSpeed / sp
            a.vel[0] = F(a.vel[0] * k)
            a.vel[2] = F(a.vel[2] * k)
        # UpdateActorPosAndRot
        for i in range(3):
            a.prevPos[i] = a.pos[i]
            a.pos[i] = F(a.pos[i] + a.vel[i])
            if not a.planes0:
                a.vel[i] = F(a.vel[i] * F(1.0 - (1.0 - a.phys_friction) / 4.0))
            elif i == 1:
                a.vel[i] = F(a.vel[i] * a.phys_friction)
            else:
                mul = F(1.0 - a.friction)
                a.vel[i] = F(a.vel[i] * (F(1.0 - mul) if self.slip < 1.0 else F(1.0 - mul / self.slip)))
        # CheckCollisions of the hand (only while its collision flag 0x10 is set): it touches the floor when its
        # sphere reaches it. The friction above used the surfaces of the previous game frame.
        if is_hand and self.collide and self.ground and self.floor_y is not None:
            a.planes0 = bool(a.pos[1] - a.radius < self.floor_y)
        # UpdateActorCollisionAndGravity: gravity in the air; on the model's floor the vertical speed is kept 0
        if self.ground:
            a.vel[1] = 0.0
        elif a.flags_gravity:
            g = GRAVITY / 2.0 if a.half_gravity else GRAVITY
            a.vel[1] = F(max(a.vel[1] - g, -15.0))
        sp = len2(a.vel)
        if sp > a.maxSpeed:
            k = a.maxSpeed / sp
            a.vel[0] = F(a.vel[0] * k)
            a.vel[2] = F(a.vel[2] * k)
        if a.vel[1] > 15.0:
            a.vel[1] = 15.0
        if is_hand:
            # onGround is rebuilt from the surfaces the hand touches; with collisions off (flag 0x10) it is 0
            a.onGround = bool(self.collide and self.ground)
        if not self.ground and self.floor_y is not None and a.radius is not None and a is not self.hand:
            if a.pos[1] - a.radius <= self.floor_y:
                self.landed = True
                self.note("ball reached the floor")

    # ---------------------------------------------------------------- ProcessInput (controller.c)
    def deal_with_max_speed(self, a, maxSpeed, oldSpeed):
        """DealWithMaxSpeed: over the limit, a speed that began under it is cut to it, else it is scaled by 0.9."""
        speed = len2(a.vel)
        if speed > maxSpeed:
            if oldSpeed <= maxSpeed:
                k = maxSpeed / speed
                a.vel[0] = F(a.vel[0] * k)
                a.vel[2] = F(a.vel[2] * k)
            else:
                a.vel[0] = F(a.vel[0] * 0.9)
                a.vel[2] = F(a.vel[2] * 0.9)

    def stick_accel(self, div):
        """The acceleration the stick gives the ball (JOINED, BOUNCE and BALLWALK): DIV is 120 or 180."""
        h, b, p, c = self.hand, self.ball, self.pad, self.cam_yaw
        k = h.accel / 0.5 * b.accel * 10.0 / b.mass
        x = k * (math.sin(c) * p.stickY / div - math.sin(c + PI / 2) * p.stickX / div)
        z = k * (math.cos(c) * p.stickY / div - math.cos(c + PI / 2) * p.stickX / div)
        return [F(x * 0.32), F(z * 0.32)]

    def process_input(self):
        h, b, p = self.hand, self.ball, self.pad
        m = self.mode
        if m in (JOINED, BOUNCE):
            tx, tz = self.stick_accel(120.0)
            oldSpeed = len2(b.vel)
            if oldSpeed <= 2.0:
                tx, tz = F(tx * 1.25), F(tz * 1.25)
            maxSpeed = b.moveSpeed * (1.0 if self.slip <= 1.0 else self.slip / 3.0)
            if m == JOINED and b.planes0:
                # AccelerateActorAcrossPlane over a flat floor: the same vector, capped at moveSpeed * 10
                a = math.hypot(tx, tz)
                if a != 0.0:
                    b.vel[0] = F(b.vel[0] + tx)
                    b.vel[2] = F(b.vel[2] + tz)
                    sp = len2(b.vel)
                    if sp > b.moveSpeed * 10.0:
                        k = b.moveSpeed * 10.0 / sp
                        b.vel[0] = F(b.vel[0] * k)
                        b.vel[2] = F(b.vel[2] * k)
                    self.inputAccel = [tx, 0.0, tz]
                else:
                    self.inputAccel = [0.0, 0.0, 0.0]
            else:
                b.vel[0] = F(b.vel[0] + tx)
                b.vel[2] = F(b.vel[2] + tz)
                self.inputAccel = [tx, 0.0, tz]
            if math.hypot(tx, tz) != 0.0:
                h.friction = 0.6
            if oldSpeed != 0.0:
                self.deal_with_max_speed(b, maxSpeed, oldSpeed)
        elif m == BALLWALK:
            tx, tz = self.stick_accel(180.0)
            tx, tz = -tx, -tz    # FlipVector
            oldSpeed = len2(b.vel)
            if self.easy:
                b.vel[0] = F(b.vel[0] - tx)
                b.vel[2] = F(b.vel[2] - tz)
            else:
                b.vel[0] = F(b.vel[0] + tx)
                b.vel[2] = F(b.vel[2] + tz)
            if oldSpeed != 0.0:
                self.deal_with_max_speed(b, b.maxSpeed, oldSpeed)
            if oldSpeed <= b.moveSpeed:
                ln = len2(b.vel)
                if ln > b.moveSpeed:
                    k = b.moveSpeed / ln
                    b.vel[0] = F(b.vel[0] * k)
                    b.vel[2] = F(b.vel[2] * k)
        else:
            # the last branch: the glove's own speed is limited to its moveSpeed
            self.deal_with_max_speed(h, h.moveSpeed, 10000.0)
            self.inputAccel = [0.0, 0.0, 0.0]

    # ---------------------------------------------------------------- UpdateActorPositionDependencies (actor.c)
    def move_hand_to_ball(self):
        """MoveHandToBall with scale 1 (a direction is held): the glove goes to the ball and its vertical speed is 0."""
        h, b = self.hand, self.ball
        h.vel[1] = 0.0
        h.pos = list(b.pos)

    def move_ball_to_hand(self):
        """MoveBallToHand for a hand not in a spin: the ball goes to the glove's centre."""
        self.ball.pos = list(self.hand.pos)

    def position_dependencies(self):
        h, b, p = self.hand, self.ball, self.pad
        if self.mode == BALLWALK:
            h.pos[0], h.pos[2] = b.pos[0], b.pos[2]
            lim = b.radius + h.radius + 2.0
            if h.pos[1] - b.pos[1] < lim:
                h.pos[1] = F(b.pos[1] + lim - 5.0)
                h.vel[1] = F(b.vel[1] * 1.2) if b.vel[1] > 0.0 else 0.0
        if self.mode in (JOINED, THROW2CUP, BOUNCE2CUP):
            self.move_hand_to_ball()
        if self.mode in (THROWAIM,):
            self.move_ball_to_hand()
            b.vel = list(h.vel)
        if self.mode == BOUNCE:
            h.pos[0], h.pos[2] = b.pos[0], b.pos[2]
            h.vel[0], h.vel[2] = b.vel[0], b.vel[2]
            if self.cancerTimer == 0 and p.mag < 5.0:
                b.vel[0] = F(b.vel[0] * 0.7)
                b.vel[2] = F(b.vel[2] * 0.7)
            val = b.pos[1] - 6.0 + b.radius
            if h.pos[1] < val:
                h.pos[1] = F(val)
                if h.vel[1] < 0.0:
                    h.vel[1] = 0.0

    # ---------------------------------------------------------------- MoveBall (ball.c)
    def move_ball(self):
        b, p = self.ball, self.pad
        if self.mode in (JOINED, BALLWALK) and p.mag == 0.0:
            b.friction = F(max(0.0, 1.0 - (1.0 - b.phys_friction) * 4.0))
        else:
            b.friction = b.phys_friction

    # ---------------------------------------------------------------- ApplyAction (hand.c)
    def apply(self, action):
        h, b, p = self.hand, self.ball, self.pad
        self.prevMode = self.mode
        self.mode = action
        b.moveSpeed = 13.0
        self.atime = 0
        self.note(f"ApplyAction({NAMES[action]})")
        if action == JOINED:
            if self.prevMode not in (THROW2CUP, BOUNCE2CUP):
                b.vel = list(b.prevVel)
                self.move_hand_to_ball()
                h.vel = list(b.vel)
                self.lock = 4
                self.joinTimer = 0
            h.half_gravity = False
        elif action == PREWHACK:
            pass
        elif action == THROWAIM:
            if self.prevMode != JOINED:
                h.vel[1] = 0.0
                h.vel[0] = F(h.vel[0] * 0.3)
                h.vel[2] = F(h.vel[2] * 0.3)
            if not h.onGround:
                h.flags_gravity = False
            self.bounceQueued = 0
            self.lock = 4
        elif action == BOUNCE:
            h.planes0 = False
            h.onGround = False
            if self.ground:
                b.vel[1] = F(LEVEL_HAND_SCALE * 84.0 / 0.05 / min(max(b.mass, 10.0), 15.0))
            b.moveSpeed = self.BOUNCE_MAX.get(self.ball_type, 8.0)   # BallBounceMaxSpeed
            self.ground_bounce_started = True
        elif action == BALLWALK:
            # hand.phys->mass / 15 = 10 / 15 of the glove's horizontal speed is added to the ball
            k = h.mass / (15.0 if b.mass < 10.0 else b.mass * 1.5)
            b.vel[0] = F(b.vel[0] + h.vel[0] * k)
            b.vel[2] = F(b.vel[2] + h.vel[2] * k)
            if self.prevMode != JUMP:
                self.lock = 15
            h.radius = 13.0
        elif action == RELEASE:
            pass
        elif action == JUMP:
            self.lock = 1

    # ---------------------------------------------------------------- MaintainAction (hand.c)
    def maintain(self):
        h, b, p = self.hand, self.ball, self.pad
        m = self.mode
        # the start of MaintainAction
        h.flags_gravity = not (m == THROWAIM and not h.onGround)
        self.collide = m not in (JOINED, PREWHACK, BOUNCE)
        h.half_gravity = (m == BOUNCE)
        if m != BOUNCE:
            self.bounceQueued = 0
        if m in (JOINED, PREWHACK, BOUNCE):
            h.onGround = b.onGround
        if self.joinTimer != 0:
            self.joinTimer -= 1
        if not self.hand_holds_ball() and m != 33 and p.button[B]:
            self.joinTimer += 1
        if m == JOINED:
            if p.pressed[L]:
                self.apply(BALLWALK)
            else:
                b.moveSpeed = 7.0       # BallJoinedMaxSpeed
                if self.atime >= 11:
                    if b.radius + 5.0 < h.pos[1] - b.pos[1]:
                        self.apply(BOUNCE)
                        h.vel[1] = b.vel[1]
        elif m == BOUNCE:
            if h.pos[1] - b.pos[1] > 14.0:
                h.vel[1] = F(h.vel[1] * 0.85 + b.vel[1] * 0.15)
            if self.bounceQueued and not p.button[B] and not p.button[A]:
                dist = math.dist(h.pos, b.pos)
                if dist < b.radius + 7.0 and b.vel[1] < 1.0:
                    self.note("bounce re-hit")
                    b.vel[1] = -15.0 if not self.ground else F(LEVEL_HAND_SCALE * 84.0 / 0.05 / 10.0)
                    self.bounceQueued = 0
            if p.pressed[A] or p.pressed[B]:
                self.bounceQueued = 1
        elif m == THROWAIM:
            h.vel[0] = F(h.vel[0] * 0.5)
            h.vel[1] = F(h.vel[1] * 0.5)
            h.vel[2] = F(h.vel[2] * 0.5)
            h.vel[1] = 0.0
            if p.button[Z]:
                p.latch[B] = 1
                self.apply(JOINED)
                self.note("Z cancel of the throw aim")
        elif m == PREWHACK:
            if p.button[Z]:
                p.latch[A] = 1
                self.apply(JOINED)
                self.note("Z cancel of the slap windup")
            else:
                h.pos = list(b.pos)
                h.vel = [0.0, 0.0, 0.0]
        elif m == BALLWALK:
            b.moveSpeed = h.moveSpeed
            if p.pressed[L]:
                self.apply(JOINED)
            elif not p.button[Z]:
                h.onGround = True
            else:
                # DismountBall: ApplyAction(JUMP), then RELEASE, the glove is kicked back by 10
                self.apply(JUMP)
                self.mode = RELEASE
                p.pressed[Z] = 0
                self.ended = "dismount"   # the glove's kick depends on its rotation, which is not modelled
                self.note("dismount (Z held on the ball)")

    # ---------------------------------------------------------------- ConditionsMet (hand.c), the part MBZ needs
    def conditions(self, act):
        h, b, p = self.hand, self.ball, self.pad
        if act == PREWHACK:
            return p.button[A] and p.holdTime[A] >= 3
        if act == WHACK:
            return not p.button[A]
        if act == RELEASE:
            return bool(p.pressed[Z])
        if act == THROWAIM:
            return bool(p.button[B] and p.holdTime[B] >= 5 and abs(h.pos[1] - b.pos[1]) < b.radius * 5.0)
        if act == THROW:
            return not p.button[B]
        if act == BOUNCE:
            if not p.button[B] and p.sincePress[B] < 6:
                self.bounceQueued = 1
                return True
            return False
        if act == AIRWHACK:
            return bool(p.button[A] and h.pos[1] - b.pos[1] < b.radius * 1.5)
        if act == 30:  # LOB
            return (not p.button[A]) and p.sincePress[A] < 4
        if act == JUMP:
            return bool(p.button[A] and (h.onGround or self.mode == BALLWALK))
        return False

    TABLE = {
        JOINED: (PREWHACK, RELEASE, THROWAIM, BOUNCE, 30),
        BOUNCE: (RELEASE, THROWAIM, BOUNCE2CUP, AIRWHACK),
        PREWHACK: (WHACK,),
        THROWAIM: (THROW,),
        BALLWALK: (JUMP,),
    }

    # ---------------------------------------------------------------- MoveHand (hand.c)
    def move_hand(self):
        h, b, p = self.hand, self.ball, self.pad
        # airborne with no stick: the glove's horizontal speed x 0.7 (not on the ground, no input acceleration)
        if not h.onGround and math.sqrt(sum(c * c for c in self.inputAccel)) == 0.0:
            h.vel[0] = F(h.vel[0] * 0.7)
            h.vel[2] = F(h.vel[2] * 0.7)
        self.maintain()
        if self.lock:
            self.lock -= 1
        if self.lock == 0:
            for act in range(45):
                if act in self.TABLE.get(self.mode, ()) and self.conditions(act):
                    self.apply(act)
                    break
        self.atime += 1
        h.moveSpeed = 7.0

    # ---------------------------------------------------------------- one game frame
    def step(self, down, stick=(0, 0)):
        """One game frame: DOWN is the set of held buttons (indices) read for it."""
        self.events = []
        self.frame += 1
        if down is not None:
            self.pad.read(down, stick)
        b, h = self.ball, self.hand
        temp = list(b.vel)
        self.update_actor(h, True)
        self.update_actor(b, False)
        b.prevVel = temp
        self.process_input()
        self.position_dependencies()
        self.move_ball()
        self.move_hand()
        return self


def down_set(names):
    return {BTN[n] for n in names}


# --------------------------------------------------------------------------------- the self-test
def from_row(row, cam_yaw=PI, ground=False):
    """A Game that starts in the state of an emulator snapshot (the state after the game frame before ROW's)."""
    g = Game(cam_yaw=row.get("cam", cam_yaw), ground=ground, floor_y=(row["bpos"][1] - 9.0 if ground else None))
    g.mode, g.prevMode = row["mode"], row["prev"]
    g.lock = row["lock"]
    g.atime = row["atime"]
    g.hand.pos, g.hand.vel = list(row["hpos"]), list(row["hvel"])
    g.ball.pos, g.ball.vel = list(row["bpos"]), list(row["bvel"])
    g.ball.prevVel = list(row["bprev"])
    g.hand.prevVel = list(row["hprev"])
    p = g.pad
    p.button, p.pressed, p.latch = list(row["btn"]), list(row["prs"]), list(row["latch"])
    p.holdTime, p.sincePress = list(row["hold"]), list(row["since"])
    p.stickX, p.stickY = row["stick"]
    p.mag = row["mag"]
    p.prevMag = row["mag"]
    # prevButton is not in the snapshot; a button held now was held before unless it was pressed this frame
    p.prevButton = [1 if (row["btn"][i] and not row["prs"][i]) else 0 for i in range(14)]
    g.ball.onGround = bool(row["bground"])
    g.hand.onGround = bool(row["hground"])
    g.hand.planes0 = bool(row["hpl"]) if "hpl" in row else ground
    g.ball.planes0 = bool(row["bpl"]) if "bpl" in row else ground
    g.bounceQueued = row["bounceQ"]
    g.ball.moveSpeed, g.hand.moveSpeed = row["bmove"], row["hmove"]
    g.ball.friction, g.hand.friction = row["bfric"], row["hfric"]
    g.ball.maxSpeed, g.hand.maxSpeed = row["bmax"], row["hmax"]
    g.inputAccel = [0.0, 0.0, 0.0] if p.mag == 0 else [1.0, 0.0, 1.0]
    return g


def speed(v):
    return len2(v)


def check(name, tol=0.02, show=False, start=None):
    """Run the model on a scenario of data/ from row START and compare mode and speeds with every later row.
    Row i holds the pad that game frame (f0 + i) reads (plan[i]) and the state after the game frame before it."""
    d = json.load(open(os.path.join(HERE, "data", name + ".json")))
    rows = d["rows"]
    ground = name.startswith("gnd")
    if start is None:
        start = 0 if ground else 2   # the air scenarios start with the ground bounce: skip the first two rows
    g = from_row(rows[start], ground=ground)
    plan = d["plan"]
    worst = 0.0
    modes_ok = True
    out = []
    for k in range(start, len(rows) - 1):
        g.cam_yaw = rows[k]["cam"]   # the camera's yaw as ProcessInput of this frame sees it
        g.step(None if k == start else down_set(plan[k]), (0, 0))
        r = rows[k + 1]
        if not ground and r["bground"]:
            out.append((r['fc'], 'landed', r['mode'], speed(g.ball.vel), speed(r['bvel']), 0.0))
            break
        if g.ended or g.mode in (THROW, WHACK, AIRWHACK, RELEASE, 17):
            out.append((r["fc"], NAMES[g.mode] if g.mode < 21 else g.mode, r["mode"], speed(g.ball.vel), speed(r["bvel"]), 0.0))
            break
        ms = g.mode == r["mode"]
        dv = max(abs(g.ball.vel[0] - r["bvel"][0]), abs(g.ball.vel[2] - r["bvel"][2]),
                 abs(g.hand.vel[0] - r["hvel"][0]), abs(g.hand.vel[2] - r["hvel"][2]))
        worst = max(worst, dv)
        modes_ok &= ms
        out.append((r["fc"], NAMES[g.mode] if g.mode < 21 else g.mode, r["mode"], speed(g.ball.vel), speed(r["bvel"]), dv))
        if not ms:
            break
    if show:
        for o in out:
            print("  F%d model %-9s emu mode %2d  bsp model %.3f emu %.3f  dv %.4f" % o)
    return modes_ok, worst


if __name__ == "__main__":
    if not os.path.isdir(os.path.join(HERE, "data")):
        raise SystemExit("mbz.py: the scenario logs are not in the repository (README.md).")
    names = sorted(f[:-5] for f in os.listdir(os.path.join(HERE, "data")) if f.endswith(".json"))
    if len(sys.argv) > 1:
        names = [n for n in names if n in sys.argv[1:]]
    for n in names:
        ok, worst = check(n, show=len(sys.argv) > 1)
        print(f"{n:28s} modes {'ok ' if ok else 'BAD'} max |dv| {worst:.4f}")
