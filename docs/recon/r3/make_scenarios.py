"""Writes R3's scenarios (docs/recon/r3_plan.md): the three of C26 in the catch's phase, and a catch forward, back and to a side.
   python -I make_scenarios.py <repo root>"""
import json, os, sys

root = sys.argv[1]
R3 = 'fall.catch=1,fall.arm_curve=1,fall.head=1,fall.palms=1'
BODY = {
    "mesh": "/Game/MetaHumans/SandboxNPC/Body/SKM_SandboxNPC_BodyMesh.SKM_SandboxNPC_BodyMesh",
    "physics_asset": "/Game/Generated/Physics/PA_SandboxNPC.PA_SandboxNPC",
    "attached_meshes": ["/Game/MetaHumans/SandboxNPC/Face/SKM_SandboxNPC_FaceMesh.SKM_SandboxNPC_FaceMesh"],
    "location_cm": [0, 0, 0],
    "control": "active",
}
R = 0.707107


def tune():
    return {"t": 0.0, "type": "console", "command": "Sandbox.Tune " + R3}


def blow(t, ns, d, body="spine_04", duration=None):
    e = {"t": t, "type": "impulse", "body": body, "impulse_ns": ns, "direction": d}
    if duration:
        e["duration_s"] = duration
    return e


def fall_expect(extra=None):
    e = {
        "tuning_changes": 1,
        "fell": True,
        "max_rest_after_fall_s": 3.0,
        "max_energy_gain_j": 40.0,
        "max_body_penetration_cm": 2.0,
        "max_self_penetration_cm": 2.0,
        "max_joint_stretch_cm": 2.0,
        "measures": {"control.fall.r3.runs": {"min": 1}},
    }
    if extra:
        e.update(extra)
    return e


scenarios = {}
JUMP = {"control.pose.target_step_max_deg": {"max": 110.0}}
# A catch forward and to a side: a blow of 200 N*s on the chest (the chest's 80 and 120 do not fell the body); a hand first, ahead of
# the chest along the fall (C14), the head kept off the floor (C15), no target jumping past the suite's 106 degrees. Forward the hands
# hold the trunk off the floor and the head comes down to it at about 1 m/s: asked as C16's 2.0 m/s (the head touches there; a fall
# whose head did not touch at all would read null and fail this check, and the check is then to be changed, not the body).
# The written fall's poses alone put a hand first in every fall by a blow (C14 100% at r3base): an arm that caught is asked for as well.
CAUGHT = {"control.fall.r3.arms_caught": {"min": 1}}
scenarios["fall_catch_forward"] = dict(duration_s=10.0, events=[tune(), blow(3.0, 200, [0, 1, 0])],
    expect=fall_expect({"measures": dict(JUMP, **CAUGHT, **{"control.fall.r3.runs": {"min": 1}, "control.fall.hands_ahead_first": {"equals": True},
                                                 "control.fall.head_impact_speed_mps": {"max": 2.0}})}))
scenarios["fall_catch_side"] = dict(duration_s=10.0, events=[tune(), blow(3.0, 200, [-1, 0, 0])],
    expect=fall_expect({"measures": dict(JUMP, **CAUGHT, **{"control.fall.r3.runs": {"min": 1}, "control.fall.hands_ahead_first": {"equals": True},
                                                 "control.fall.head_struck": {"equals": False}})}))
# Backward the catch is not made (fall.catch_back 0): the head and the arms' curve alone; the head kept off the floor.
scenarios["fall_back_r3"] = dict(duration_s=10.0, events=[tune(), blow(3.0, 200, [0, -1, 0])],
    expect=fall_expect({"measures": dict(JUMP, **{"control.fall.r3.runs": {"min": 1}, "control.fall.head_struck": {"equals": False}})}))
# C26: a hit, a push and a hold while the arms catch (a fall forward and to the left): each must reach the body while an arm catches and
# before a hand or the trunk has met the floor (the measures count only then, and only from the Falling state's start).
scenarios["fall_phase_catch_hit"] = dict(duration_s=10.0, events=[tune(), blow(3.0, 200, [R, R, 0]), blow(3.6, 40, [-1, 0, 0])],
    expect=fall_expect({"hit_events": 2, "measures": dict(JUMP, **CAUGHT, **{"control.fall.r3.runs": {"min": 1}, "control.fall.r3.hits_while_catching": {"min": 1}})}))
# The push from the fall's side: across its way (forward and to the left), not from its front.
scenarios["fall_phase_catch_push"] = dict(duration_s=10.0, events=[tune(), blow(3.0, 200, [R, R, 0]), blow(3.55, 30, [R, -R, 0], duration=0.25)],
    expect=fall_expect({"measures": dict(JUMP, **CAUGHT, **{"control.fall.r3.runs": {"min": 1}, "control.fall.r3.hits_while_catching": {"min": 1}})}))
hold = {
    "t": 3.40, "type": "effector", "effector": "hand_right", "at_bone": "lowerarm_l", "follow_bone_s": 0.6,
    "path": [
        {"t": 0.0, "location_cm": [90, 50, 120], "grip": 0},
        {"t": 0.08, "location_cm": [0, 0, 0], "grip": 0},
        {"t": 0.10, "location_cm": [0, 0, 0], "grip": 1},
        {"t": 1.4, "location_cm": [0, 0, 0], "grip": 1},
        {"t": 1.5, "location_cm": [0, 0, 0], "grip": 0},
        {"t": 2.0, "location_cm": [0, 0, 80], "grip": 0},
    ],
}
scenarios["fall_phase_catch_hold"] = dict(duration_s=10.0, events=[tune(), blow(3.0, 200, [R, R, 0]), hold],
    expect=fall_expect({"effector_grabs": 1, "grab_events": 1, "min_grab_frames": 100,
                        "measures": dict(JUMP, **CAUGHT, **{"control.fall.r3.runs": {"min": 1}, "control.fall.r3.held_while_catching": {"equals": True}})}))

for name, s in scenarios.items():
    doc = {"name": name, "duration_s": s["duration_s"], "body": BODY, "events": s["events"], "expect": s["expect"]}
    with open(os.path.join(root, "Scenarios", name + ".json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(doc, f, indent=2)
        f.write("\n")
    print("wrote", name)
