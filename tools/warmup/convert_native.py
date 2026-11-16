"""Warmup animations: converter for animations authored ON THE TEKKEN 7 SKELETON itself (author's "MORE
CONVERTED" set, 2026-11-15: 207 bones, names and rest pose identical to TekkenMannequin_no_ik.blend's armature - every
bone checked, world and local). No retarget: each frame's pose (matrix_basis, valid because the rests are identical)
is copied bone for bone onto the mannequin, whose bones carry the rotation modes kiloutre's exporters expect, then the
same exports as convert.py: kiloutre's getAnimFrameFromBones (body) and, with --hands, his hand exporters with the
thumbs rewritten as in convert.py (Thumb1 = its rest against FingerBase x its pose, Thumb2 / Thumb3 = their pose,
fingers' axes; the game's hand skeleton has no rest rotations).

  blender -b TekkenMannequin_no_ik.blend -P convert_native.py -- <in.fbx> <out.bin> [--hands]

Writes <out.bin> (+ <out>_HandL.bin / _HandR.bin). Prints FRAME0 / CONVERTED like convert.py."""
import importlib.util
import os
import sys

import bpy
from mathutils import Matrix

args = sys.argv[sys.argv.index("--") + 1:]
src_path, out_path = args[0], args[1]
HANDS = "--hands" in args
PNG = args[args.index("--png") + 1] if "--png" in args else None  # check renders: <dir> --frames 1,30,...
PNG_FRAMES = [int(x) for x in args[args.index("--frames") + 1].split(",")] if "--frames" in args else []

HELPER = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "!REFERENCES",
                      "BlenderTekkenAnimationImporter-main", "BlenderTekkenAnimationImporter-main", "TekkenAnimHelper.py")
spec = importlib.util.spec_from_file_location("TekkenAnimHelper", os.path.normpath(HELPER))
tah = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tah)

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

# the skeletons must really be the same (names and rest pose), else this converter is the wrong one
missing = [b.name for b in tgt.data.bones if b.name not in src.data.bones]
worst = 0.0
for b in tgt.data.bones:
    if b.name in src.data.bones:
        d = (tgt.matrix_world @ b.matrix_local).to_translation() - (src.matrix_world @ src.data.bones[b.name].matrix_local).to_translation()
        worst = max(worst, d.length)
if missing or worst > 1e-3:
    raise SystemExit(f"NOT THE TEKKEN SKELETON: {len(missing)} bones missing ({missing[:6]}), rest offset up to {worst:.4f} m - use convert.py")

pb, spb = tgt.pose.bones, src.pose.bones
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


# Hand reach (author 2026-11-15, Julia's Final: crouched tying her shoelace, her hands float far in front of her foot).
# A line in tuning.txt (the game ignores it; this converter reads it - re-convert to apply):
#   <Name>_<Sit>_HandIK until=<frame> fade=<frames> amount=<%> up=<cm> forward=<cm> spread=<cm> [target=<bone>]
# For frames 1..until both wrists are pulled toward the target bone (default L_Foot, the ankle): up = above it,
# forward = along the foot, spread = the distance between the two hands across the foot (negative swaps sides).
# Full strength (amount %) until (until - fade), then it fades to 0 at `until`. Arm-only IK: upper arm free, elbow a
# pure hinge (Tekken 7 elbows use only their hinge value - a twist there turns forearms in game), the hand keeps
# its own rotation (fingers untouched). Frames count from the FBX's first frame (before any cutstart in the game).
# lean=auto (default) also bends the spine forward just enough for the shoulders to reach (her arms are 56 cm, the
# ankle 64-71 cm from her shoulders in the crouch); lean=<deg> fixes the bend instead (0 = arms only).
import math
from mathutils import Vector

key = os.path.splitext(os.path.basename(out_path))[0] + "_HandIK"
# the author's master copy when present, else the tuning.txt shipped in dist/warmup
TUNING = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "!PRIVATE", "warmup_tuning.txt")
if not os.path.exists(TUNING):
    TUNING = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "dist", "warmup", "tuning.txt")
ik = None
if os.path.exists(TUNING):
    for line in open(TUNING, encoding="utf-8"):
        p = line.split("#")[0].split()
        if p and p[0] == key:
            ik = {"until": 80, "fade": 20, "amount": 100.0, "up": 5.0, "forward": 0.0, "spread": 10.0, "target": "L_Foot", "lean": "auto", "hinge": 1.0}
            for kv in p[1:]:
                k, _, v = kv.partition("=")
                if k in ik:
                    ik[k] = v if k in ("target", "lean") else (int(v) if k in ("until", "fade") else float(v))
if ik:
    print("HANDIK", ik)
    empties = {}
    for side in "LR":
        e = bpy.data.objects.new(f"IK_{side}", None)
        sc.collection.objects.link(e)
        empties[side] = e
        # The Tekken skeleton's bone tails do not meet the next bone: the forearm's tail is 27 cm from the wrist, so an
        # IK aimed at the wrist (use_tail off) drove the forearm's TAIL there and left the wrist 27 cm short. The chain
        # ends at the hand's own tail instead (the hand's rotation is locked, so that point moves rigidly with the
        # forearm) and the target is shifted by the wrist -> hand-tail offset (apply_reach, repeated a few times).
        # Chain: hand (locked), forearm, upper arm, collarbone.
        c = pb[f"{side}_Hand"].constraints.new("IK")
        c.target, c.chain_count, c.use_tail, c.influence = e, 4, True, 0.0
        c.name = "REACH"
        pb[f"{side}_Hand"].lock_ik_x = pb[f"{side}_Hand"].lock_ik_y = pb[f"{side}_Hand"].lock_ik_z = True
        fa = pb[f"{side}_ForeArm"]
        fa.lock_ik_x = fa.lock_ik_y = ik["hinge"] > 0  # the elbow bends about its local Z only (convert.py HINGES)


def ik_weight(f):
    if not ik:
        return 0.0
    i = f - f0 + 1
    full_until = ik["until"] - ik["fade"]
    if i <= full_until:
        w = 1.0
    elif i >= ik["until"]:
        w = 0.0
    else:
        w = (ik["until"] - i) / max(1, ik["fade"])
        w = w * w * (3 - 2 * w)  # smoothstep
    return w * ik["amount"] / 100.0


def targets():
    W = tgt.matrix_world
    tb = ik["target"]
    ank = W @ pb[tb].head
    toe = W @ (pb[tb].children[0].head if pb[tb].children else pb[tb].tail)
    fwd = toe - ank
    fwd.z = 0
    fwd = fwd.normalized() if fwd.length > 1e-6 else Vector((0, 1, 0))
    up = Vector((0, 0, 1))
    side = fwd.cross(up).normalized()
    base = ank + up * (ik["up"] / 100) + fwd * (ik["forward"] / 100)
    return {"L": base + side * (ik["spread"] / 200), "R": base - side * (ik["spread"] / 200)}


ARM_REACH = None


def shoulders_short(tg):
    """how far the farther shoulder joint is beyond 93% of the arm's reach (m; <= 0 = both can reach)"""
    W = tgt.matrix_world
    return max(((W @ pb[f"{s}_Arm"].head) - tg[s]).length - 0.93 * ARM_REACH for s in "LR")


def bend_spine(theta, axis):
    """turn Spine1 and Spine2 by theta / 2 each about the world axis through their own joint"""
    W = tgt.matrix_world
    for b in ("Spine1", "Spine2"):
        piv = W @ pb[b].head
        R = Matrix.Translation(piv) @ Matrix.Rotation(theta / 2, 4, axis) @ Matrix.Translation(-piv)
        pb[b].matrix = tgt.matrix_world.inverted() @ (R @ (tgt.matrix_world @ pb[b].matrix))
        bpy.context.view_layer.update()


def apply_reach(w):
    global ARM_REACH
    W = tgt.matrix_world
    if ARM_REACH is None:
        ARM_REACH = ((W @ pb["L_ForeArm"].head) - (W @ pb["L_Arm"].head)).length + ((W @ pb["L_Hand"].head) - (W @ pb["L_ForeArm"].head)).length
    # lean: the smallest forward bend of the spine (about the body's side axis) that brings both shoulders within
    # reach of their targets - or a fixed angle (lean=<deg>) - scaled by the same weight as the reach
    tg = targets()
    lean = ik.get("lean", "auto")
    theta = 0.0
    if lean != "auto":
        theta = math.radians(float(lean))
    elif shoulders_short(tg) > 0:
        hip, neck = W @ pb["Spine1"].head, W @ pb["Neck"].head
        mid = (tg["L"] + tg["R"]) / 2
        axis = (neck - hip).cross(mid - hip).normalized()  # turning about this moves the chest toward the target
        saved = {b: pb[b].matrix_basis.copy() for b in ("Spine1", "Spine2")}
        lo, hi = 0.0, math.radians(35)
        for _ in range(14):  # bisection on the bend angle
            th = (lo + hi) / 2
            bend_spine(th, axis)
            short = shoulders_short(targets())
            for b, m in saved.items():
                pb[b].matrix_basis = m
            bpy.context.view_layer.update()
            lo, hi = (lo, th) if short <= 0 else (th, hi)
        theta = hi
    if theta:
        hip, neck = W @ pb["Spine1"].head, W @ pb["Neck"].head
        mid = (tg["L"] + tg["R"]) / 2
        axis = (neck - hip).cross(mid - hip).normalized()
        bend_spine(theta * w, axis)
        lean_log.append(math.degrees(theta))
    tg = targets()
    for s in "LR":
        pb[f"{s}_Hand"].constraints["REACH"].influence = w
    # the chain ends at the hand's tail: aim that point at (wrist target + the wrist -> hand-tail offset as it is now),
    # a few rounds so the offset matches the solved pose
    for _ in range(5):
        for s in "LR":
            off = (W @ pb[f"{s}_Hand"].tail) - (W @ pb[f"{s}_Hand"].head)
            empties[s].location = tg[s] + off
        bpy.context.view_layer.update()
    global solver_d
    solver_d = [((W @ pb[f"{s}_Hand"].head) - tg[s]).length for s in "LR"]  # what the solver itself reached
    # bake the solved chain into its own (local) pose, then switch the IK off again. With the target on the wrist
    # (use_tail off) the 3-bone chain is forearm, upper arm AND collarbone - the first version baked only the arm and
    # forearm, the collarbone snapped back and the hands stayed ~30 cm short.
    chain = ("Shoulder", "Arm", "ForeArm")
    solved = {}
    for s in "LR":
        for b in chain:
            solved[f"{s}_{b}"] = pb[f"{s}_{b}"].matrix.copy()
    for s in "LR":
        pb[f"{s}_Hand"].constraints["REACH"].influence = 0.0
    for s in "LR":
        for b in (f"{s}_{c}" for c in chain):
            bpy.context.view_layer.update()
            pb[b].matrix_basis = tgt.convert_space(pose_bone=pb[b], matrix=solved[b], from_space="POSE", to_space="LOCAL")
    bpy.context.view_layer.update()


def render_check(f):
    """front-side and side views of the mannequin at frame f (after any reach), for checking poses"""
    os.makedirs(PNG, exist_ok=True)
    for o in bpy.data.objects:  # only the mannequin: the imported file brings its own body in the ORIGINAL pose
        if o.type == "MESH":
            o.hide_render = o.find_armature() != tgt and o.parent != tgt
    cam = bpy.data.objects.get("CHK_CAM")
    if cam is None:
        cam = bpy.data.objects.new("CHK_CAM", bpy.data.cameras.new("CHK_CAM"))
        sc.collection.objects.link(cam)
        cam.data.type, cam.data.ortho_scale = "ORTHO", 2.2
    sc.camera = cam
    sc.render.engine = "BLENDER_WORKBENCH"
    sc.render.resolution_x, sc.render.resolution_y = 700, 700
    W = tgt.matrix_world
    c = W @ pb["BASE"].head
    c.z = 0.75
    for view, d in (("side", Vector((1, 0, 0))), ("front", Vector((0, -1, 0))), ("quarter", Vector((0.7, -0.7, 0.25)).normalized())):
        cam.location = c + d * 6
        cam.rotation_euler = (-d).to_track_quat("-Z", "Y").to_euler()
        sc.render.filepath = os.path.join(PNG, f"f{f:03d}_{view}.png")
        bpy.ops.render.render(write_still=True)


reach_log = []
lean_log = []
solver_d = [0.0, 0.0]
hinge_off = 0.0
frames, hands_l, hands_r = [], [], []
for f in range(f0, f1 + 1):
    sc.frame_set(f)
    for b in pb:
        b.matrix_basis = spb[b.name].matrix_basis.copy()  # same rest -> the same local pose
    bpy.context.view_layer.update()
    w = ik_weight(f)
    if w > 0:
        W = tgt.matrix_world
        before_d = [((W @ pb[f"{s}_Hand"].head) - (W @ pb[ik["target"]].head)).length for s in "LR"]
        apply_reach(w)
        after_d = [((W @ pb[f"{s}_Hand"].head) - (W @ pb[ik["target"]].head)).length for s in "LR"]
        for s in "LR":
            e = pb[f"{s}_ForeArm"].matrix_basis.to_euler("XYZ")
            hinge_off = max(hinge_off, abs(e.x), abs(e.y))
        if (f - f0) % 10 == 0:
            reach_log.append(f"f{f} w{w:.2f} L {before_d[0]*100:.0f}->{after_d[0]*100:.0f} cm R {before_d[1]*100:.0f}->{after_d[1]*100:.0f} cm (solver {solver_d[0]*100:.0f}/{solver_d[1]*100:.0f} from target)")
    frames.append(tah.getAnimFrameFromBones(tgt, False))
    if PNG and f in PNG_FRAMES:
        render_check(f)
    if HANDS:
        for rows, fn, side in ((hands_l, tah.getLeftHandAnimFrameFromBones, "L"), (hands_r, tah.getRightHandAnimFrameFromBones, "R")):
            fr = list(fn(tgt))
            fr[3:12] = thumb_values(side)  # values 3-11 = Thumb1-3 (after FingerBase)
            rows.append(fr)


def write(rows, path, kind):
    a = tah.TekkenAnimation() if kind == "body" else tah.TekkenAnimation(type="hand")
    a.setLength(len(rows))
    a.recalculateSize()
    for i, fr in enumerate(rows):
        for fid, v in enumerate(fr):
            a.setField(float(v), i, fid)
    with open(path, "wb") as fh:
        fh.write(bytes(a.data))
    return len(a.data)


size = write(frames, out_path, "body")
for side, rows in (("L", hands_l), ("R", hands_r)):
    if rows:
        hp = out_path[:-4] + f"_Hand{side}.bin"
        write(rows, hp, "hand")
        print(f"HAND{side} {len(rows)} frames -> {hp}")
if reach_log:
    print("HANDIK " + " | ".join(reach_log))
    # elbows must stay pure hinges (local Z only): the largest turn about local X / Y on the forearms while reaching
    print("HANDIK forearm off-hinge max %.2f deg" % math.degrees(hinge_off))
    if lean_log:
        print("HANDIK spine lean (full strength) %.1f - %.1f deg" % (min(lean_log), max(lean_log)))
print("FRAME0 offset %.1f %.1f %.1f pos %.1f %.1f %.1f mesh %.3f %.3f %.3f" % tuple(frames[0][:12][i] for i in (0, 1, 2, 3, 4, 5, 9, 10, 11)))
print(f"CONVERTED {len(frames)} frames ({f0}-{f1}) native -> {out_path} ({size} bytes)")
