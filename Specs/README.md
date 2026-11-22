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
    {"bone": "spine_04", "mass_fraction": 0.1596, "shapes_from": ["spine_04", "clavicle_l", "clavicle_r"]},
    {"bone": "foot_l", "mass_fraction": 0.0137, "shapes": [
      {"type": "box", "center_cm": [14.9, 7.0, 3.25], "size_cm": [9.5, 26.0, 6.5], "yaw_deg": -8.5}]}
  ],
  "joints": [
    {"child": "spine_04", "swing1": 15, "swing2": 10, "twist": 10}
  ],
  "collision": {"disable": [], "enable": []}
}
```

| Field | Meaning |
| --- | --- |
| `output` | Package path of the generated asset |
| `template` | Existing physics asset that supplies collision shapes and joint frames |
| `total_mass_kg` | Body mass; each body gets `mass_fraction` of it. Fractions must sum to 1 |
| `bodies[].bone` | Bone the body is attached to. Bones without a body follow their parent rigidly |
| `bodies[].shapes_from` | Template bodies whose shapes this body takes over. Listing several merges them |
| `bodies[].shapes` | Boxes written out in the spec: `center_cm` and `yaw_deg` (rotation about the vertical) in the mesh's reference-pose space, where the floor is z = 0, and `size_cm` along the box's own axes. A body needs `shapes_from`, `shapes` or both |
| `joints[].child` | Child body. Its parent is the nearest ancestor bone that has a body. One joint per non-root body |
| `joints[].swing1`, `swing2`, `twist` | A number limits the axis to plus or minus that many degrees; `"locked"` and `"free"` are the alternatives |
| `collision.disable`, `enable` | Extra `[boneA, boneB]` pairs. Jointed bodies never collide. Other pairs start "off" if any pair of the template bodies they were built from was off |

What comes from the template and what from the spec: shapes (unless written out) and the orientation of
the joint axes are the template's; the set of bodies, masses, joint limits and collision pairs are the spec's.
Three things the generator sets itself, because the template's values break a fully simulated body:

- **Joint positions.** The parent-side anchor of every joint is put where the child-side anchor is in this
  mesh's reference pose. The stock MetaHuman asset places them for another body's proportions: on SandboxNPC
  the anchors of all 15 joints were 1.5 to 3.6 cm apart, and the solver closed the gaps in the first step,
  shortening the skeleton by 12 cm.
- **No motors.** The stock joints carry orientation drives that pull each joint toward the zero of its
  axes, which is not the reference pose: they flex elbows and knees. The generated asset has none; muscles
  are added at run time by `USandboxBodyController`.
- **Feet are boxes with a flat sole on the floor** (written out in `npc_body.json`). The template's foot
  shapes, a tilted box and a toe capsule that sits 1.5 cm below the floor, make the foot rock, and through a
  stiff ankle the whole body leans.

The generator log (`Saved\Tools\gen-assets.log`) lists colliding body pairs whose bounding boxes overlap in
the reference pose. Bounding boxes overstate capsules, so a small overlap can be a false alarm.

Current limitations, to be lifted when joint behaviour is tuned in M2:

- Limits are symmetric around the template's joint frame. An asymmetric range such as a knee's 0 to 150
  degrees cannot be expressed yet.
- `npc_body.json` carries the stock MetaHuman limits, summed where two template joints were merged into one
  (neck and shoulder). They are not yet the clinical ranges listed in `docs/PRD.md`.
- The template's joint axes are not mirror images of each other on the left and right side: in the reference
  pose the left elbow reads -40 degrees of twist and the right one +33. The limits are therefore not
  symmetric between sides yet. Scenario metrics show it per joint (`spawn_twist_deg` and the like).
- Limit softness (stiffness 50, damping 5 on every cone and twist limit) is still the template's.

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
(fully baked) or `stationary` (baked bounce light, dynamic shadows on movable objects).

The generator saves the map unlit. `Tools\bake-lighting.ps1` then builds lightmaps and reflection captures
with CPU Lightmass. A map is regenerated only when its spec file is newer than the map, because regenerating
throws the baked lighting away; `Tools\gen-assets.ps1 -Force` regenerates regardless.

Limitations: box shape only, engine grid material, no windows, doors or furniture yet.
