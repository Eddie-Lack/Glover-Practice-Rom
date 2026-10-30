"""Standalone model of the pad, the glove's and the ball's walking, and the camera (horizontal plane only).

It follows one game frame of MainGameLoop (gloveworld/src/main.c 1038 to 1075) in this order:
  1. UpdateActor(hand)        actor.c UpdateActor, UpdateActorPosAndRot (move, then friction; rot toward rotAim)
     UpdateActor(ball)
  2. UpdateActor(camera)      same function: speed cut to 20, move, vel * 0.26667
  3. ProcessInput             controller.c 363 to 715: push from the pad, the speed caps
  4. UpdateActorPositionDependencies  actor.c 1148 to 1370: the target (cameraInfo.actor) leads by 0.4 * focus
                              velocity, CalcCameraRot, the side offset, the target follows the focus
  5. MoveBall, MoveHand       (not modelled; the action is an input here)
  6. MoveCam                  camera.c 106 to 342: yaw from camera and target, distance control, "behind" swing
  7. PlayerSpinCamera         camera.c 415 to 526: C-left and C-right push the camera sideways

The check against emulator logs needs logs that the repository does not hold (README.md).
Angles: yaw a points along (sin a, cos a). C-right moves the camera to the right of its view and so raises the yaw.
"""
import math

PI = math.pi
TWOPI = 2 * math.pi
HALFPI = 1.570796327


# The game's atan (maths.c 98 to 116) is a table of 125 steps per unit: atantab[(int)(x * 125) + 2] / 40000. The table
# was read from the emulator (out/atantab.json); this formula gives the same 15626 entries.
_ATAN = [int(math.atan(i / 125.0) * 40000 + 0.0005) for i in range(125 * 125 + 1)]


def g_atan(x):
    if x > 125:
        x = 125
    return _ATAN[int(x * 125)] / 40000.0


def g_atan2(y, x):
    """atan2 (maths.c 118 to 146), with the table's coarse steps."""
    if x == 0.0 and y == 0.0:
        return 0.0
    if x > 0.0:
        if y > 0.0:
            return g_atan(y / x)
        return -g_atan(-y / x)
    if x < 0.0:
        if y > 0.0:
            return g_atan(-x / y) + 1.570796327
        if y < 0.0:
            return -1.570796327 - g_atan(x / y)
        return 3.1415927
    if y > 0.0:
        return 1.5707964
    return -1.5707964


def aabs(a):
    a = a - TWOPI if a > TWOPI else a
    return a + TWOPI if a < 0 else a


def shortest(a, b):
    """FindShortestAngle (maths.c 569): the unsigned angle between two yaws."""
    t = a - b
    if abs(t) > PI:
        r = b - a if a > b else t
        return r + TWOPI
    return abs(t)


def shortest_signed(a1, a2):
    """FindShortestAngleSigned (maths.c 585)."""
    t = a1 - a2
    if -PI < t <= PI:
        return t
    if t > PI:
        return t - TWOPI
    return a1 + TWOPI - a2


def fwd(a, m=1.0):
    """RotateVector2D of (0, m) by a: (sin a, cos a) * m."""
    return (math.sin(a) * m, math.cos(a) * m)


def length(v):
    return math.hypot(v[0], v[1])


# ---------------------------------------------------------------------------------------------------------------
# The pad (controller.c ReadController 130 to 195)

def pad_from(dpad=(), stick=(0, 0)):
    """The stick pair the game uses, from raw stick values (-80 to 80) and a set of D-pad names."""
    def axis(raw):
        s = max(-79, min(79, raw))
        if abs(s) > 5:
            return s - 5 if s > 0 else s + 5
        return 0
    sx, sy = axis(stick[0]), axis(stick[1])
    if sx == 0 and sy == 0:
        if "UP" in dpad:
            sy = 79
        elif "DOWN" in dpad:
            sy = -79
        if "LEFT" in dpad:
            sx = -79
        elif "RIGHT" in dpad:
            sx = 79
    return sx, sy


class P:
    """Constants (measured in the emulator in HUB1 and Atlantis 1, or read from the data)."""
    hand_friction = 0.6          # handInertia, hand.friction while walking; terrain 0 has slip 1
    hand_accel, hand_mass, hand_move, hand_turn = 5.0, 10.0, 7.0, 0.6
    cam_friction = 0.26666668    # camera.friction
    cam_maxspeed = 20.0          # camera.phys->maxSpeed (MoveCam sets 2 * 10)
    distance = 120.0             # cameraInfo.distance: levelCamDistance 100 * cameraZoomVal[1] 1.2
    ring = 10.0                  # levelHandScale * 10 / 0.05
    zoomval = 1.2                # cameraZoomVal[cameraZoom]
    behind_limit, behind_speed, behind_angle = 0.6, 1.0, 0.3
    spin = 16.0                  # PlayerSpinCamera: 4.0 * 4.0 per frame the C button is held
    lead = 0.4                   # the target leads the focus by 0.4 * focus velocity
    follow_max = 20.0


def new_state(**kw):
    s = dict(
        hpos=[0.0, 0.0], hvel=[0.0, 0.0], hrot=PI, haim=PI, hspeedmode="walk",
        bpos=[0.0, 0.0], bvel=[0.0, 0.0],
        cpos=[0.0, 135.0], cvel=[0.0, 0.0], crot=PI,
        tpos=[0.0, 0.0], follow=20.0, side=0.0, sideaim=0.0, sidevel=0.0,
        focus="hand", ang=0.0,
    )
    s.update(kw)
    return s


def rotaim_step(rot, aim, turn, div):
    """ActorHandleRotaim (actor.c 1000 to 1045) for rotAim[1] == 1 (div 2)."""
    aim = aabs(aim)
    d = shortest(rot, aim)
    if d <= 0.01:
        return aabs(aim)
    step = min(turn, d / div)
    if rot < aim:
        rot = rot - step if aim - rot > PI else rot + step
    elif aim < rot:
        rot = rot + step if rot - aim > PI else rot - step
    return aabs(rot)


def step_hand_walk(S, sx, sy):
    """ProcessInput for HAND_WALK on flat ground with slip 1 (controller.c 501 to 620)."""
    mag = math.hypot(sx, sy)
    if mag > 0:
        S["ang"] = aabs(g_atan2(sx, sy))
    if mag >= 5.0:
        S["haim"] = aabs(-S["ang"] + S["crot"])
    tz = P.hand_accel * 10.0 / P.hand_mass * mag / 180.0
    a = aabs(S["crot"] - S["ang"])
    old = length(S["hvel"])
    if tz != 0.0:
        d = fwd(aabs(-S["ang"] + S["crot"]), tz)
        S["hvel"][0] += d[0]
        S["hvel"][1] += d[1]
    sp = length(S["hvel"])
    if old != 0.0 and sp > P.hand_move:
        k = P.hand_move / sp if old <= P.hand_move else 0.9
        S["hvel"][0] *= k
        S["hvel"][1] *= k
    return tz


def update_actors(S, ball_active=False):
    """Steps 1 and 2: UpdateActor for the glove, the ball and the camera (horizontal parts)."""
    S["hpos"][0] += S["hvel"][0]
    S["hpos"][1] += S["hvel"][1]
    S["hvel"][0] *= P.hand_friction
    S["hvel"][1] *= P.hand_friction
    S["hrot"] = rotaim_step(S["hrot"], S["haim"], P.hand_turn, 2.0)
    v = S["cvel"]
    sp = length(v)
    if sp > P.cam_maxspeed:
        v[0] *= P.cam_maxspeed / sp
        v[1] *= P.cam_maxspeed / sp
    S["cpos"][0] += v[0]
    S["cpos"][1] += v[1]
    v[0] *= P.cam_friction
    v[1] *= P.cam_friction


def calc_rot(S):
    return aabs(g_atan2(S["cpos"][0] - S["tpos"][0], S["cpos"][1] - S["tpos"][1]) + 3.141592654)


def position_dependencies(S, focus_pos, focus_vel):
    """Step 4 (actor.c 1285 to 1370), for the target on flat ground."""
    S["tpos"][0] += focus_vel[0] * P.lead
    S["tpos"][1] += focus_vel[1] * P.lead
    S["crot"] = calc_rot(S)
    # side offset
    if S["hspeedmode"] == "ballwalk":
        val = shortest_signed(S["crot"], S["hrot"])
    else:
        val = shortest_signed(S["crot"], aabs(S["hrot"] + PI))
    tf = abs(PI - abs(val))
    if tf < 0.4 or tf > PI - 0.4:
        S["sideaim"] = 0.0
    elif 0.5 < tf < PI - 0.5:
        lim = min(P.distance / 10.0, 40.0)
        S["sideaim"] = lim if val > 0 else max(-40.0, -P.distance / 10.0)
    v = (S["sideaim"] - S["side"]) / 10.0
    v = min(v, 0.5) if v > 0 else max(-0.5, v)
    S["sidevel"] = (S["sidevel"] + v) * 0.6
    S["side"] += S["sidevel"]
    # RotateVector2D((side, 0), crot) = side * (cos a, -sin a)
    off = (S["side"] * math.cos(S["crot"]), -S["side"] * math.sin(S["crot"]))
    t2 = [focus_pos[0] - S["tpos"][0] + off[0], focus_pos[1] - S["tpos"][1] + off[1]]
    if S.get("ftimer", 0) != 0:
        S["ftimer"] -= 1
    elif S["follow"] < P.follow_max:
        S["follow"] += min((P.follow_max - S["follow"]) / 8.0, 1.0)
    d = length(t2)
    if d > S["follow"]:
        S["tpos"][0] += t2[0] * S["follow"] / d
        S["tpos"][1] += t2[1] * S["follow"] / d
    else:
        S["tpos"][0] += t2[0]
        S["tpos"][1] += t2[1]


def move_cam(S, hand_vel, hand_rot):
    """Step 6 (camera.c MoveCam), without overrides, collisions and slope: the yaw, the distance, the swing."""
    S["crot"] = calc_rot(S)
    v = (S["cpos"][0] - S["tpos"][0], S["cpos"][1] - S["tpos"][1])
    val2 = length(v)
    maxs = 10.0
    ring = P.distance + P.ring
    cv = S["cvel"]

    def accel(speed):
        cv[0] += speed * math.sin(S["crot"])
        cv[1] += speed * math.cos(S["crot"])
        sp = length(cv)
        if sp > P.cam_maxspeed:
            cv[0] *= P.cam_maxspeed / sp
            cv[1] *= P.cam_maxspeed / sp
    if val2 > ring:
        accel(min((val2 - ring) / 2.0, maxs))
    if val2 < P.distance - P.ring:
        accel(-min((P.distance - P.ring - val2) / 2.0, maxs))
    hs = length(hand_vel)
    if hs > P.behind_limit:
        angle = shortest_signed(S["crot"], aabs(hand_rot))
        if abs(angle) > P.behind_angle and abs(angle) < PI - P.behind_angle:
            scale = 0.0
            if abs(angle) < HALFPI:
                scale = abs(angle / HALFPI)
            if abs(angle) > HALFPI:
                scale = (PI - abs(angle)) / HALFPI
            scale = abs(scale) * P.zoomval
            w = P.behind_speed * hs * scale
            side = aabs(S["crot"] + HALFPI) if angle > 0 else aabs(S["crot"] - HALFPI)
            d = fwd(side, w)
            cv[0] += d[0]
            cv[1] += d[1]


def spin_cam(S, left, right):
    """Step 7 (camera.c PlayerSpinCamera, no planes near the camera)."""
    cv = S["cvel"]
    if right:
        d = fwd(S["crot"] - HALFPI, P.spin)
    elif left:
        d = fwd(S["crot"] + HALFPI, P.spin)
    else:
        return
    cv[0] += d[0]
    cv[1] += d[1]


def frame_glove(S, sx, sy, cl=False, cr=False):
    """One game frame with the glove walking (no ball involved)."""
    update_actors(S)
    step_hand_walk(S, sx, sy)
    position_dependencies(S, S["hpos"], S["hvel"])
    move_cam(S, S["hvel"], S["hrot"])
    spin_cam(S, cl, cr)
    return S



# ---------------------------------------------------------------------------------------------------------------
# The ball: holding it (J, HAND_JOINED) and standing on it (B, HAND_BALLWALK). The rules are those of
# lmash.py (checked there against the emulator); the camera and the glove's turning are added.

class Ball:
    mass, accel, max_speed, friction, min_speed, joined_max = 10.0, 0.5, 26.0, 0.94, 0.1, 7.0
    k = 5.0 / 0.5 * 0.5 * 10.0 / 10.0 * 0.32          # hand accel / 0.5 * ball accel * 10 / ball mass * 0.32 = 1.6
    stand_add = 10.0 / 15.0                            # hand.c 1015: hand mass / 15 for a ball of mass 10
    idle_friction = max(0.0, 1.0 - (1.0 - 0.94) * 4.0)


def deal_with_max_speed(vel, max_speed, old_speed):
    """controller.c DealWithMaxSpeed (735 to 750)."""
    speed = length(vel)
    if speed > max_speed:
        k = max_speed / speed if old_speed <= max_speed else 0.9
        vel[0] *= k
        vel[1] *= k


def ball_state(mode="J", **kw):
    s = new_state(hspeedmode="ballwalk" if mode == "B" else "walk", mode=mode, bmove=7.0, bfric=0.94, hplane=True,
                  hcoll=False, prev=[0.0, 0.0], focus="ball", ftimer=0, easy=True, air=False, hflag=(mode == "J"))
    s.update(kw)
    return s


def frame_ball(S, sx, sy, press=False, cl=False, cr=False):
    """One game frame with the glove holding the ball (J) or standing on it (B) on flat ground."""
    mode0 = S["mode"]
    prev = list(S["bvel"])                       # main.c 1038 and 1044: ball.prevVel = ball.vel at the frame's start
    # --- UpdateActor(hand): speed cut to 14, move, friction (0.6 with a plane under it, else 0.95), turn
    h = length(S["hvel"])
    if h > 14.0:
        S["hvel"][0] *= 14.0 / h
        S["hvel"][1] *= 14.0 / h
    S["hpos"][0] += S["hvel"][0]
    S["hpos"][1] += S["hvel"][1]
    f = P.hand_friction if S["hplane"] else 0.95
    S["hvel"][0] *= f
    S["hvel"][1] *= f
    if S["air"]:
        S["hplane"] = False
    elif S["hcoll"]:
        S["hplane"] = (mode0 == "J")
    if not S["hflag"]:      # with flag 0x4000 UpdateActorPosAndRot returns before the turn (actor.c 727)
        S["hrot"] = rotaim_step(S["hrot"], S["haim"], P.hand_turn, 2.0)
    # --- UpdateActor(ball): cut to maxSpeed, move, friction
    sp = length(S["bvel"])
    if sp > Ball.max_speed:
        S["bvel"][0] *= Ball.max_speed / sp
        S["bvel"][1] *= Ball.max_speed / sp
    moved = length(S["bvel"])
    S["bpos"][0] += S["bvel"][0]
    S["bpos"][1] += S["bvel"][1]
    ff = (1.0 - (1.0 - Ball.friction) / 4.0) if S["air"] else S["bfric"]
    S["bvel"][0] *= ff
    S["bvel"][1] *= ff
    if moved < Ball.min_speed and not S["air"]:
        S["bvel"] = [0.0, 0.0]
    # --- UpdateActor(camera)
    cam_update(S)
    # --- ProcessInput
    mag = math.hypot(sx, sy)
    if mag > 0:
        S["ang"] = aabs(g_atan2(sx, sy))
    if mag >= 5.0:
        S["haim"] = aabs(-S["ang"] + S["crot"])
    c = S["crot"]
    div = 120.0 if mode0 == "J" else 180.0
    ax = Ball.k * (math.sin(c) * sy - math.sin(c + PI / 2) * sx) / div
    az = Ball.k * (math.cos(c) * sy - math.cos(c + PI / 2) * sx) / div
    old = length(S["bvel"])
    bv = S["bvel"]
    if mode0 == "J":
        if old <= 2.0:
            ax, az = ax * 1.25, az * 1.25
        bv[0] += ax
        bv[1] += az
        if old != 0.0:
            deal_with_max_speed(bv, S["bmove"], old)
    else:
        sign = 1.0 if S["easy"] else -1.0
        bv[0] += sign * ax
        bv[1] += sign * az
        if old != 0.0:
            deal_with_max_speed(bv, Ball.max_speed, old)
        if old <= S["bmove"]:
            ln = length(bv)
            if ln > S["bmove"]:
                bv[0] *= S["bmove"] / ln
                bv[1] *= S["bmove"] / ln
    # --- UpdateActorPositionDependencies: the camera target (focus is the ball in both modes)
    position_dependencies(S, S["bpos"], S["bvel"])
    # --- MoveBall
    S["bfric"] = Ball.friction if mag != 0 else Ball.idle_friction
    S["hcoll"] = (mode0 != "J")
    # --- MoveHand, MaintainAction
    if mode0 == "J":
        if press:
            S["bvel"][0] += S["hvel"][0] * Ball.stand_add
            S["bvel"][1] += S["hvel"][1] * Ball.stand_add
            S["mode"] = "B"
            S["hspeedmode"] = "ballwalk"
            S["bmove"] = 13.0
        else:
            S["bmove"] = Ball.joined_max
    else:
        if press:
            S["bvel"] = list(prev)
            S["hvel"] = list(prev)
            S["mode"] = "J"
            S["hspeedmode"] = "walk"
            S["bmove"] = 13.0
            S["follow"] = 0.0           # hand.c 785: cameraInfo.followSpeed = 0, followTimer = 1
            S["ftimer"] = 1
        else:
            S["bmove"] = P.hand_move
            if length(S["bvel"]) > 0.1:
                S["haim"] = aabs(g_atan2(S["bvel"][0], S["bvel"][1]) + 3.141592654)
    # --- MoveHand, after MaintainAction (hand.c 3031 to 3036): in J the glove turns here and flag 0x4000 is set;
    # in any other action the flag is cleared and the turn happens in the next UpdateActor
    if S["mode"] == "J":
        S["hflag"] = True
        S["hrot"] = rotaim_step(S["hrot"], S["haim"], P.hand_turn, 2.0)
    else:
        S["hflag"] = False
    # --- MoveCam, PlayerSpinCamera
    move_cam(S, S["hvel"], S["hrot"])
    spin_cam(S, cl, cr)
    return S


def cam_update(S):
    """UpdateActor(camera): speed cut to 20, move, friction."""
    v = S["cvel"]
    sp = length(v)
    if sp > P.cam_maxspeed:
        v[0] *= P.cam_maxspeed / sp
        v[1] *= P.cam_maxspeed / sp
    S["cpos"][0] += v[0]
    S["cpos"][1] += v[1]
    v[0] *= P.cam_friction
    v[1] *= P.cam_friction


if __name__ == "__main__":
    print("dpad.py is a library: the checks against emulator logs need logs that the repository does not hold (README.md).")
