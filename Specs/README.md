# Specs

Text descriptions that `Tools\gen-assets.ps1` turns into binary assets under `Content/Generated/` (not
committed). Edit the spec, regenerate, run the tests: `DictatorSandbox.Generated.*` checks that every
generated asset matches its spec.

## physics/*.json: physics assets

```json
{
  "name": "npc_body",
  "output": "/Game/Generated/Physics/PA_SandboxNPC",
  "skeletal_mesh": "/Game/MetaHumans/SandboxNPC/Body/SKM_SandboxNPC_BodyMesh.SKM_SandboxNPC_BodyMesh",
  "template": "/Game/MetaHumans/SandboxNPC/Body/PHYS_SandboxNPC.PHYS_SandboxNPC",
  "total_mass_kg": 80.0,
  "bodies": [
    {"bone": "pelvis", "mass_fraction": 0.1117, "shapes_from": ["pelvis"]},
    {"bone": "spine_04", "mass_fraction": 0.1436, "shapes_from": ["spine_04"]},
    {"bone": "foot_l", "mass_fraction": 0.0137, "shapes": [
      {"type": "box", "center_cm": [14.9, 7.0, 3.25], "size_cm": [9.5, 26.0, 6.5], "yaw_deg": -8.5}]}
  ],
  "joints": [
    {"child": "spine_04", "along": "neck_01", "swing1_axis": [1, 0, 0],
     "swing1": [-22.5, 12.5], "swing2": [-12.5, 12.5], "twist": [-15, 15], "limit": {"stiffness": 50, "damping": 5}},
    {"child": "calf_l", "mirror": "calf_r", "along": "foot_l", "swing1_axis": [-1, 0, 0],
     "swing1": [-2, 150], "swing2": "locked", "twist": "locked", "limit": {"stiffness": 50, "damping": 5}},
    {"child": "lowerarm_l", "mirror": "lowerarm_r", "along": "hand_l", "swing1_axis": "bend",
     "swing1": [0, 140], "swing2": "locked", "twist": "locked", "limit": {"stiffness": 50, "damping": 5}}
  ],
  "collision": {"disable": [["thigh_l", "thigh_r"], ["pelvis", "spine_04"]]}
}
```

An excerpt of `npc_body.json`, its values as they are (some lines wrapped): three of its 19 bodies, three of the
eleven entries of its joints (18 joints with the mirrored ones) and two of the nine pairs it keeps from colliding.

| Field | Meaning |
| --- | --- |
| `name` | The spec's name, by custom its file's without `.json`; the generator's log and the tests name the asset by it (the test of which way the NPC's joints open runs on `npc_body` only) |
| `output` | Package path of the generated asset |
| `skeletal_mesh` | The mesh the asset is made for: its bones and reference pose, in which shapes and joint frames are placed. A bone the spec names that the mesh lacks is an error |
| `template` | Existing physics asset that supplies collision shapes, and the projection settings of the joints |
| `total_mass_kg` | Body mass; each body gets `mass_fraction` of it. Fractions must sum to 1 |
| `bodies[].bone` | Bone the body is attached to. Bones without a body follow their parent rigidly |
| `bodies[].shapes_from` | Template bodies whose shapes this body takes over. Listing several merges them (`npc_body_neck.json` makes one body of the neck and the head: `["neck_01", "head"]`) |
| `bodies[].inertia_scale` | Optional factor (1 to 20) on the inertia the engine works out from the body's mass and shapes. See "Inertia of the hands" |
| `bodies[].ccd` | Optional, `true` or `false` (the default): continuous collision detection for this body. Off for every body of `npc_body.json`, by measurement: see "Limits and continuous collision detection" |
| `bodies[].shapes` | Shapes written out in the spec, in the mesh's reference-pose space, where the floor is z = 0. `"type": "box"`: `center_cm`, `size_cm` along the box's own axes and `yaw_deg` (rotation about the vertical). `"type": "sphere"`: `center_cm`, `radius_cm`. `"type": "capsule"`: `radius_cm` and the two ends of its line, `from_cm` and `to_cm` (a head's cheekbones). Fields a type does not take are not read. A body needs `shapes_from`, `shapes` or both. The generator's log prints every body's shapes in this space, the template's among them: the way to see what a template has |
| `joints[].child` | Child body. Its parent is the nearest ancestor bone that has a body. One joint per non-root body, written out or mirrored |
| `joints[].mirror` | Child body of the same joint on the other side. It is generated as the exact mirror image, with the same ranges, so left and right cannot differ |
| `joints[].along` | Bone the limb runs toward from the joint, or a direction `[x, y, z]` for a limb with no bone beyond it (the head). The twist axis points from the child bone to this bone, or along the direction |
| `joints[].swing1_axis` | The axis the joint's main bending turns about: a direction `[x, y, z]` in the mesh's reference-pose space, `{"from": bone, "to": bone}`, or `"bend"` (see below). It is made perpendicular to the twist axis |
| `joints[].swing1`, `swing2`, `twist` | `[min, max]` in degrees, `"locked"` or `"free"`. A range need not be symmetric |
| `joints[].limit` | What the joint does at the end of its range: `"hard"`, or `{"stiffness": n, "damping": n}` for a soft limit (the engine's figures of a soft limit, the same for the two swings and the twist). Written for every joint; a mirrored joint has its twin's |
| `collision.disable` | `[boneA, boneB]` pairs that do not collide with each other. Jointed bodies never collide; every other pair of bodies does, unless it is listed here. See "Self-collision" |

### The physics specs

| Spec | Asset | What it is |
| --- | --- | --- |
| `npc_body.json` | `PA_SandboxNPC` | The default body since the owner's decision of 26 November 2026: 19 bodies and 18 joints, the head a body of its own on the neck (`neck_01` and `head`, below). The run-time asset allows 144 pairs of its bodies to collide |
| `npc_body_neck.json` | `PA_SandboxNPCNeck` | The body before it: 18 bodies, head and neck one body on one joint (127 pairs) |
| `npc_body_toes.json` | `PA_SandboxNPCToes` | 20 bodies: the one-piece neck's 18 and a toe body on each foot (`ball_l`, `ball_r`; their muscles are the `toes` group). Put off; yes or no is decided before R4 |

Every tool runs on another of these assets without a file changed: `SANDBOX_PHYSICS_ASSET=PA_SandboxNPCNeck` puts
that asset in the place of `PA_SandboxNPC` in a copy of each scenario of the queue (`Tools\common.ps1`), and for an
asset whose head is part of the neck (`PA_SandboxNPCNeck`, `PA_SandboxNPCToes`)
`SANDBOX_HEAD_BODY=neck_01` makes what a scenario does to `head` act on that bone.

The wrist limited in the hand's own axes (`npc_body_wrist.json`, `PA_SandboxNPCWrist`, and the joint field `limits_in`
that only it used) was a no-go of the natural-body track and was removed on 26 November 2026 (R2's step 0: decision 9,
and the owner's choice of that day to take `limits_in` with it); a spec that still names `limits_in` is refused.

### Joints

A joint is described by its anatomy, and the generator builds the engine's joint frames from that and the
mesh's reference pose:

- **twist** turns about the limb's own length (the direction of `along`);
- **swing1** turns about `swing1_axis`;
- **swing2** turns about the third axis, `swing1_axis` x twist axis.

A positive angle turns the child body about its axis the way `FQuat(Axis, Angle)` does. In the mesh's space
the character faces +y and its left is +x, so for a leg hanging down with `"swing1_axis": [1, 0, 0]` positive
swing1 swings it forward (hip flexion), positive swing2 swings a left leg toward the other one (adduction)
and positive twist turns a left foot outward.

Angles are measured from the reference pose, which must lie inside every range: a body spawned outside its
limits is thrown back by the first physics step, so the generator refuses such a spec. The exception is
`"swing1_axis": "bend"`, for a limb that is already bent in the reference pose (an elbow in an A-pose): the
axis is the normal of the plane of the two segments, swing1 is measured from the straight limb and positive
bends it further, so an elbow is simply `[0, 140]`.

The engine's limits are symmetric around the parent-side frame. An asymmetric range is expressed by turning
that frame to the middle of the range: a knee of -2 to 150 degrees becomes plus or minus 76 around a frame
bent by 74. `metrics.json` therefore shows the knee of a standing character at 74 degrees with a limit of 76
(`spawn_swing1_deg`, `swing1_deg`).

A mirrored joint is the reflection of the written one in the body's midplane. Reflecting a rotation axis also
reverses it, so the three axes of the frame together make half a turn about x, and the same ranges then mean
mirror-image motion. The test `DictatorSandbox.Generated.PhysicsAsset.JointsFollowAnatomy` checks that, and
that the NPC's knee bends backward, its elbow folds, its hip flexes forward.

### Self-collision

Every pair of bodies collides, except the jointed pairs and the pairs under `collision.disable`. That list
is for bodies whose shapes overlap in the reference pose: left on, they push each other apart from the
first step and the character cannot hold the pose it is spawned in. `npc_body.json` lists nine: the two
thighs (1.4 cm into each other), the pelvis and the chest across the lower spine (5.5 cm), the lower
spine with each thigh (0.9 cm), the neck with each collarbone (6.0 cm), the two collarbones where they meet
at the breastbone, and the chest with each upper arm (the shoulder's capsule reaches into the chest's; they
were a jointed pair, and so never collided, until the collarbones came between them). So the legs can
cross at the thighs; shanks and feet do collide with the other leg, and forearms and hands with the chest.

Which pairs overlap is measured, not guessed: a scenario run reports the deepest overlap of colliding
bodies in the spawn pose (`penetration.spawn_self_cm` in `metrics.json`), and a run with active control
fails the check `spawn_pose_clear_of_itself` above 0.1 cm. The generator's log also lists colliding pairs
whose bounding boxes overlap, which overstates capsules: the lower spine and the upper arms are reported
there (2.3 cm) and do not touch.

The template's own collision table is not carried over. It is not the same on the left and on the right
(an upper arm collides with its hand on one side only), and left and right must behave alike.

Until M3 the generated asset had no self-collision at all: the engine call that creates a body
(`FPhysicsAssetUtils::CreateNewBody`) switches collision off between the new body and every earlier one
unless told otherwise, and the generator only ever took pairs away. An arm went through the chest. The test
`DictatorSandbox.Generated.PhysicsAsset.MatchesSpec` now counts the pairs that can collide.

What comes from the template and what from the spec: shapes (unless written out) and the joints'
projection settings are the template's; the set of bodies, masses, joint axes, joint limits and what a
joint does at its limit, and
collision pairs are the spec's. Three things differ from the stock MetaHuman asset on purpose, because its
values break a fully simulated body:

- **Joint positions and axes.** Both anchors of a joint sit on the child bone, so they coincide in the
  reference pose. The stock asset places them for another body's proportions: on SandboxNPC the anchors of
  all 15 joints were 1.5 to 3.6 cm apart, and the solver closed the gaps in the first step, shortening the
  skeleton by 12 cm. Its axes are not mirror images on the two sides either (the left elbow read -40 degrees
  of twist in the reference pose, the right one +33).
- **No motors.** The stock joints carry orientation drives that pull each joint toward the zero of its
  axes, which is not the reference pose: they flex elbows and knees. The generated asset has none; muscles
  are added at run time by `USandboxBodyController`.
- **Feet are boxes with a flat sole on the floor** (written out in `npc_body.json`). The template's foot
  shapes, a tilted box and a toe capsule that sits 1.5 cm below the floor, make the foot rock, and through a
  stiff ankle the whole body leans.

The generator log (`Saved\Tools\gen-assets.log`) lists colliding body pairs whose bounding boxes overlap in
the reference pose. Bounding boxes overstate capsules, so a small overlap can be a false alarm.

The ranges in `npc_body.json` are the starting limits of `docs/PRD.md`, with these choices where the table
or the engine leaves room:

- **Shoulder: two links since M3.** The engine was believed to limit the two swings of a joint together, as an elliptical
  cone around one direction, so one joint cannot give 180 degrees of flexion and 180 of abduction. The
  shoulder joint's cone is 105 degrees wide around a direction forward and to the side: the arm hangs at
  the side and reaches straight up, both with 15 degrees to spare, but reaches back by only about 20 degrees
  instead of 50. Each collarbone is a body of its own (0.64 kg, taken from the chest's mass) on a
  sternoclavicular joint: 40 degrees of elevation and 10 of depression (swing1), 20 of protraction and 25
  of retraction (swing2), plus or minus 30 of rotation about its length. The shoulder girdle adds its range
  to the arm's, as a person's does, and the arm can rise in front and to the side. Its muscle is in the
  `spine` group, not `arms` (`FSandboxBodyTuning::GroupOfBone`): with the arms' soft muscles each collarbone sagged
  10 degrees under its arm.
- **Spine.** The table's totals are split evenly over the two spine joints.
- **Neck.** Two joints since 26 November 2026, the head a body of its own: `neck_01` (swing1 -40 to 50) and `head`
  (-20 to 25), together the range the one joint had (-60 to 75; the swing2 of 35 and 10 make the old 45, the twists of
  35 and 45 the old 80). The one-piece neck, one joint for the whole range and head and neck one body, is
  `npc_body_neck.json`.
- **Forearm rotation** is on the wrist's twist (plus or minus 70), because the forearm is one body.
- **Ankle.** Sideways tilt (plus or minus 15) and turning (plus or minus 10) are not in the table.
- **Knee and elbow** are hinges with nothing else free.
- **Knee: -2 to 150, not 0 to 150.** With the limit at 0 a standing knee sits exactly on it, the limit works
  against the leg muscles in every solver step and the feet rock almost twice as fast (0.13 rad/s against
  0.08). Two degrees of slack remove that; five and ten change nothing further.

### Limits and continuous collision detection

Measured in M3 part 5 (`docs/testing.md`), not assumed:

- **The engine limits each swing by itself.** Its joint solver takes the turn of a joint apart into a twist
  and a swing and holds the twist and the swing's two angles each within its own range: a box. (The shoulder
  paragraph above was written when it was believed to be an ellipse; with a box one joint could have given
  the arm its range. The collarbones stay: they are anatomy, and the poses are written for them.)
- **Every joint has the soft limit of the stock asset, stiffness 50 and damping 5,** now written in the spec
  instead of copied from the template in silence. It is not soft in effect: a joint stays within 1 to 2.5
  degrees of its range in a knockdown and within 6 at the ankle in a hard step. Hard limits hold no better and
  bounce: a fall gained 46 J against 26 and took 4.0 s to rest against 3.6.
- **No body has continuous collision detection.** In 32 knockdowns nothing went deeper than 1.5 cm into the
  floor, under the 2 cm at which the owner's rule switches it on. A passive body dropped from 1.5 m does go
  8.3 cm in, and gains 149 J on the way out (the body of 18, M3 part 5; 130 J on the body of 19, baseline `led`); sweeping
  the feet, or the feet and the head, changes neither
  figure. It is the solver not converging in the engine's 8 iterations (3.2 cm with 32, 1.5 cm with 64, which
  is what a body under control has).

### Inertia of the hands

The hands have `"inertia_scale": 3`, a stability measure and not anatomy. Their inertia from shape and mass
(5.5, 15.2 and 12.6 kg*cm2) already matches de Leva's figures for a hand (about 5.9, 14.4 and 9.6), and so do
the forearm and the upper arm. But a fist at 9 m/s on the forearm near the wrist spins the 1.3 kg forearm,
and the 0.49 kg hand behind it snaps round its wrist at 30 to 47 rad/s for one substep before its muscle
catches it. How fast depended on the exact point struck: 2 cm higher or lower switched it on or off, and
the scenario `strike_punch_forearm` of M2 happened to strike where it was off. Three times the inertia keeps
the snap under 24 rad/s at every height from 102.6 to 110.6 cm (`strike_punch_forearm_heights`), and leaves
standing, falls and the push matrix as they were. The engine's own remedy, inertia conditioning of the solver
(`p.Chaos.Solver.InertiaConditioning.Distance`), was tried and not taken: it acts on every body, and a fallen
body then took 3.3 to 6.7 s to come to rest instead of 2.6. The solver's other joint settings changed nothing.

## rooms/*.json: room maps

```json
{
  "name": "test_room",
  "output": "/Game/Generated/Maps/TestRoom",
  "size_cm": [500, 400, 270],
  "wall_thickness_cm": 20,
  "lightmap_resolution": 128,
  "lights": [
    {"location_cm": [-120, -80, 240], "intensity": 1800, "color": [1.0, 0.93, 0.82],
     "attenuation_radius_cm": 900, "source_radius_cm": 12, "mobility": "static"}
  ]
}
```

A box room centred on the origin with the floor's top face at z = 0: floor, ceiling and four walls built
from the engine cube, point lights, one reflection capture and a player start. `mobility` is `static`
(fully baked) or `stationary` (baked bounce light, dynamic shadows on movable objects). `test_room.json` is the
example above with a second light, a stationary one: the test room of M1, whose lighting is baked, where
`room_stand_npc` and a session without `-Name` stand.

The generator saves the map unlit. `Tools\bake-lighting.ps1` then builds lightmaps and reflection captures
with CPU Lightmass. A map is regenerated only when its spec file is newer than the map, because regenerating
throws the baked lighting away; `Tools\gen-assets.ps1 -Force` regenerates regardless.

`furniture` is optional: pieces of static boxes, each with a `name`, the point of the floor it stands on
(`location_cm`), a turn about the vertical (`yaw_deg`) and its `boxes` (`center_cm` from that point, `size_cm`).
`getup_room.json` has a table (a top on four legs, 73 cm high) and a chair (seat, back, four legs) for the stand of
falls and get-ups (`Tools\getup-stand.ps1`); it is not baked, its lights are stationary. They stand in the middle
of the room as a table and a chair do: the table at the origin, the chair drawn up to its +y long side at (0, 50),
its back away from the table and the front of its seat 6 cm under the table top. The stand reads where the pieces
named `table` and `chair` stand from this file and places its bodies around them, so the pair can be moved; it
takes the table's -y side and -x end and the chair's +x side and back to be open floor.

```json
"furniture": [
  {"name": "table", "location_cm": [0, 0, 0], "yaw_deg": 0,
   "boxes": [{"center_cm": [0, 0, 73], "size_cm": [120, 70, 4]}, {"center_cm": [-55, -30, 35.5], "size_cm": [5, 5, 71]}]}
]
```

A piece may have `mass_kg`: it is then a thing with mass and no part of the map. A scenario (or a session) that loads the map puts it into the world as a prop of the piece's name (`FScenarioSpec::AddRoomProps`), unless the scenario lists a prop of that name itself; the
pieces' places are read from the spec in the project's folder, so a change needs no new map, only one for a piece that becomes static or movable.
`movable_room.json` is `getup_room.json` with a table of 30 kg and a chair of 5 kg (`Tools\getup-stand.ps1 -FurnitureRoom movable_room`).

Limitations: box shape only, engine grid material, no windows or doors; furniture without `mass_kg` is static boxes.

## pose_prior.json: the clips of the pose database

Not an asset. The list of recorded clips `Tools\pose-prior.ps1 -Build` makes the database of recorded poses from
(`FSandboxPosePrior`, `metrics.pose_prior` in `Scenarios/README.md`): standing, stepping, crouching, sitting, being
pushed and felled, lying and getting up, a part of Epic's sample chosen by the owner on 25 November 2026. Each set
names a folder (`in_project`: below `Content/Characters`; `from_sample`: below the sample's `Animations`, copied by
`Tools\fetch-sample-anims.ps1 -PosePrior`), a regular expression on a clip's file name, and the number of clips it
must give: another number is an error, since figures from two databases are not to be compared. `exclude` leaves out
montages, additives and the like. The database is not committed.
