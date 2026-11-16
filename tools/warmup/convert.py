"""Warmup animations: retarget one of the author's FBX warmup animations onto the Tekken 7 mannequin and write a Tekken 7 body animation (0xC8, 23 vectors per frame) with
kiloutre's exporter math (BlenderTekkenAnimationImporter TekkenAnimHelper.getAnimFrameFromBones).

  blender -b <TekkenMannequin_no_ik.blend> -P convert.py -- <in.fbx> <out.bin> [--png <folder> --frames 1,40,80]

Retarget: both rigs face -Y in Blender world. Each mapped target bone first gets the swing that turns its rest
direction onto the source bone's rest direction (the author's rig rests in an A-pose, the mannequin in a T-pose);
then every frame its world rotation = source pose world @ source rest world^-1 @ aligned target rest. The body
position follows the source pelvis. The format has no usable Spine2 value (the exporter writes 0), so the chest
orientation (source Spine2) is given to Spine1 and the mannequin's Spine2 stays at rest.
"""
import importlib.util
import math
import os
import sys

import bpy
from mathutils import Matrix, Vector

args = sys.argv[sys.argv.index("--") + 1:]
src_path, out_path = args[0], args[1]
png_dir = args[args.index("--png") + 1] if "--png" in args else None
png_frames = [int(x) for x in args[args.index("--frames") + 1].split(",")] if "--frames" in args else []

HELPER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "!REFERENCES",
                      "BlenderTekkenAnimationImporter-main", "BlenderTekkenAnimationImporter-main", "TekkenAnimHelper.py")
spec = importlib.util.spec_from_file_location("TekkenAnimHelper", os.path.normpath(HELPER))
tah = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tah)

# target bone -> (source bone for rotation, (source dir from, to), (target dir from, to)) ; dir = rest direction
# target bone, source bone, (source primary from, to), (target primary from, to), optional secondary pairs
# (source from, to), (target from, to) - else the secondary is the body's sideways axis (X), or forward (-Y) for
# bones that point sideways (shoulders, arms)
MAP = [
    ("BASE", "Rot", ("Rot", "Neck"), ("BASE", "Neck"), None),
    ("Hip", "Hip", ("Rot", "Neck"), ("BASE", "Neck"), None),
    ("Spine1", "Spine2", ("Spine2", "Neck"), ("Spine2", "Neck"), None),   # chest orientation (see header)
    ("Neck", "Neck", ("Neck", "Head"), ("Neck", "Head"), None),
    ("Head", "Head", ("Head", "ATH_Head_Top"), ("Head", "ATH_Head_Top"), None),
    ("L_Shoulder", "L_Shoulder", ("L_Shoulder", "L_UpperArm"), ("L_Shoulder", "L_Arm"), None),
    ("L_Arm", "L_UpperArm", ("L_UpperArm", "L_LowerArm"), ("L_Arm", "L_ForeArm"), None),
    ("L_ForeArm", "L_LowerArm", ("L_LowerArm", "L_Hand"), ("L_ForeArm", "L_Hand"), None),
    ("L_Hand", "L_Hand", ("L_Hand", "L_Middle1"), ("L_Hand", "L_Middle1"), (("L_Index1", "L_Pinky1"), ("L_Index1", "L_Pinky1"))),
    ("R_Shoulder", "R_Shoulder", ("R_Shoulder", "R_UpperArm"), ("R_Shoulder", "R_Arm"), None),
    ("R_Arm", "R_UpperArm", ("R_UpperArm", "R_LowerArm"), ("R_Arm", "R_ForeArm"), None),
    ("R_ForeArm", "R_LowerArm", ("R_LowerArm", "R_Hand"), ("R_ForeArm", "R_Hand"), None),
    ("R_Hand", "R_Hand", ("R_Hand", "R_Middle1"), ("R_Hand", "R_Middle1"), (("R_Index1", "R_Pinky1"), ("R_Index1", "R_Pinky1"))),
    ("L_UpLeg", "L_UpperLeg", ("L_UpperLeg", "L_LowerLeg"), ("L_UpLeg", "L_Leg"), None),
    ("L_Leg", "L_LowerLeg", ("L_LowerLeg", "L_Foot"), ("L_Leg", "L_Foot"), None),
    ("L_Foot", "L_Foot", ("L_Foot", "L_Toe"), ("L_Foot", "L_Toe"), None),
    ("R_UpLeg", "R_UpperLeg", ("R_UpperLeg", "R_LowerLeg"), ("R_UpLeg", "R_Leg"), None),
    ("R_Leg", "R_LowerLeg", ("R_LowerLeg", "R_Foot"), ("R_Leg", "R_Foot"), None),
    ("R_Foot", "R_Foot", ("R_Foot", "R_Toe"), ("R_Foot", "R_Toe"), None),
]
# Hands (2026-11-15, --hands): fingers onto the mannequin's finger bones, then kiloutre's hand exporters (19
# rotations each) -> <out>_HandL.bin / <out>_HandR.bin. Author rig: <Finger>0 (metacarpal) + 3 joints, Thumb0-2, all
# parented to the hand. Mannequin: palm chain FingerBase -> Middle -> Ring -> Pinky (index metacarpal = FingerBase),
# the digits 1-3 on those, Thumb1-3 on FingerBase. Twist hint for every finger bone: the palm width Index1 -> Pinky1.
HANDS = "--hands" in args


def fingers(s):
    sec = ((f"{s}_Index1", f"{s}_Pinky1"), (f"{s}_Index1", f"{s}_Pinky1"))
    out = [(f"{s}_FingerBase", f"{s}_Index0", (f"{s}_Index0", f"{s}_Index1"), (f"{s}_FingerBase", f"{s}_Index1"), sec)]
    for f in ("Middle", "Ring", "Pinky"):
        out.append((f"{s}_{f}", f"{s}_{f}0", (f"{s}_{f}0", f"{s}_{f}1"), (f"{s}_{f}", f"{s}_{f}1"), sec))
    out += [(f"{s}_Thumb1", f"{s}_Thumb0", (f"{s}_Thumb0", f"{s}_Thumb1"), (f"{s}_Thumb1", f"{s}_Thumb2"), sec),
            (f"{s}_Thumb2", f"{s}_Thumb1", (f"{s}_Thumb1", f"{s}_Thumb2"), (f"{s}_Thumb2", f"{s}_Thumb3"), sec),
            (f"{s}_Thumb3", f"{s}_Thumb2", (f"{s}_Thumb2", f"ADR_{s}_Thumb2Inflat_mus"), (f"{s}_Thumb3", f"{s}_Thumb4__null"), sec)]
    for f in ("Index", "Middle", "Ring", "Pinky"):
        out += [(f"{s}_{f}1", f"{s}_{f}1", (f"{s}_{f}1", f"{s}_{f}2"), (f"{s}_{f}1", f"{s}_{f}2"), sec),
                (f"{s}_{f}2", f"{s}_{f}2", (f"{s}_{f}2", f"{s}_{f}3"), (f"{s}_{f}2", f"{s}_{f}3"), sec),
                (f"{s}_{f}3", f"{s}_{f}3", (f"{s}_{f}3", f"ADR_{s}_{f}3Inflat_mus"), (f"{s}_{f}3", f"{s}_{f}4__null"), sec)]
    return out


ALL = MAP + (fingers("L") + fingers("R") if HANDS else [])
PELVIS_SRC, PELVIS_TGT = "Rot", "BASE"   # body position: source body root (115 cm, = BASE's 1.15 m)
# stick figure of the source for comparison renders (parent -> child heads)
STICK = [("Rot", "Spine1"), ("Spine1", "Spine2"), ("Spine2", "Neck"), ("Neck", "Head"), ("Head", "ATH_Head_Top"),
         ("Spine2", "L_Shoulder"), ("L_Shoulder", "L_UpperArm"), ("L_UpperArm", "L_LowerArm"), ("L_LowerArm", "L_Hand"),
         ("L_Hand", "L_Middle1"), ("Spine2", "R_Shoulder"), ("R_Shoulder", "R_UpperArm"), ("R_UpperArm", "R_LowerArm"),
         ("R_LowerArm", "R_Hand"), ("R_Hand", "R_Middle1"), ("Rot", "L_UpperLeg"), ("L_UpperLeg", "L_LowerLeg"),
         ("L_LowerLeg", "L_Foot"), ("L_Foot", "L_Toe"), ("Rot", "R_UpperLeg"), ("R_UpperLeg", "R_LowerLeg"),
         ("R_LowerLeg", "R_Foot"), ("R_Foot", "R_Toe")]
SHIFT = Vector((1.3, 0, 0))  # the stick figure stands 1.3 m to the mannequin's left (screen right in front view)

tgt = bpy.data.objects["Armature"]
for ob in list(bpy.data.objects):
    if ob.name.endswith(".001"):
        bpy.data.objects.remove(ob, do_unlink=True)  # the file's second copy of the mannequin
before = set(bpy.data.objects)
bpy.ops.import_scene.fbx(filepath=src_path)
src = next(o for o in bpy.data.objects if o not in before and o.type == "ARMATURE")
act = src.animation_data.action
f0, f1 = int(act.frame_range[0]), int(act.frame_range[1])
sc = bpy.context.scene


def rest_world(arm, b):
    return arm.matrix_world @ arm.data.bones[b].matrix_local


def head_world(arm, b, pose=True):
    if pose:
        return arm.matrix_world @ arm.pose.bones[b].head
    return arm.matrix_world @ arm.data.bones[b].head_local


def frame(arm, prim, sec, pose, flat=False):
    """orthonormal frame from a primary direction and a secondary hint (Gram-Schmidt)"""
    a = head_world(arm, prim[1], pose) - head_world(arm, prim[0], pose)
    if flat:  # feet: along the ground (both rigs rest flat-footed; their ankle -> toe slopes differ, 33 vs 28 deg)
        a.z = 0
    a = a.normalized()
    if isinstance(sec, Vector):
        h = sec
    else:
        h = head_world(arm, sec[1], pose) - head_world(arm, sec[0], pose)
    if abs(a.dot(h.normalized())) > 0.95:  # hint along the bone: use forward instead
        h = Vector((0, -1, 0)) if abs(a.x) > 0.7 else Vector((1, 0, 0))
    b = (h - a * a.dot(h)).normalized()
    c = a.cross(b)
    return Matrix((a, b, c)).transposed()  # columns a, b, c


# rest alignment: the world rotation turning the target bone's rest frame onto the source bone's rest frame
align = {}
for t, s, sd, td, sec in ALL:
    if sec:
        ss, ts = sec
    else:
        dir_s = head_world(src, sd[1], False) - head_world(src, sd[0], False)
        ss = ts = Vector((0, -1, 0)) if abs(dir_s.normalized().x) > 0.7 else Vector((1, 0, 0))
    flat = t in ("L_Foot", "R_Foot")  # author 2026-11-15: toes pointed up compared with the idle stance
    fs, ft = frame(src, sd, ss, False, flat), frame(tgt, td, ts, False, flat)
    align[t] = (fs @ ft.inverted()).to_4x4()

# scale: source pelvis height vs mannequin pelvis height (rest)
k = head_world(tgt, PELVIS_TGT, False).z / max(1e-6, head_world(src, PELVIS_SRC, False).z)
# Tekken 7 body animations carry a 90 degree yaw on the body (game animations: mesh rotation field 10 = -pi/2; in-game
# tests 2026-11-14: no yaw faced the camera, -90 faced away from the opponent, so +90 here - the exporter flips it) (every game animation
# read 2026-11-14; without it the first in-game test faced the camera). Rest frames are taken first, then the whole
# source turns, so every bone and the body position turn with it.
REST_SRC = {s: rest_world(src, s).to_3x3().normalized() for _, s, *_ in ALL}
YAW = math.radians(float(args[args.index("--yaw") + 1])) if "--yaw" in args else math.pi / 2
# applied in the maths (turning the source object did nothing: its action keys the object transform every frame)
YAWM = Matrix.Rotation(YAW, 3, "Z")
tgt_inv = tgt.matrix_world.inverted()
pb = tgt.pose.bones
# Thumbs (author 2026-11-15: thumbs drawn to the index knuckle in game, a jump at the hand-over). The game's hand
# skeleton (bone matrices read in game: player motion +0x848, 0xA0 a bone, 19 a hand in kiloutre's order) has NO
# rest rotation on any hand bone - zero values = every bone along its parent, the thumb lying beside the index. The
# mannequin's Thumb1 has one (63.5, -30.8, -20.3 deg against FingerBase = game (-1.11, -0.54, 0.35), the game's own
# idle thumb is (-1.06, -0.59, 0.38)), and kiloutre's exporter writes only the turn from the mannequin's rest. So the
# thumbs are written here instead: Thumb1 = its rest-relative-to-FingerBase times its pose, Thumb2 / Thumb3 = their
# pose (no rest rotation), all with the fingers' axes (XYZ; left -x, y, -z, right x, y, z).
THUMB_SIGNS = {"L": (-1, 1, -1), "R": (1, 1, 1)}
thumb_prev = {}


def thumb_values(side):
    out = []
    for i, b in enumerate(("Thumb1", "Thumb2", "Thumb3")):
        bone = tgt.data.bones[f"{side}_{b}"]
        m = pb[f"{side}_{b}"].matrix_basis.to_3x3()
        if i == 0:
            m = (bone.parent.matrix_local.to_3x3().inverted() @ bone.matrix_local.to_3x3()) @ m
        prev = thumb_prev.get((side, b))
        e = m.to_euler("XYZ", prev) if prev else m.to_euler("XYZ")
        thumb_prev[(side, b)] = e
        out += [s * v for s, v in zip(THUMB_SIGNS[side], e)]
    return out
for b in pb:
    b.matrix_basis = Matrix.Identity(4)
bpy.context.view_layer.update()
frames, err = [], {}
hands_l, hands_r = [], []
errp = {}  # direction error only (no palm / twist term)


# Elbows and knees are pure hinges in Tekken 7 (game animations 2026-11-15: of their 3 values only the 3rd ever moves -
# elbows 0..-2.5, knees 0..2.6; a twist written there is dropped, which turned forearms and wrists in game). So the
# lower bone gets a bend about its hinge axis only (mannequin: forearm local Z, shin local X), the upper bone takes
# the twist that puts the hinge plane through the source's lower segment, and the hand / foot gets its full rotation
# on top. upper -> (lower, end, hinge axis, bend range in Blender euler units, source lower, source end)
HINGES = {
    "L_Arm": ("L_ForeArm", "L_Hand", "Z", (-2.75, 0.05), "L_LowerArm", "L_Hand"),
    "R_Arm": ("R_ForeArm", "R_Hand", "Z", (-0.05, 2.75), "R_LowerArm", "R_Hand"),
    "L_UpLeg": ("L_Leg", "L_Foot", "X", (-0.05, 2.75), "L_LowerLeg", "L_Foot"),
    "R_UpLeg": ("R_Leg", "R_Foot", "X", (-0.05, 2.75), "R_LowerLeg", "R_Foot"),
}
LOWERS = {v[0] for v in HINGES.values()}


def hinge_solve(upper, rot_upper, d):
    """twist psi of the upper bone about its own direction and bend theta of the lower bone, so the lower segment
    points along world direction d. Returns (upper world rotation, theta)."""
    lower, end, axis, (lo, hi), _, _ = HINGES[upper]
    ur = rest_world(tgt, upper).to_3x3().normalized()
    lr = rest_world(tgt, lower).to_3x3().normalized()
    M = ur.inverted() @ lr
    v = (lr.inverted() @ (head_world(tgt, end, False) - head_world(tgt, lower, False))).normalized()
    e = Vector({"X": (1, 0, 0), "Y": (0, 1, 0), "Z": (0, 0, 1)}[axis])
    ev = e.cross(v)
    a = (rot_upper @ (ur.inverted() @ (head_world(tgt, lower, False) - head_world(tgt, upper, False)))).normalized()
    d = d.normalized()

    def solve(psi):
        R = Matrix.Rotation(psi, 3, a) @ rot_upper
        W = R @ M
        wv, wev = W @ v, W @ ev
        th = min(max(math.atan2(d.dot(wev), d.dot(wv)), lo), hi)
        got = (wv * math.cos(th) + wev * math.sin(th)).normalized()
        return got.angle(d, 0) + 0.002 * abs(psi), psi, th, R

    best = min((solve(math.radians(x)) for x in range(-180, 180)), key=lambda r: r[0])
    for _ in range(2):  # refine around the best
        step = math.radians(0.1 if _ else 1.0)
        best = min((solve(best[1] + step * i) for i in range(-10, 11)), key=lambda r: r[0])
    return best[3], best[2]


def set_world_rot(bone, rot3):
    cur = tgt.matrix_world @ pb[bone].matrix
    m = Matrix.LocRotScale(cur.to_translation(), rot3.to_quaternion(), Vector((1, 1, 1)))
    pb[bone].matrix = tgt_inv @ m
    bpy.context.view_layer.update()


def render_compare(f):
    """front and side view: the mannequin (retargeted) and a stick figure of the source, side by side"""
    name = "SRC_STICK"
    if name in bpy.data.objects:
        bpy.data.objects.remove(bpy.data.objects[name], do_unlink=True)
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions, cu.bevel_depth = "3D", 0.02
    for a, b in STICK:
        if a not in src.pose.bones or b not in src.pose.bones:
            continue
        sp = cu.splines.new("POLY")
        sp.points.add(1)
        for i, bn in enumerate((a, b)):
            p = YAWM @ (head_world(src, bn) * k) + SHIFT
            sp.points[i].co = (p.x, p.y, p.z, 1)
    ob = bpy.data.objects.new(name, cu)
    sc.collection.objects.link(ob)
    cam = bpy.data.objects.get("CMP_CAM")
    if cam is None:
        cam = bpy.data.objects.new("CMP_CAM", bpy.data.cameras.new("CMP_CAM"))
        sc.collection.objects.link(cam)
        cam.data.type, cam.data.ortho_scale = "ORTHO", 3.4
    sc.camera = cam
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x, sc.render.resolution_y = 900, 760
    mid = SHIFT * 0.5 + Vector((0, 0, 1.0))
    for view, loc, rot in (("front", mid + Vector((0, -6, 0)), (math.pi / 2, 0, 0)),
                           ("side", mid + Vector((6, 0, 0)), (math.pi / 2, 0, math.pi / 2))):
        cam.location, cam.rotation_euler = loc, rot
        sc.render.filepath = os.path.join(png_dir, f"{view}_{f:03d}.png")
        bpy.ops.render.render(write_still=True)


if png_dir:
    os.makedirs(png_dir, exist_ok=True)
for f in range(f0, f1 + 1):
    sc.frame_set(f)
    # body position
    want = YAWM @ (head_world(src, PELVIS_SRC) * k)
    rest_head = head_world(tgt, PELVIS_TGT, False)
    pb["BODY_SCALE__group"].matrix_basis = Matrix.Identity(4)
    bpy.context.view_layer.update()
    delta_world = want - rest_head
    delta_world.z = want.z - rest_head.z
    base_m = tgt.matrix_world @ pb["BODY_SCALE__group"].matrix
    pb["BODY_SCALE__group"].matrix = tgt_inv @ (Matrix.Translation(delta_world) @ base_m)
    bpy.context.view_layer.update()
    for t, s, sd, td, sec in ALL:
        if t in LOWERS:
            continue  # set with its upper bone (hinge)
        r = YAWM @ (src.matrix_world @ src.pose.bones[s].matrix).to_3x3().normalized()
        rs = REST_SRC[s]
        rt = (align[t].to_3x3() @ rest_world(tgt, t).to_3x3()).normalized()
        want = r @ rs.inverted() @ rt
        if t in HINGES:
            lower, _, axis, _, s_lower, s_end = HINGES[t]
            d = YAWM @ (head_world(src, s_end) - head_world(src, s_lower))
            want, theta = hinge_solve(t, want, d)
            set_world_rot(t, want)
            pb[lower].matrix_basis = Matrix.Rotation(theta, 4, axis)
            bpy.context.view_layer.update()
            continue
        set_world_rot(t, want)
    # error check: angle between each mapped bone's direction (and the hands' palm axis) on both rigs
    for t, s, sd, td, sec in ALL:
        vs = YAWM @ (head_world(src, sd[1]) - head_world(src, sd[0]))
        vt = head_world(tgt, td[1]) - head_world(tgt, td[0])
        e = math.degrees(vs.angle(vt, 0))
        errp[t] = max(errp.get(t, 0), e)
        if sec:
            ps = YAWM @ (head_world(src, sec[0][1]) - head_world(src, sec[0][0]))
            pt = head_world(tgt, sec[1][1]) - head_world(tgt, sec[1][0])
            e = max(e, math.degrees(ps.angle(pt, 0)))
        err[t] = max(err.get(t, 0), e)
    frames.append(tah.getAnimFrameFromBones(tgt, False))
    if HANDS:
        for rows, fn, side in ((hands_l, tah.getLeftHandAnimFrameFromBones, "L"), (hands_r, tah.getRightHandAnimFrameFromBones, "R")):
            fr = list(fn(tgt))
            fr[3:12] = thumb_values(side)  # values 3-11 = Thumb1-3 (after FingerBase)
            rows.append(fr)
    if png_dir and f in png_frames:
        render_compare(f)

anim = tah.TekkenAnimation()
anim.setLength(len(frames))
anim.recalculateSize()
for i, fr in enumerate(frames):
    for fid, v in enumerate(fr):
        anim.setField(float(v), i, fid)
with open(out_path, "wb") as fh:
    fh.write(bytes(anim.data))
for side, rows in (("L", hands_l), ("R", hands_r)):
    if not rows:
        continue
    ha = tah.TekkenAnimation(type="hand")
    ha.setLength(len(rows))
    ha.recalculateSize()
    for i, fr in enumerate(rows):
        for fid, v in enumerate(fr):
            ha.setField(float(v), i, fid)
    hp = out_path[:-4] + f"_Hand{side}.bin"
    with open(hp, "wb") as fh:
        fh.write(bytes(ha.data))
    print(f"HAND{side} {len(rows)} frames -> {hp}")
print("MAXERR " + ", ".join(f"{t} {e:.1f}" for t, e in err.items()))
if HANDS:
    print("DIRERR " + ", ".join(f"{t} {e:.1f}" for t, e in errp.items() if "_" in t and t.split("_")[1][:3] in ("Fin", "Thu", "Ind", "Mid", "Rin", "Pin")))
print("FRAME0 offset %.1f %.1f %.1f pos %.1f %.1f %.1f mesh %.3f %.3f %.3f" % tuple(frames[0][:12][i] for i in (0, 1, 2, 3, 4, 5, 9, 10, 11)))
print(f"CONVERTED {len(frames)} frames ({f0}-{f1}), scale {k:.4f} -> {out_path} ({len(anim.data)} bytes)")
