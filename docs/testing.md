# Testing: repeatability policy and measurements

## Policy

1. **Same build, same machine, same scenario: results must be identical.** Every run writes two hashes
   (all body transforms and all bone transforms, every frame). `Tools\repeatability.ps1` runs a scenario
   several times and fails if any hash differs. A mismatch is a defect to investigate, not noise to tolerate.
2. **Headless and rendered runs must agree.** `-IncludeRendered` adds one GPU-rendered run to the comparison.
   If they diverge, something in the simulation depends on rendering (visibility-based pose updates are the
   usual cause).
3. **Pass criteria are physical thresholds, not hashes.** A scenario passes on its `expect` values and the
   built-in checks. Hashes are not committed as baselines, because a different CPU, compiler or engine hotfix
   may legitimately change floating-point results.
4. **Fallback if exact repeatability is lost.** Should a later feature make identical runs impossible (async
   physics is the likely one), switch the affected scenarios to a tolerance of 0.1 cm and 0.1 degrees at every
   checkpoint over three runs (`Tools\compare-runs.ps1 -PositionToleranceCm 0.1 -RotationToleranceDeg 0.1`),
   and record the reason here.

Since M3 part 1 the solver's determinism mode is on (`p.Chaos.Solver.Deterministic=1`), after one run in
about 25 of a passive body had differed on a busy machine; see that section. Scenario processes may run
side by side: each has its own fixed timestep, and the suite gives the same hashes eight at a time as one
at a time.

Re-measure at each milestone. M0 measured a passive ragdoll on a flat floor; muscles, effectors and props add
contacts and simulation islands, which is where parallel solving could start to matter.

## Measurements

### M0, 2026-11-22

Engine 5.8.4, MSVC 14.50.35726, Ryzen 7 9800X3D, RTX 5080. Synchronous physics, 90 Hz frame, 2 substeps.
Stock mannequin (`SKM_Manny_Simple`, 22 bodies, 74.8 kg).

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `drop_passive` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `impulse_passive` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

A full clean-state bootstrap between two measurement sessions also reproduced the same hashes.

Other numbers from these runs:

- A headless scenario simulates about 10 times faster than real time (6 s in 0.5 to 0.6 s); a whole
  scenario process, including engine start-up, takes about 8 s.
- Largest rigid-joint separation during a 1.5 m drop: 0.86 cm.

### M1, 2026-11-22

Same machine and settings. MetaHuman body `SKM_SandboxNPC_BodyMesh`; the generated physics asset
`PA_SandboxNPC` has 16 bodies and 80.0 kg, the stock MetaHuman asset 21 bodies and 80.6 kg.

| Scenario | Physics asset | Largest joint stretch | Notes |
| --- | --- | --- | --- |
| `drop_passive` | Stock mannequin | 0.86 cm | |
| `drop_metahuman` | Stock MetaHuman | 0.14 cm | Rests after 5.3 s |
| `drop_npc` | Generated | 0.13 cm | Rests after 4.4 s |
| `hang_npc_hand` | Generated | 0.49 cm (right wrist) | Whole body hangs from the right hand for 10 s; M1 limit is 3 cm |

Headless and rendered runs of `drop_npc` and `hang_npc_hand` gave identical hashes.

Lighting: the test room (5 m by 4 m by 2.7 m, two point lights, 128-texel lightmaps) bakes from the command
line with CPU Lightmass at Preview quality in 90 s the first time and 12 to 17 s afterwards. No Lightmass
problems of the kind reported on the forums for 5.6 to 5.8 appeared. Higher qualities were not tried.

Flat launch with the OpenXR plugin enabled (headset off, SteamVR not running): `-nohmd` alone still let the
plugin create an OpenXR instance, which took 26.5 s and was then rejected as not viable. The run itself
passed with unchanged hashes and SteamVR was not left running. With `-DisablePlugins=OpenXR` added the same
rendered scenario takes 10.7 s end to end. Start-up time of a flat launch with the headset connected has
not been measured yet.

First run in the headset (PlayStation VR2 through SteamVR 2.18.4, OpenXR), scenario `vr_room_look`: the
MetaHuman standing in the baked test room, 30 simulated seconds, engine renderer settings untouched.

| Measure | Value |
| --- | --- |
| Render target chosen by SteamVR | 7456 x 3800 for both eyes (3728 x 3800 per eye, about 28 megapixels) |
| GPU time per frame | 19.7 ms mean, 22.1 ms at the 99th percentile |
| Frame interval | 22.4 ms mean: the headset ran at half rate (45 FPS) with reprojection |
| Render thread | 6.0 ms mean |
| Same scene flat at 1280 x 720 | 1.2 ms GPU |

This misses the 9 ms GPU budget by a factor of two, as expected before any VR tuning. Known contributors:
instanced stereo is off (the log says so), the render target is about three times the pixel count assumed
in the plan, no upscaler is in use, and SteamVR reported no visibility mask for the headset ("Failed to
create all visibility mask meshes"), so the hidden corners of each eye are shaded too.

Renderer sweep in the headset, scenario `vr_room_perf` (same scene, 15 simulated seconds, 1260 timed frames
per run). "Scale" is `xr.SecondaryScreenPercentage.HMDRenderTarget`, a percentage of SteamVR's target per axis.

| Anti-aliasing | Scale | Other | GPU ms, mean / p99 | Frame interval ms | Headset rate |
| --- | --- | --- | --- | --- | --- |
| TSR (engine default) | 100 | | 19.1 / 19.7 | 22.2 | 45 FPS |
| TAA | 100 | | 10.6 / 12.0 | 11.2 | 90 FPS, at the limit |
| TAA | 100 | instanced stereo | 10.9 / 11.5 | 11.7 | drops frames |
| TAA | 85 | | 7.5 / 7.9 | 11.1 | 90 FPS |
| TAA | 85 | instanced stereo | 7.6 / 8.0 | 11.1 | 90 FPS |
| TAA | 70 | | 5.0 / 5.4 | 11.1 | 90 FPS |
| TAA | 70 | no screen-space reflections, no ambient occlusion | 3.7 / 4.1 | 11.1 | 90 FPS |
| FXAA | 70 | | 4.9 / 5.2 | 11.1 | 90 FPS |

Conclusions:

- **TSR was the dominant cost**: about 8.5 ms of the 19 ms at the full target. The project default is now TAA.
- **The project default scale is 85%** (about 20 megapixels per frame, still 2.5 times the panels' pixel
  count). With both defaults and no overrides the scene measures 7.1 ms mean and 7.8 ms at the 99th
  percentile, inside the 9 ms budget, at a steady 90 FPS.
- **At SteamVR's full target the budget is missed** (10.6 ms).
- **Instanced stereo gained nothing** in this scene on GPU or render thread, and has open bugs in 5.8, so it
  stays off.
- These numbers are for a grey-box room and a MetaHuman without hair. Final materials, furniture, hair,
  the animated face rig and the active ragdoll will all add cost; the measured headroom is about 2 ms.
- DLSS has not been tried: the NVIDIA plugin is not installed.
- The owner viewed the scene in the headset with the project defaults (TAA, 85% scale) and judged the image
  sharp and acceptable, with the stutter of the 45 FPS runs gone.

Two findings to carry forward:

- **The world pin itself gave by up to 3.7 cm** in `hang_npc_hand` (`max_pin_error_cm`), far more than any
  skeleton joint. A grab in M4 is the same kind of constraint, so it will need attention there (iterations,
  projection or a stiffer formulation).
- **An earlier reading of 3.6 cm stretch for the MetaHuman was wrong.** It was the authored anchor offset
  at `spine_02` being closed in the first frames, now reported separately as `initial_joint_offset_cm`.

Correction made in M2: the stretch figures for the generated asset in the table above were measured while
the asset still carried the template's joint motors and anchor gaps on every joint, not only at `spine_02`.
Both are fixed now and the figures are superseded by the M2 step 1 section below.

### M2 step 1 (the character stands), 2026-11-22

Same machine and settings. The NPC (`SKM_SandboxNPC_BodyMesh` with the regenerated `PA_SandboxNPC`) is spawned
with its feet on the floor and held by `USandboxBodyController`: 15 muscles (one per joint, force-mode
springs toward the reference pose) and 5 balance-assist controls (pelvis, chest, head, both feet), with 64
solver position iterations on its bodies. Default tuning, bare floor, headless.

| Measure | `stand_npc` (10 s) | `stand_npc_10min` (600 s) | Limit |
| --- | --- | --- | --- |
| Pelvis drift from the spawn pose, maximum | 0.76 cm | 0.76 cm | 5 cm (M2 gate) |
| Pelvis drift at the end | 0.53 cm | 0.50 cm | |
| Lowest head height | 158.3 cm | 158.3 cm | 150 cm |
| Largest joint stretch | under 0.01 cm | under 0.01 cm | 2 cm (M2 gate) |
| Fastest body after 2 s, linear | 0.7 cm/s | 0.7 cm/s | 5 cm/s |
| Fastest body after 2 s, angular (a foot) | 0.07 rad/s | 0.13 rad/s | 0.3 rad/s |
| Assist load, force over body weight | 0.044 | 0.046 | |
| Assist load, torque over weight x centre-of-mass height | 0.0019 | 0.0020 | |
| Solver time per frame, mean | 0.40 ms | 0.39 ms | 1.5 ms (M2 gate) |
| Wall-clock time of the run | 1.1 s | 51 s | |

Both scenarios gave identical hashes in repeated headless runs and in a GPU-rendered run (see the table at
the end of this section). The 10-minute scenario simulates about 12 times faster than real time.

The assist-load figures are the baseline for the M3 criterion "standing assist share is at most half its M2
value". They are estimates: the solver does not report drive forces, so the load is computed from the assist
springs' settings and each body's distance from its target.

How much each part contributes, from 10-second runs with tuning overrides:

| Variant | Pelvis drift | Foot slide (left foot, sideways / forward) | Jitter (fastest foot) | Assist force share |
| --- | --- | --- | --- | --- |
| Default | 0.76 cm | 1.0 / 0.4 cm | 0.07 rad/s | 0.044 |
| 8 position iterations (engine default) | 0.98 cm | 4.0 / 5.2 cm and still sliding | 0.54 rad/s | 0.153 |
| 32 position iterations | 0.68 cm | 1.7 / 1.3 cm | 0.21 rad/s | 0.074 |
| No assist on the feet | 0.85 cm | 1.9 / 0.4 cm | 0.08 rad/s | 0.011 |
| No assist on pelvis, chest and head | 3.06 cm | 1.1 / 0.6 cm | 0.14 rad/s | 0.037 |
| No assist at all | 3.40 cm | 1.8 / 1.2 cm | 0.15 rad/s | 0 |
| No leg muscles | falls | | | |

With no assist at all the character still stands for the full 10 minutes (head at 158 cm) but walks 52 cm
across the floor on its vibrating feet. So at level 0 the legs carry the body and the assist mostly anchors it.

Solver cost against iterations (mean solver time per frame, one character on a bare floor): 0.26 ms at 8,
0.31 ms at 32, 0.40 ms at 64.

What it took to get here, in the order found. Each of the first three is a defect of the M1 physics asset
that the M1 scenarios could not show, because a body in free fall hides them:

1. **Joint anchors apart on every joint.** M1 recorded "a 3.6 cm offset at `spine_02`". That was the largest
   of 15: every joint had a gap of 1.5 to 3.6 cm, because the stock MetaHuman asset places its joints for
   another body's proportions. Closing the gaps shortened the skeleton by 12 cm in the first step. The
   generator now puts each parent-side anchor on the child-side one; the gap reads 0.000003 cm.
2. **Motors in the template.** Every stock joint has an orientation drive (stiffness 1000 to 5000) toward
   the zero of its axes, which is not the reference pose. They flexed elbows and knees: the hands moved
   30 cm in the first tenth of a second. The generator now clears them.
3. **Feet not flat.** The template's toe capsule sits 1.5 cm below the floor in the reference pose, so the
   foot pitched and the body leaned 6 degrees back through the stiff ankle. The feet are now boxes written
   out in the spec, with the sole on the floor. Pelvis drift went from 4.1 to 1.0 cm.
4. **Acceleration-mode drives.** Physics Control's default scales a drive by the inertia of the two jointed
   bodies, which made the load-bearing joints far too weak; the pose crept for as long as the run lasted.
   Muscles now use force mode, in N*m/rad.
5. **Solver convergence.** See the iteration rows above.

Effect of the corrected asset on the M1 scenarios (passive body, 8 iterations). The M1 figures were taken
with the template's motors still in the asset, which stiffened the body; these replace them:

| Scenario | Largest joint stretch, M1 | Now | Notes |
| --- | --- | --- | --- |
| `drop_npc` | 0.13 cm | 0.82 cm (right knee) | Rests after 2.4 s instead of 4.4 s |
| `room_drop_npc` | not recorded | 0.61 cm | Rests after 3.8 s |
| `hang_npc_hand` | 0.49 cm | 1.52 cm (right wrist) | M1 limit of 3 cm still met. The world pin gives by 9.7 cm (3.7 cm before) |

Not yet checked: the conversion from N*m/rad to the engine's units is taken from the engine source (unit
scale and Chaos's factor of 1.5 on angular drives), not from a calibration measurement. The arms' sag under
gravity is of the expected size (about 2 cm at the hand for 150 N*m/rad), which rules out an error of an
order of magnitude but not one of tens of percent.

Repeatability:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs (54,000 frames each) |

### M2 step 2 (the parameter file), 2026-11-22

Same machine and settings. The body's tuning now comes from `Config/Tuning/body.json` through
`FSandboxParamStore`, and every active run watches the tuning files (size and modification time, every 30
frames).

Nothing changed for a run that changes nothing. The whole suite was run before the step and after it, and
the hashes of all 13 earlier scenarios are equal. For the two stand scenarios:

| Scenario | Hashes before (values in code) | Hashes after (values from the file, files watched) |
| --- | --- | --- |
| `stand_npc` | bodies 55b1e27c, bones d4f09323 | the same |
| `stand_npc_10min` | bodies dde3a3e2, bones a38aa1a1 | the same |

So the numbers read from JSON are the same doubles the compiler made of the constants, and the 1,800 looks
at the files during the 10-minute run leave no trace. Their cost is not measurable either: the run takes
51.4 s of wall-clock time, as before (51.1 s).

A change in the middle of a run, by each route:

| Scenario | Route | Change | Reaches the body at | Result |
| --- | --- | --- | --- | --- |
| `tune_live_override` | Console command `Sandbox.Tune`, run at t = 4 s | Leg muscles 800 to 0 N*m/rad | t = 4.000 s (frame 360) | Pelvis from 90.0 cm to 17.3 cm; fallen at 5.10 s |
| `tune_live_file` | The value written into the file at t = 4 s; the session notices it | The same | t = 4.011 s (frame 361, after the look at frame 360) | The same; fallen at 5.11 s |
| `tune_live_solver` | Console command, run at t = 3 s | Position iterations 64 to 8 | t = 3.000 s (frame 270) | Feet start to rock: 0.54 rad/s after 4 s, against 0.07 unchanged |

Without leg muscles the character does not topple: it sinks and ends sitting on the floor with a straight
back, held there by the spine muscles and the upright assist (checked on a screenshot). Its head stays at
77 cm, which is why "fallen" is judged by the pelvis (below half its starting height), not by the head.
Joint stretch in these runs stays under 0.06 cm and the fastest joint reaches 9.2 rad/s.

Checks made once, with throw-away scenarios, that every kind of value applies to a running body:

| Changed at t = 3 s of a 10 s stand | Measure after t = 4 s | Changed live | Set from the start (`-Tune`) | Unchanged |
| --- | --- | --- | --- | --- |
| `solver.position_iterations` 64 to 8 | Fastest foot | 0.544 rad/s | 0.537 rad/s | 0.074 rad/s |
| | Assist force share | 0.147 | 0.153 | 0.044 |
| | Solver time per frame, median | 0.27 ms | 0.25 ms | 0.42 ms |
| All assist stiffness to 0 | Assist force share | 0 | 0 | 0.044 |
| A value out of range (`muscles.arms.stiffness_nm_per_rad=-1`) | Tuning changes | 0, with the reason in the log; hashes equal to the unchanged run | the run does not start | |

The first row did not hold at first. The live change of the iteration count was accepted by the store and
the controller and recorded as a tuning change, and the run's hashes were equal to the unchanged run: the
solver never saw it. Chaos takes a simulation island's iteration counts from its bodies only when something
joins or leaves the island, and a standing character's island never changes. The controller now adds a
constraint that restricts nothing for one frame whenever the counts change. `tune_live_solver` guards this
with a lower bound on the jitter, which only the reduced count reaches.

An edit from outside the process, once, by hand: `stand_npc_10min` was started on a copy of the tuning
files (`-TuningDir`), and 25 s later a script changed `muscles.arms.stiffness_nm_per_rad` from 150 to 120
in that copy. The run reported the reload 12 ms after the write, at 217.3 s of simulated time, and finished
with the new value. Where such an edit lands in simulated time depends on the wall clock, so a run that is
edited from outside is not repeatable; that is the nature of the edit, not of the watching.

What stops a run before it starts (exit code 2, with the reason): an unknown name in `-Tune`, a value out
of range, a misspelt name in a tuning file, a value missing from the tuning files.

Repeatability:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `tune_live_override` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `tune_live_file` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `tune_live_solver` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

`tune_live_file` is repeatable because the scenario runner looks at the files on fixed frames of the run
and the scenario writes the file itself at a fixed simulated time. Outside scenarios the store looks every
30 engine frames, counted from start-up.

Not measured: hot reload in a live interactive session (there is no pawn to run one with before step 4),
and per-state values on a body (the store resolves them and unit tests cover that; the body has no states
before step 3).

### M2 step 3 (states and the push matrix), 2026-11-22

Same machine and settings. Three things changed: the joints of `PA_SandboxNPC` are built from anatomy
(`Specs/README.md`), the body has the states Standing, Flinch, Falling and Lying, and `Tools\push-matrix.ps1`
exists. Every hash of an NPC scenario changed with the joints; the figures below replace the earlier ones.

**Joints.** Where the reference pose sat in the joint limits, before and after:

| Joint | Before (stock axes): left / right | After: left / right |
| --- | --- | --- |
| Elbow | twist -40.0 / +32.9 of a limit of 70 | 36.6 degrees of bend on both sides, range 0 to 140 |
| Knee | twist -54.0 / -49.0 of a limit of 60 | straight on both sides, range -2 to 150 |
| Shoulder | swing1 -46.1 / -63.0, swing2 39.1 / 20.1 | identical on both sides |
| Hip | swing2 -20.0 / +20.0 (mirrored), swing1 40.0 / 40.0 | identical on both sides |

No joint is outside its limits in the reference pose, and every left and right pair reads the same to the
last digit (`spawn_*_deg` in `metrics.json`).

The knee's range is -2 to 150 degrees, not the plan's 0 to 150. With the limit at 0 a standing knee sits
exactly on it:

| Knee range | Fastest foot after 2 s, left / right | Pelvis drift, maximum |
| --- | --- | --- |
| 0 to 150 | 0.072 / 0.133 rad/s | 0.81 cm |
| -2 to 150 | 0.071 / 0.079 rad/s | 0.95 cm |
| -5 to 150 | 0.070 / 0.078 rad/s | 0.95 cm |
| -10 to 150 | 0.070 / 0.077 rad/s | 0.95 cm |

**Standing on the new joints** (default tuning, bare floor, headless):

| Measure | `stand_npc` (10 s) | `stand_npc_10min` (600 s) | Step 1 (10 s) | Limit |
| --- | --- | --- | --- | --- |
| Pelvis drift, maximum | 0.95 cm | 0.95 cm | 0.76 cm | 5 cm |
| Lowest head height | 158.3 cm | 158.3 cm | 158.3 cm | 150 cm |
| Largest joint stretch | under 0.01 cm | under 0.01 cm | under 0.01 cm | 2 cm |
| Fastest body after 2 s, angular (a foot) | 0.079 rad/s | 0.106 rad/s | 0.07 rad/s | 0.3 rad/s |
| Assist load, force over body weight | 0.047 | 0.051 | 0.044 | |
| Largest joint-limit violation | 0.01 degrees | 0.01 degrees | not measured | 1 degree |
| Solver time per frame, mean | 0.41 ms | 0.41 ms | 0.40 ms | 1.5 ms |

The state machine adds nothing measurable to a standing run: with it and without it the hashes of
`stand_npc` were equal.

**States.** Scenarios of the suite, with the time each state began:

| Scenario | Stimulus | States | Notes |
| --- | --- | --- | --- |
| `hit_chest_10_stands` | 10 N*s blow on the chest at 3 s | flinch 3.00 s, standing 3.36 s | Pelvis moves 1.2 cm |
| `hit_head_30_stands` | 30 N*s blow on the head | flinch 3.00 s, standing 3.36 s | Neck turns at up to 13.0 rad/s; pelvis moves 4.3 cm |
| `hit_chest_200_falls` | 200 N*s blow on the chest | flinch 3.00 s, falling 3.08 s, lying 4.14 s | Fallen (pelvis below half height) at 3.40 s; joint stretch 0.04 cm |
| `push_thigh_200_falls` | 200 N*s on the left thigh over 0.1 s | flinch 3.00 s, falling 3.13 s, lying 5.38 s | Joint stretch 0.16 cm; a hip 4.7 degrees past its limit |
| `tune_live_override` | Leg muscles switched off at 4 s | falling 4.42 s, lying 6.22 s | Falling because the pelvis sank 20 cm; before step 3 this body ended sitting upright |
| `hang_npc_hand` | Pinned by the right hand in the air | falling 0.38 s | Never at rest: it swings |

Screenshots of `hit_chest_200_falls` (on its back, limbs loosely straight) and of the passive `drop_npc`
(prone, one leg bent) show no joint bent the wrong way.

What the flinch does, measured on `hit_head_30_stands`: with the hit muscles weakened to 30% the neck turns
at up to 13.0 rad/s; with `reactions.flinch_weakening=1` (no weakening) at 9.8 rad/s. The body stands in
both runs.

**Push matrix.** 128 runs, 8 at a time, 169 s. Each cell is how many of the 8 directions ended in a fall;
the M2 rules are the first and the last column.

Single blow (the default, and what `Tools\bootstrap.ps1` runs):

| Body | 10 N*s | 30 N*s | 80 N*s | 200 N*s |
| --- | --- | --- | --- | --- |
| Head (`neck_01`) | 0 of 8 | 0 of 8 | 8 of 8 | 8 of 8 |
| Chest (`spine_04`) | 0 of 8 | 0 of 8 | 1 of 8 (straight back) | 8 of 8 |
| Left thigh | 0 of 8 | 0 of 8 | 0 of 8 | 8 of 8 |
| Right thigh | 0 of 8 | 0 of 8 | 0 of 8 | 8 of 8 |
| Pelvis movement of those that stood | 1 to 3 cm | 2 to 8 cm | 7 to 14 cm | |
| Fastest joint | 4 rad/s | 13 rad/s | 31 rad/s | 40 rad/s |
| Largest joint stretch | 0.01 cm | 0.01 cm | 0.12 cm | 0.31 cm |

Every mirror-image pair (the same push reflected left to right, on the other side's body) ended the same
way: 56 pairs, none different. The same impulses delivered as pushes over 0.1 s (`-PushSeconds 0.1`,
measured before the knee's range was changed) gave the same table of falls, with a fastest joint of
32 rad/s and a largest stretch of 0.18 cm.

How the fall threshold was set. With `thresholds.fall_pelvis_offset_cm` at 20, a 200 N*s push on a thigh
moved the pelvis by 18.4 to 19.6 cm and the character came back upright in 12 of 16 cases: the feet are
held by their assist and the legs are stiff. At 15 cm, which is about where the centre of mass leaves the
feet's support, every 200 N*s push ends in a fall and 80 N*s on the chest still stands in 7 directions of 8.
The outcomes for 30 and 80 N*s are recorded, not judged: the outcome table is an M3 criterion.

**Solver iterations decide the stretch under load.** Hanging from one hand, 10 s:

| Body | Position iterations | Largest joint stretch (wrist) | The pin gives by |
| --- | --- | --- | --- |
| Passive, new joints | 8 (engine default) | 2.88 cm | 9.0 cm |
| Passive, new joints | 64 (console variable) | 0.51 cm | 2.0 cm |
| The NPC under active control (falls limp at 0.38 s) | 64 (its own) | 0.13 cm | 0.5 cm |

`hang_npc_hand` now runs the third row, with a limit of 2 cm: a passive NPC does not exist in the product.
This supersedes the "world pin gives by 9.7 cm" finding of step 1 for the NPC proper; a grab in M4 starts
from 0.5 cm.

**Passive bodies overrun their joint limits.** The new metric shows a passive body dropped on the floor
pushing a joint 36 to 40 degrees past its limit (`drop_npc`, `room_drop_npc`), against 0.2 to 4.7 degrees
for the NPC under control in the fall scenarios. The limits are soft (stiffness 50, damping 5, still the
template's) and a passive body has 8 iterations. Not a problem for the NPC as it runs, but the softness is
untuned.

Repeatability, final build of the step:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `hit_chest_10_stands` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `hit_chest_200_falls` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `push_thigh_200_falls` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `hang_npc_hand` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `drop_npc` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

The matrix runs eight scenario processes side by side; each uses a fixed timestep, so its result does not
depend on the others. `hit_chest_200_falls` and `push_thigh_200_falls` gave the same hashes inside the
suite and alone.

`Tools\bootstrap.ps1` now takes about 7.5 minutes: the suite of 20 scenarios 4.3 minutes, the push matrix
2.8 minutes.

Not measured: hits by another body (there are no effectors before step 4), the flinch's effect on anything
but the neck, and falls against walls or furniture.

### M2 step 4 (desktop pawn with effectors and tools), 2026-11-22

Same machine and settings. New in this step: the player's hands (effectors), the character's sense of
contact, the `effector` scenario event, the desktop pawn and the interactive session (`Tools\play.ps1`).

**Nothing changed for a run without hands.** The whole suite was run before the step and after it. All 20
earlier scenarios give the same two hashes, although the character now receives contact events from the
physics scene in every frame, the game mode has a new pawn class, and the scene is built by shared code.
`room_stand_npc`, new, gives the same hashes as `stand_npc`: the test room's floor is the bare floor to the
solver. The solver's mean time per frame is unchanged (`stand_npc` 0.41 ms before and after).

**The force cap, measured from the other side.** A hand's spring may apply 300 N. In `effector_force_cap`
the hand's target is moved 30 cm below the floor and left there; the hand comes to rest on the floor and
the floor's contact events report what it presses with:

| Measure | Value |
| --- | --- |
| Contact force in the run's last frame | 300.000 N (cap 300 N) |
| Largest contact force of any frame | 300.0 N |
| Hand's distance from its target | 34.5 cm; the spring law asks for 1157 N at most |
| Same, with the cap changed to 100 N at 4 s from the console (`tune_live_effector`) | 100.000 N |

This is one measurement of three things: the cap holds, the conversion of newtons into the engine's units
is right, and the impulses of the contact events (summed over the two substeps of a frame) are the real
ones. The hand is alone in its simulation island here, so this is the engine's 8 position iterations.

The engine caps a drive axis by axis. Without correction a spring with one cap would be 1.41 times stronger
along a face diagonal and 1.73 times along a space diagonal. The caps are therefore set every frame along
the direction of the force asked for. Checked once with a throw-away scenario: two free hands whose targets
jump 100 cm in one frame, one along x and one along the space diagonal.

| Hand | Peak speed |
| --- | --- |
| Target jumps 100 cm along x | 11.7968 m/s |
| Target jumps 100 cm along (-1, 1, 1) | 11.7968 m/s |
| Along x, mass 3 kg from the start (`-Tune`) | 9.6627 m/s |
| Along x, mass changed from 1.5 to 3 kg at 1 s, a second before the jump | 9.6627 m/s, and the same hashes as the row above |

The last two rows are the check that a value made tunable at run time reaches the solver: the run with the
live change is identical to the run that had the value from the start.

**Hands and the character.** Default tuning (hand 1.5 kg, 3000 N/m, 300 N), bare floor, headless. The hand's
figures come from its own contact events, the character's from the character's.

| Scenario | The hand's target | Hand delivers | Character feels | States | Notes |
| --- | --- | --- | --- | --- | --- |
| `effector_push_chest` | Into the chest at 2.5 m/s and straight back | 17.75 N*s in 5 frames, peak 540 N | 17.75 N*s in 9 reports | flinch 3.20 s (hit of 6.0 N*s on `spine_04`), standing 3.57 s | Pelvis moves 2.2 cm |
| `effector_hit_head` | Into the side of the head at 7.5 m/s | 14.10 N*s in 1 frame, peak 1269 N | 14.10 N*s in 1 report | flinch 3.08 s (hit of 14.1 N*s on `neck_01`), standing 3.44 s | Hand reaches 8.28 m/s; neck turns at 6.2 rad/s |
| `effector_push_chest_falls` | Into the chest and on through it for 1 s | 114.07 N*s in 46 frames, peak 503 N | 114.07 N*s in 91 reports | flinch 3.24 s, standing 3.61 s, flinch 3.61 s, falling 3.64 s (pelvis 15.1 cm away), lying 5.06 s | Joint stretch 0.02 cm; fastest joint 7.6 rad/s |
| `effector_miss` | Across in front of the chest, not touching | 0 | 0 | standing | Pelvis drift 0.95 cm, as in `stand_npc` |

What the two sides count agrees to the last digit, as it should: it is the same contact seen from both
bodies. The character knows nothing of the hand. It counts every contact with another simulated body and
leaves out the static world, because the floor under its feet reports 8.7 N*s per frame, more than the
flinch threshold of 5.

Two things these runs show that the plan did not say:

- **The peak contact force is not held by the force cap** (540, 1269 and 503 N against a cap of 300). A
  hand that is stopped gives up its momentum within a frame, on top of what the spring adds: 1.5 kg at
  8.3 m/s is 12.4 N*s. The cap is a limit on sustained force, and is checked that way above.
- **A held push makes the character flinch again the moment a flinch ends** (3.61 s in the third row). Each
  frame of contact adds to the felt impulse, and the threshold is reached again within two frames. The same
  would happen with an `impulse` push longer than a flinch. Left as it is: what a sustained push should do
  to the muscles is M3.

Largest joint stretch in the six hand scenarios: 0.02 cm. Largest joint-limit violation: 0.1 degrees.

**Cost.** One hand in the scene adds 0.03 ms to the solver's mean time per frame (`effector_miss` 0.44 ms
against `stand_npc` 0.41 ms); the most expensive run of the suite is `effector_push_chest_falls` at
0.46 ms. The M2 limit is 1.5 ms.

**The interactive session.** `Tools\play.ps1 -Smoke` runs the session without a person: it uses each tool
through the function its key calls and asks the world what happened. 480 frames, 13 checks, all pass,
headless and rendered:

| Check | Result |
| --- | --- |
| The pawn has two hands, resting on their targets | 0.00 cm from target |
| Impulse tool, one click on the chest | flinch from a "hit of 30.0 N*s": the same entry as a scenario's `impulse` |
| Pause | 0 frames simulated in 30 paused frames; the pelvis transform is bit-equal |
| Single step | exactly 1 frame simulated |
| Slow motion, divisor 4 | 20 frames simulated in 80 |
| Drag handle on the left forearm, camera raised 30 cm | holds `lowerarm_l`; the held point rises 22.4 cm and stays 7.7 cm behind its target (the arm's muscles pull back, the handle's force is capped) |
| Right hand reaches into the chest | the hand delivers 38.0 N*s, the character feels 38.0 N*s and flinches |
| Reset | a fresh character, standing, pelvis 1.5 cm from where the first one started |

A window opened by `Tools\play.ps1` and left alone for 30 s logged 900 frames in 10.00 s and 900 frames in
10.01 s by the clock, with no error in the log: the session holds the simulation's 90 frames per second.
`-Smoke -Shot` gives a picture of the session's view: the two hands below the crosshair, the HUD lines, the
character in the room.

Repeatability, final build of the step:

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `effector_force_cap` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_push_chest` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_hit_head` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_push_chest_falls` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `effector_miss` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `tune_live_effector` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

The hands' transforms are part of the `bodies` hash of a run that has hands.

`Tools\bootstrap.ps1` now takes about 8.5 minutes: the suite of 27 scenarios 5.1 minutes, the push matrix
2.9 minutes, the session's self-test 10 seconds. The push matrix gives the same table and figures as in
step 3.

**For a person to check**, because no run without one can: that the keys and the mouse do what the HUD
says; that the camera turns the right way up and down; that the hands feel weighted and do not launch the
character; that the drag handle is usable; that slow motion is readable; and hot reload in a live session
(save `Config/Tuning/effectors.json` with another `max_force_n` while the window is open and see the hands
change, with "Effectors: new tuning values applied" in the log). The steps are in the report of the step.

Not measured: strikes at full speed and tunnelling (step 5; continuous collision detection is on for the
hands but nothing here tests it), a hand against a fallen character, two hands at once, anything in the
headset.

### M2 step 5 (overlays, strikes and the M2 gate figures), 2026-11-22

Same machine and settings. New in this step: debug overlays (`Sandbox.Overlay`), seven strike scenarios, the
runner's own measurement of penetration and tunnelling, and two limits that `Tools\suite.ps1` holds the
whole set to.

**Nothing changed for the runs that existed.** The suite was run on the build of step 4 before any change
and on the final build: all 27 earlier scenarios give the same two hashes. That includes one change to the
body itself (the character's bodies now ask for contact events, see below).

**Strikes.** A hand's target is sent in a straight line through a place on the standing character at 6 m/s
(slap) or 8 m/s (punch) and back. Default tuning (hand 1.5 kg, 300 N), bare floor, headless. The hand
arrives faster than its target moves, because it starts behind and has to catch up.

| Scenario | Hand arrives at | Touches first | Hand delivers | Peak force | Struck body moves | Fastest joint | Deepest penetration |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `strike_slap_head` (from the side) | 6.25 m/s | `neck_01` | 11.3 N*s in 1 frame | 1017 N | 7.9 cm | 6.0 rad/s (neck) | 0.000 cm |
| `strike_punch_head` (from the front) | 8.87 m/s | `neck_01` | 15.7 N*s in 1 frame | 1415 N | 13.2 cm | 5.8 rad/s (neck) | 0.000 cm |
| `strike_slap_torso` | 6.25 m/s | `spine_04` | 15.2 N*s in 5 frames | 956 N | 4.9 cm | 1.6 rad/s (neck) | 0.000 cm |
| `strike_punch_torso` | 8.95 m/s | `spine_04` | 29.3 N*s in 8 frames | 1394 N | 6.9 cm | 3.6 rad/s (chest) | 0.003 cm |
| `strike_slap_leg` (outside of the left thigh) | 6.25 m/s | `thigh_l` | 21.9 N*s in 6 frames | 1325 N | 1.4 cm | 2.8 rad/s (ankle) | 0.004 cm |
| `strike_punch_leg` (front of the right thigh) | 8.87 m/s | `thigh_r` | 28.2 N*s in 6 frames | 1859 N | 2.0 cm | 2.6 rad/s (knee) | 0.010 cm |
| `strike_punch_forearm` | 8.87 m/s | `lowerarm_l` | 13.7 N*s in 2 frames | 962 N | 22.0 cm | 10.1 rad/s (shoulder) | 0.002 cm |

In all seven: no tunnelling; the character flinches (3.07 to 3.09 s) and is standing again 0.36 to 0.37 s
later;
the pelvis moves 1.4 to 3.3 cm; joint stretch is 0.03 cm at most and no joint is more than 0.1 degrees
outside its limits. What the hand delivered and what the character felt agree to the last digit, as in
step 4. The M2 limits are no tunnelling and 30 rad/s; the fastest joint in any strike turns at a third of
that.

The thresholds of the scenarios for "the struck region is visibly displaced" are a choice made here, not a
given: 5 cm for the head, 3 cm for the chest, 1 cm for a thigh (measured: 7.9 and 13.2, 4.9 and 6.9, 1.4
and 2.0). A thigh is 11.3 kg on a leg that stands on the floor; a slap does not move it far.

**Tunnelling and penetration: how they are measured.** After every frame the runner asks the scene, with
queries that change nothing: how deep is the hand's sphere inside each body of the character and inside
the static world (the distance it would have to move to be clear), and does the straight line from where
the hand's centre was a frame ago to where it is now cross a surface. The centre of a sphere cannot get
closer to a surface than its radius, so a crossing, or a depth of the radius or more, means the hand went
through; such frames are counted (`tunnel_frames`) and any of them fails the run
(`effectors_not_tunnelled`, checked in every run that has hands). The solver's own contacts are not used:
what it missed it cannot report. The measurement is taken once per frame, so what happens between two
substeps is not seen.

In the six hand scenarios of step 4 the same measurement reads 0 tunnelled frames and 0.004 cm at most.

**The metric fails when it should, and continuous collision detection is what keeps it from failing.**
A check made once: the strike scenarios with the target's speed multiplied, the hand's force cap raised to
6000 N so that it can follow, and CCD on or off (`-CVars "p.Chaos.Solver.UseCCD=0"`). The hand's speed is
what it reached, not what was asked for.

| Hand's speed | CCD | Deepest penetration, worst of the seven | Tunnelled |
| --- | --- | --- | --- |
| 6.25 and 8.87 m/s (the scenarios as they are) | on | 0.01 cm | no |
| the same | off | 0.01 cm | no |
| 11.8 to 15.8 m/s | on | 0.03 cm | no |
| the same | off | 1.88 cm (chest) | no |
| 16.6 to 22.1 m/s | on | 0.01 cm | no |
| the same | off | 2.26 cm (chest) | no |
| 18.8 to 25.1 m/s | on | 0.02 cm | no |
| the same | off | 1.68 cm (head) | no |
| 24.6 to 28.1 m/s | on | 0.01 cm | no |
| the same | off | 6.54 cm (chest), deeper than the hand's 4.5 cm radius | yes: `effectors_not_tunnelled` fails |

So at strike speeds the hand does not tunnel with or without CCD. With CCD nothing tunnels up to 28 m/s,
three times the punch's speed; without it the hand sinks 1.9 cm into the chest from 15 m/s and goes through
at 25 m/s. CCD on the hands stays on. Two more things these runs show:

- **Below 6.5 m/s CCD is not in play at all.** Chaos sweeps a body only when it moves more than 0.4 of its
  smallest extent in a substep, 3.6 cm for the 9 cm hand. The two slaps give the same hashes with CCD off;
  the punches do not.
- **Far above strike speeds CCD costs momentum.** From 22 m/s some hits with CCD deliver about half the
  impulse of the same hit without it (11.4 against 23.7 N*s on the head at 25 m/s), and the hand's speed at
  the first contact reads half its peak. Nothing a hand or a 10 m/s kick comes near, but a thrown prop
  could.

At three times the punch's speed joints do pass 30 rad/s: 34 rad/s in the shoulder after a forearm hit at
25 m/s, and 179 rad/s in the wrist in one run. The limit is met with a factor of three at 8.9 m/s, not at
any speed.

**The character did not hear its own contacts.** The overlay had no contacts between the feet and the floor
to draw, and a character knocked down onto the floor reported none either. The cause is in how the engine
builds a body's collision filter: a body of a skeletal mesh asks for contact events only if its own flag
and the flag of the component's body instance are both set, and `USkeletalMeshComponent::
SetNotifyRigidBodyCollision` sets only the first. So since step 4 the character had been told of a contact
only when the other body asked for events itself. The hands do, which is why every hand scenario agreed to
the last digit; the floor does not, and a thrown prop would not have. The controller now sets both flags.
After the change each foot reports 4.4 N*s per frame against the floor (two reports, one per substep):
392 N per foot, half the character's weight. The character still counts only contacts with bodies that
move under physics as hits, so nothing it does changed: all hashes are equal, and the solver's mean time is
the same (0.40 ms in `stand_npc`).

**Overlays.** Seven layers: centre of mass, support polygon, joint drive targets against the pose, balance
assist, contacts, controller state, hands. They are drawn by a subsystem that only reads. Checked on the
whole suite:

| Run | Scenarios | Hashes equal to the plain headless run |
| --- | --- | --- |
| Headless, `-Overlay all` (the overlay says it has no renderer and stays off) | 34 | 34 |
| Rendered, `-Overlay all` (the overlay draws every frame) | 34 | 34 |

Pictures looked at: `strike_punch_torso` at the frame of the hit (the arrow of the contact on the chest
with its force, the flinch named above the head, the support polygon under both feet with the two arrows
of the floor's reaction, the centre of mass 9.4 cm inside the polygon) and the session's view from
`Tools\play.ps1 -Smoke -Shot -Overlay all`. The session's self-test has a check for them in each mode: with
a renderer the V key's command gives 147 line segments and 8 lines of text; without one it gives nothing.

**A reset logged warnings nobody read.** The session's self-test passed while its log held 30 warnings of
Physics Control ("Failed to find bone data", two per muscle) in the frame of the reset. The new character is
spawned in the middle of a frame and its controller was ticked in that same frame, before the mesh had any
bone transforms. The controller now waits for the mesh's first pose: 0 warnings, and the reset check reads
as before (pelvis 1.52 cm from where the first character started). Scenarios spawn their body before the
world's first tick and never had the warnings.

**Repeatability.** The runs below were made one change before the final build (the controller's wait for
the mesh's first pose, above); the final build gives the same hashes for all 34 scenarios.

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `strike_slap_head` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_head` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_slap_torso` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_torso` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_slap_leg` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_leg` | 3 headless + 1 rendered | Identical hashes in all 4 runs |
| `strike_punch_forearm` | 3 headless + 1 rendered | Identical hashes in all 4 runs |

**The limits for the whole set** (`Tools\suite.ps1`, 34 scenarios, 375 s):

| Limit (M2 gate) | Worst scenario | Value |
| --- | --- | --- |
| Joint stretch of a simulated body at most 2 cm | `impulse_passive` (stock mannequin, 8 solver iterations) | 0.86 cm |
| | worst with the NPC under control: `push_thigh_200_falls` | 0.16 cm |
| Mean physics step at most 1.5 ms | a hand scenario (`strike_punch_head` in one run, `effector_hit_head` in the next) | 0.45 ms |

Five scenarios have a body that is not simulated (portraits and the headset scenes) and no stretch figure:
nothing solves their joints. Two of them, `face_metahuman` and `portrait_metahuman`, used to report
3.6 cm, which is the gap between the anchors the stock MetaHuman asset is authored with; it is still
reported, as `initial_joint_offset_cm`.

Fastest joint by kind of stimulus, for the question of what the 30 rad/s limit applies to: strikes by a
hand 10.1 rad/s; calibrated blows on the NPC 13 rad/s at 30 N*s, 31 at 80 and 40 at 200 (push matrix, step
3); a passive body dropped on the floor 34 to 52 rad/s (`drop_npc`, `room_drop_npc`, `drop_passive`).

Decided by the owner on 23 November 2026: the 30 rad/s limit applies to strikes by a hand, and the other
stimuli record the figure without being judged by it; a body that is not simulated has no stretch figure,
and the portrait scenarios stay on the stock asset.

**Still for a person to check.** The list of step 4 has not been gone through yet; the owner deferred it on
23 November 2026 and M3 starts without it: keys and mouse, the direction the camera turns up and down, the feel of the hands, the drag handle, slow
motion, hot reload in a live window. New with this step: the V key and the overlays in a live session
(`Tools\play.ps1 -Overlay all`).

The suite now takes 6.3 minutes one scenario after another, against the 5 minutes NFR-6 allows for build
and suite together. The scenarios do not depend on each other: all 34 run in 91 s eight at a time, with the
same hashes. `Tools\suite.ps1` has not been made parallel, because its physics-step limit is a wall-clock
measurement.

`Tools\bootstrap.ps1` takes 9.6 minutes: the suite 6.2 minutes, the push matrix 2.8 minutes, the unit tests
(25) 15 s, the session's self-test (14 checks) 10 s. The push matrix gives the same table and figures as in
step 3 (worst joint stretch 0.31 cm, fastest joint 39.6 rad/s).

Not measured: strikes on a character that is lying or falling, two hands at once, a strike against a
character near a wall, penetration between the character's own bodies and between the character and the
floor (the fall-and-rest family, M3), anything in the headset (overlay text is not drawn there).

### M3 part 1 (senses, metrics and tools; three corrections), 2026-11-23

Same machine and settings. The part was made in two steps. Step A added what the body senses of its balance,
the measurements of energy, rest and penetration, the `capture` overlay layer, and the tools (the suite in
parallel, baselines, sweeps), and was to change nothing a run does. Step B corrected three things step A
brought out: the generated physics asset had no self-collision, the standing character slept one substep
in six, and the solver's contact order depended on thread scheduling.

**Step A changed nothing for the runs that existed.** The suite was run on the committed M2 state before
any change (the baseline `m3_start`) and on the build of step A:

| Compared | Result |
| --- | --- |
| 34 scenarios, headless | The same two hashes in all 34 |
| Push matrix, 128 runs | The same outcome, state sequence and pelvis drift in all 128 |
| `stand_npc`, `hit_chest_200_falls`: 2 headless runs and 1 rendered | Identical |
| `hit_head_30_stands`, `hit_chest_200_falls` rendered with `-Overlay all` | The hashes of the headless runs |

So the body's new sense (it now keeps the floor's contact impulses and works out its balance every tick),
the scene queries after every frame and two new tuning values leave no trace in a run.

**Running the set eight at a time.** One after another the 34 scenarios took 393 s; eight at a time they
take 95 to 100 s, with the same hashes. The physics step, which is timed by the clock, reads longer when
the runs share the processor:

| Scenario | Mean physics step alone | Sharing the processor with seven runs |
| --- | --- | --- |
| `stand_npc` | 0.40 ms | 0.58 to 0.77 ms |
| `hit_chest_200_falls` | 0.44 ms | 0.68 to 0.75 ms |
| worst of the set | 0.45 ms | 0.75 to 0.95 ms |

The limit of 1.5 ms is still far off, and a scenario that does read over a step limit in the parallel pass
is run again alone before it is judged. Figures written down in this file are from runs one at a time.

**What the new figures said about the body as it was** (before step B):

| Figure | Value | Remark |
| --- | --- | --- |
| Centre of mass inside the support polygon, standing | 9.3 cm | The overlay of M2 step 5 read 9.4 cm |
| The floor's push on both feet over body weight, standing | 0.83 | Should be 1: the first sign of the sleep below |
| Bodies of the NPC that may collide with each other | 0 pairs of 120 | The asset had no self-collision |
| Rest after the fall began, the three falls of the suite | 0.57, 2.36 and 0.91 s | Limit 3 s |
| Energy gained by a falling or lying NPC | 0.02 to 0.10 J | An 80 kg body lifted by 1 cm is 7.8 J |
| Energy gained by a passive body dropped from 1.5 m | 171 J (`drop_npc`), 74 J (`room_drop_npc`), 77 J (`drop_metahuman`) | It lands with a foot 8.4, 5.1 and 1.6 cm inside the floor and is pushed out again |

The last row is the measurement the decision about continuous collision detection on the feet will rest
on (part 5). The stock mannequin lands 4.1 cm deep and gains nothing (`drop_passive`).

**The standing character slept one substep in six.** The feet's contact reports of three successive frames
of `stand_npc`, with the solver's time for the frame:

| Frame | Reports of the left foot against the floor | Force over the frame | Solver time |
| --- | --- | --- | --- |
| 297 | 1 (2.13 N*s) | 192 N | 0.27 ms |
| 298 | 2 (2.11 and 2.12 N*s) | 381 N | 0.44 ms |
| 299 | 2 (2.13 and 2.14 N*s) | 384 N | 0.44 ms |

The pattern repeats every three frames. A body in free fall gains 0.1089 m/s per frame, which is 9.80 m/s^2,
so no simulated time is lost in general; and each report that does arrive carries the full weight of one
substep. What is missing is a whole substep: the solver has put the character's island to sleep, with no
gravity, no contact and no contact event, and the controller's next tick wakes it. With
`-CVars "p.Chaos.Solver.Sleep.Enabled=0"` there are two reports in every frame and the feet carry 0.999 of
the weight. The controller now sets its bodies to never sleep, which gives the same hashes as that switch.

**The NPC had no self-collision.** Read back from the asset at run time, all 120 pairs of its 16 bodies
were switched off. Measured over all pairs that are not jointed, whatever the asset says: after the drop of
`drop_npc` the left forearm was 16.8 cm inside the chest. In the spawn pose four such pairs overlap:

| Pair | Overlap in the spawn pose |
| --- | --- |
| Pelvis and chest (`spine_04`) | 5.5 cm |
| The two thighs | 1.4 cm |
| Lower spine (`spine_02`) and each thigh | 0.9 cm |

The template's own table was tried first (70 pairs on) and dropped: it is not the same left and right (an
upper arm collides with its hand on one side only), and it leaves the thighs colliding, which made the
standing character's feet turn at 1.26 rad/s and raised the assist's load to 0.216 of body weight. The rule
now: every pair collides except the 15 jointed ones and the four above, 101 pairs.

**Step B, first change: self-collision.** Against the results of step A, 28 scenarios kept their hashes,
among them every standing one, every strike but one, and the falls in which no limb meets the body. Six
changed:

| Scenario | What changed |
| --- | --- |
| `drop_npc`, `room_drop_npc` | Limbs now rest on the body: self-penetration 0.38 and 0.65 cm; fastest joint 34 to 53 and 40 to 42 rad/s (a passive body, not judged) |
| `push_thigh_200_falls` | Rest after the fall 2.36 to 2.74 s; a hip 4.7 to 1.5 degrees past its limit; self-penetration 0.11 cm |
| `strike_punch_forearm` | The struck forearm now meets the body: fastest body 10.1 to 10.4 rad/s |
| `tune_live_file`, `tune_live_override` | The character that sits down when its leg muscles are switched off: rest 1.30 to 1.34 s |

**Step B, second change: a body under control never sleeps.** All 23 scenarios with a controlled body that
stands at some time changed (`hang_npc_hand`, which swings from the first frames, did not); the 10 without
one did not. A standing body, before and after:

| Measure | Sleeping one substep in six | Awake | Limit |
| --- | --- | --- | --- |
| Pelvis drift, maximum | 0.95 cm | 0.97 cm | 5 cm |
| Fastest body after 2 s (a foot), 10 s run | 0.079 rad/s | 0.150 rad/s | 0.3 rad/s |
| The same, 10 minute run | 0.106 rad/s | 0.158 rad/s | 0.3 rad/s |
| Assist load, force over body weight, 10 s and 10 minutes | 0.047 and 0.051 | 0.0185 and 0.0185 | |
| Assist load, torque over weight x centre-of-mass height | 0.0017 | 0.0020 | |
| The floor's push on both feet over body weight | 0.83 | 0.999 | |
| Centre of mass and capture point inside the support polygon | 9.3 cm | 9.9 cm | |
| Solver time per frame, mean | 0.40 ms | 0.45 ms | 1.5 ms |

**The M2 assist baseline was inflated by the sleep.** The M3 criterion "standing assist share is at most half
its M2 value" was written against 0.047 (and 0.051 over ten minutes). Awake, the same body with the same
tuning reads 0.0185. Half of the M2 figure is 0.0235, which the body now meets with no work done; half of
the corrected figure is 0.0093. Which one the criterion means is the owner's decision.

**Solver iterations, measured again.** `Tools\sweep.ps1` on `stand_npc`, the body awake:

| Position iterations | Pelvis drift | Fastest body after 2 s | Assist force share | Floor's push over weight |
| --- | --- | --- | --- | --- |
| 1 | 3.75 cm | 0.60 rad/s | 0.088 | 1.11 |
| 2 | 3.59 cm | 0.47 rad/s | 0.100 | 1.08 |
| 4 | 1.57 cm | 0.32 rad/s | 0.051 | 1.04 |
| 8 (the engine's default) | 1.47 cm | 0.23 rad/s | 0.039 | 1.02 |
| 16 | 1.24 cm | 0.12 rad/s | 0.026 | 1.01 |
| 32 | 1.05 cm | 0.17 rad/s | 0.020 | 1.00 |
| 64 (the NPC's) | 0.97 cm | 0.15 rad/s | 0.0185 | 1.00 |

M2 step 1 recorded 0.54 rad/s and an assist share of 0.153 at 8 iterations against 0.07 and 0.044 at 64;
much of that gap was the sleep. The count stays at 64; 16 looks as steady and is a candidate for part 7.
`tune_live_solver` used to guard the live change of the count with a change from 64 to 8 and a lower bound
of 0.25 rad/s on the feet, which the awake body no longer reaches (0.22). It now changes to 2 and expects
0.3 (measured 0.47, the same as a run that has 2 from the start).

**The push matrix after both changes** (128 runs, 169 s): the same table of falls as in M2, run for run.
Pelvis movement of those that stood: 1.0 to 3.2 cm at 10 N*s, 1.8 to 7.9 cm at 30, 6.8 to 14.5 cm at 80.
Worst joint stretch 0.28 cm, fastest joint 39.6 rad/s.

**The three falls of the suite on the corrected body:**

| Scenario | Rest after the fall began | Energy gained | Deepest in the floor | Deepest in itself | Fall went |
| --- | --- | --- | --- | --- | --- |
| `hit_chest_200_falls` | 0.60 s | 0.12 J | 0.09 cm (the chest) | 0 | 104 cm straight back (270 degrees) |
| `push_thigh_200_falls` | 2.64 s | 0.25 J | 0.71 cm (the right hand) | 0.10 cm (right forearm and lower spine) | 125 cm to its right (177 degrees) |
| `effector_push_chest_falls` | 0.86 s | 0.00 J | 0.48 cm (the chest) | 0 | 89 cm straight back (270 degrees) |

The way a fall went is the path of the centre of mass over the floor from the moment the body was let go:
0 degrees is the character's left (+x), 90 forward, 180 its right, 270 backward. All three are now held to
3 s, 1 J and 2 cm by expectations. Two runs gain a lot and are not falls to be
judged by it: the character whose leg muscles are switched off gains 95 J when the falling state gives its
legs 100 N*m/rad back (`tune_live_override`), and the character hanging from one hand gains 18 J as it
swings (`hang_npc_hand`).

**What the balance sense shows of hits the body "stands" through** (corrected body; margins to the edge of
the support polygon, lowest value while upright):

| Scenario | Centre of mass | Capture point | Remark |
| --- | --- | --- | --- |
| `stand_npc` | 9.9 cm | 9.9 cm | |
| `hit_chest_10_stands` | 8.0 cm | 5.8 cm | |
| `strike_slap_torso`, `effector_push_chest` | 6.9 and 6.3 cm | 3.5 and 2.5 cm | |
| `strike_punch_head`, `strike_punch_leg` | 5.4 and 7.6 cm | 1.4 and 2.9 cm | |
| `strike_punch_torso` | 5.6 cm | 0.7 cm outside, for 4 frames | |
| `hit_head_30_stands` | 8.7 cm outside | 13.1 cm outside, for 21 frames | One foot off the floor for 34 frames |

The last two stand only because the assist's anchors pull the body back to where it was spawned. With the
capture point deciding (part 3), a 30 N*s blow to the head needs an answer of its own.

**One run in about 25 differed from the others.** In the suite run that was to become the end-of-part
baseline, `drop_passive` (the stock mannequin, passive) gave other hashes than in every run before and
after: its fastest body read 5.44309447322 m/s instead of 5.44309447335, and the trajectories then went
apart (rest after 4.68 s instead of 4.42 s). Nothing had changed for that scenario. Chaos gathers its
contact constraints from worker threads in the order they finish unless its determinism mode is on
(`p.Chaos.Solver.Deterministic`, off by default). The mode is now set in `Config/DefaultEngine.ini`, and
every scenario run has a check that it is on:

| Run | Result |
| --- | --- |
| Six scenarios with the mode on against the same six without it | The same hashes, the same step time |
| The whole suite with the mode on against the runs before | The same hashes in all 34 (`drop_passive` its usual ones) |
| 96 runs (six scenarios, 16 times each, 16 processes on 8 cores), mode off | Identical per scenario |
| The same 96 runs, mode on | Identical per scenario |

The fault did not show again in 96 runs without the mode, so these runs do not prove that the mode cures
it; they show that it costs nothing. The M0 statement "identical in every run" was true of the runs made
then and is a little too strong in general. A baseline comparison that names a scenario the change cannot
have touched is the signal to look for this again.

**Repeatability, on the corrected body** (the runs were made before the determinism mode was switched on;
the suite after it gave the same hashes for all 34):

| Scenario | Runs compared | Result |
| --- | --- | --- |
| `stand_npc` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `stand_npc_10min` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `hit_chest_200_falls` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `push_thigh_200_falls` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `effector_push_chest_falls` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `strike_punch_forearm` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `drop_npc` | 2 headless + 1 rendered | Identical hashes in all 3 runs |
| `tune_live_solver` | 2 headless + 1 rendered | Identical hashes in all 3 runs |

The suite one scenario after another and the suite eight at a time gave the same hashes for all 34.

**The limits for the whole set** (34 scenarios, one at a time, 357 s):

| Limit | Worst scenario | Value |
| --- | --- | --- |
| Joint stretch of a simulated body at most 2 cm | `impulse_passive` | 0.86 cm |
| Mean physics step at most 1.5 ms | `effector_push_chest_falls` | 0.49 ms |

`Tools\bootstrap.ps1` takes 5.1 minutes (9.6 before the part): the suite 100 s, the push matrix 170 s, the
unit tests (26) 16 s, asset generation 11 s, the session's self-test (14 checks) 10 s. Build and suite
together are inside the 5 minutes of NFR-6 again.

**Thresholds chosen in this part,** from what was measured, accepted by the owner on 23 November 2026 with
one change (energy gain 1 J instead of the 5 J proposed; the suite passes with it and no hash moved): a
falling or lying body may gain 1 J (measured 0.25 J at most); bodies that may collide may overlap by 0.1 cm in the
spawn pose (measured 0); a foot stands from 20 N of the floor's push (it carries about 390 N standing, and
192 N in the worst frame of the sleeping body); a standing scenario keeps its capture point 5 cm inside
the support (measured 9.9 cm).

Pictures looked at: `stand_npc` with all overlays (the support polygon under both feet, the capture point's
ring inside it on the spot under the centre of mass, 387 and 397 N on the feet); `hit_chest_200_falls` at
3.05 s (the capture point 71 cm behind the body, red); `hit_head_30_stands` at 3.12 s (one foot carrying
842 N, the other none); the passive NPC after its drop, lying with its arms beside the body and not in it.

Not measured: balance on anything but the two feet (a body on hands and knees has no support polygon
yet); penetration of convex hulls (the NPC has none); falls against walls or furniture; the cost of the
never-sleeping body while it lies still (it is solved like a standing one, 0.45 ms); whether the
determinism mode holds for a scene with props.

### M3 part 2, step 0 (collarbones, inertia of the hands), 2026-11-23

The NPC's physics asset got a body for each collarbone (0.64 kg each, from the chest's 12.77 kg; joint
ranges in `Specs/README.md`), five more pairs under `collision.disable` (neck and each collarbone 6.0 cm
into each other in the spawn pose, the two collarbones, the chest and each upper arm) and three times the
inertia on the hands. 18 bodies, 17 joints, 127 colliding pairs.

Against `m3_part1_end`: 29 scenarios changed, every one with the NPC's asset in it; the five without it
(`drop_passive`, `drop_metahuman`, `impulse_passive`, `face_metahuman`, `portrait_metahuman`) did not.
`room_portrait`, `vr_room_look` and `vr_room_perf` hold the asset without simulating it: the body hash
moved because there are two more bodies, the bone hash did not. All 35 pass (one new scenario). The push
matrix has the same 128 outcomes; its fastest joint is 38.4 rad/s (39.6 before).

| Figure | Before | After |
| --- | --- | --- |
| Standing: assist force share | 0.0185 | 0.0171 |
| Standing: pelvis drift | 0.97 cm | 0.85 cm |
| Standing: fastest body after settling | 0.150 rad/s | 0.155 rad/s |
| Standing: capture margin (minimum) | 9.9 cm | 9.8 cm |
| `hit_chest_200_falls`: rest after the fall / energy gain | 0.60 s / 0.1 J | 0.62 s / 0.2 J |
| `push_thigh_200_falls` | 2.64 s / 0.2 J | 2.57 s / 0.1 J |
| `effector_push_chest_falls` | 0.86 s / 0.0 J | 0.86 s / 0.1 J |
| `strike_punch_forearm`: fastest joint | 10.2 rad/s (`upperarm_l`) | 20.9 rad/s (`hand_l`) |
| `hang_npc_hand`: limit violation / energy gain | 0.4 deg / 18 J | 4.4 deg / 5.6 J |

The collarbones alone failed `strike_punch_forearm`: the wrist snapped at 41 rad/s in the frame of the
punch and was at 7 in the next. It was not the collarbones' doing. The same punch on the old asset:

| Height of the punch | 102.6 cm | 104.6 | 106.6 (the scenario) | 108.6 | 110.6 |
| --- | --- | --- | --- | --- | --- |
| Old asset | 10.3 | 10.3 | 10.2 | 36.8 (`hand_l`) | 24.6 |
| Collarbones | 29.4 | 35.5 | 41.1 | 46.8 | 5.0 |
| Collarbones, hands at 3x inertia | 16.1 | not run alone | 20.9 | 23.5 | not run alone |

`strike_punch_forearm_heights` strikes 102.6, 104.6, 108.6 and 110.6 cm one after another on the final
asset: 22.8 rad/s at most.

What was ruled out: the hand touching thigh or pelvis (switching those pairs off left the hashes the same),
the collarbones' mass (0.004 to 0.016 of the body: 31 to 43 rad/s), and seven solver settings (soft limits
off, velocity projection, position solved last, shock propagation, inertia ratio, parent mass ratio, CCD:
40.4 to 41.5). Inertia conditioning did work: `p.Chaos.Solver.InertiaConditioning.Distance` 0 / 20 (default)
/ 40 / 80 gave 62 / 41 / 25 / 14 rad/s; but over the suite `push_thigh_200_falls` then took 3.3 to 6.7 s to
rest at any distance from 30 to 100, and `tune_live_solver` no longer told 2 iterations from 64 by its
bound. The hands' inertia, from the engine, is 5.5 / 15.2 / 12.6 kg*cm2, close to de Leva's 5.9 / 14.4 / 9.6
for a hand of 0.49 kg; so the factor of 3 is a stability measure, and is written down as one. Factors of 2,
3 and 5 gave 36, 24 and 16 rad/s at the worst height; standing and the three falls were within noise of
each other at all of them, except that `push_thigh_200_falls` rested in 3.5 s at a factor of 2: its rest
time is itself on an edge (a lying body that rolls over late), which part 5 has to deal with.

`series.csv` has two more columns (the fastest joint of each frame and its speed), and `metrics.json` the
inertia of every body. Both are measurements only.

Missed in step 0 and found in step 1: the session's self-test aimed its drag handle at a point written down
for the old asset, and with the collarbones the arm hangs elsewhere, so the handle took nothing (12 of 14
checks). Measured on `stand_npc` with `body.bodies[].final_location_cm`: the upper arm's centre of mass is
1.8 cm lower and 1.1 cm closer to the body than before, the hand 3.4 cm lower and 3.3 cm closer; the
collarbone gives a little under the arm's weight. (The chest's centre of mass is 3 cm lower only because the
collarbones' shapes left it.) The test now aims at where the forearm is: 14 of 14.

### M3 part 2, steps 1 to 3 (animation instance, poses from text, flinch offset), 2026-11-23

Step 1, the native animation instance giving the reference pose: all 35 scenarios had the same hashes as
`m3_part2_step0`. Step 2, poses from `Config/Tuning/poses.json` with a stance of zeros: the same 35 again,
plus `pose_lean`, where the stance changes at 3 s (chest -10 and -8, neck +12, collarbones +10, elbows +30):
the forearm's target ends 30.0 degrees from the reference pose, which is the angle asked for, and the body
holds the new pose with a mean error of 1.16 degrees (1.08 standing in the reference pose; the worst single
muscle is a collarbone, 4.7 to 5.8 degrees). Pictures before and after: the chest leans forward, the head
stays level, the elbows close.

Step 3, the flinch's pose offset: 15 scenarios changed, exactly those in which a hit makes the body flinch;
the 20 others kept their hashes. The first version dropped the offset when the Flinch state ended, and two
falls gained 13.8 and 7.9 J (over the 1 J bound) in the frames after they began to fall; fading it by its
own clock instead gave 0.13, 0.00 and 0.06 J for the three falls, which rest after 0.61, 2.43 and 0.86 s.

| Figure | Step 0 | Step 3 |
| --- | --- | --- |
| `hit_head_30_stands`: fastest joint / assist / capture margin (min) | 12.3 rad/s / 0.0359 / -13.2 cm | 18.5 / 0.0365 / -13.3 |
| `hit_chest_10_stands`: fastest joint | 1.3 rad/s | 6.1 |
| `strike_punch_torso`: fastest joint | 3.7 rad/s | 9.2 |
| `effector_push_chest`: fastest joint / capture margin | 1.4 rad/s / 2.6 cm | 3.4 / 2.7 |
| `hit_chest_200_falls`: energy gain / rest | 0.2 J / 0.62 s | 0.13 J / 0.61 s |
| `push_thigh_200_falls` | 0.1 J / 2.57 s | 0.00 J / 2.43 s |

Step 4, two-bone leg placement (`pose_foot_place`: pelvis welded to the world, foot anchors of the assist
off, the left foot asked to go 12 cm up and 10 cm forward): the foot ends 1.8 cm from its point, the knee's
target is bent 61 degrees, the body holds the pose with a mean error of 1.08 degrees. Three things went wrong
on the way. With the pelvis on a ball joint instead of welded, the lifted leg tipped the body over the pin,
the world-fixed point moved against the body, the leg chased it and the body fell (35 degrees of tilt): the
test, not the placement. With the placement switched on at full weight in one step, the knee's target
jumped 61 degrees and the calf snapped at 84 rad/s; eased in over `poses.blend_s`, 4.7 rad/s. My first
guess for the fall was a knee folding backward; the knee's bending direction is now taken as the front of
the mesh rather than from the pose, which changed nothing in this test (same hashes) and is kept as the
safer rule. The other 36 scenarios kept their hashes.

The push matrix differs in one of 128 runs: 80 N*s backward on the chest no longer fells the body (it did
in step 0). Under the M2 rules the matrix still passes; under the owner's M3 table, standing without a step
at 80 N*s is a failure, which parts 3 and 4 have to deal with.

### M3 part 3 (standing balance), 2026-11-23

Same machine and settings. Four steps, each against its own baseline (`m3_part3_start`, `_step2`, `_step3`,
`_end`). The push matrix now has five bodies (the pelvis added) and names an outcome per push.

**Step 1, measurements only: all 37 scenarios and the 128 runs of the old matrix kept their hashes and
outcomes.** What the assist holds in quiet standing (`control.assist_load.by_body`), as a share of body weight:

| Body | Horizontal | Vertical |
| --- | --- | --- |
| Pelvis | 0.0135 | (no vertical spring) |
| Feet together | 0.0031 | 0.0015 |
| Chest, head | 0 | 0 |

So 79% of the 0.0171 was the pelvis standing about 0.7 cm from its anchor.

**Step 2, the capture point decides and the anchors follow the feet.** Changed exactly the scenarios in which
a foot lifts or the body falls; the 33 standing ones kept their hashes. Three things had to be corrected on
the way, each found by a run:

| Tried | What happened | Now |
| --- | --- | --- |
| Step reach compared with the capture point's margin now (30 cm) | `push_thigh_200_falls` (2000 N for 0.1 s on a thigh) was held at -29 cm by the assist with 110% of body weight, and did not fall | The margin is grown by e^(omega T) over a step (T = 0.25 s, 2.2 times) and compared with 50 cm: lost at about 23 cm outside |
| Each decision held for 0.05 s | A 200 N*s blow to a thigh put the capture point 38 cm out in its first frame; the assist (3 times body weight) brought it back in three frames, and the body "stood" | Lost is decided at once; only "a step is needed" waits (`decision_s`, 0.03 s) |
| Support = the feet carrying 20 N or more | After a 30 N*s blow to the side of the chest the far foot carried 14 to 30 N for 0.15 s, the polygon shrank to one foot, and a step "was needed" | Support for decisions = every foot the floor touches, or touched within `lift_grace_s` (0.25 s, for a foot knocked off by a blow to the leg) |

Before any strategy, the matrix with the new decision: 30 N*s 27 stood, 13 held (4 of them failures, the
thighs sideways), 200 N*s 38 fell and 2 held (diagonal blows to a thigh, before lost was made immediate).

**Step 3, ankle and hip strategy.** Sweeps on `stand_npc` and `hit_chest_10_stands`
(`Saved\Sweeps\ankle1..3`):

| Ankle gain | Slow part | Assist share, standing | Pelvis drift |
| --- | --- | --- | --- |
| 0 | none | 0.0171 | 0.85 cm |
| 1 | none | 0.0147 | 0.66 cm |
| 2 | none | 0.0131 | 0.55 cm |
| 0 | on the capture point, 2 s | 0.0111 | 0.80 cm |
| 1 | on the pelvis's offset from its anchor, 0.5 s | 0.0050 | 0.59 cm |
| 2 | the same | 0.0052 | 0.50 cm |

The slow part on the capture point left 0.007 on the pelvis spring: under its weight the posture sags so that
the pelvis stands 0.35 cm off its anchor while the centre of mass is where it was. On the pelvis's own offset
it takes that up. Chosen: gain 1, slow part 0.5 s.

Sideways the ankles made things worse. A 30 N*s push to the side of the chest: capture margin -5.1 cm with no
strategy, -9.3 with ankle gain 1, -9.9 with 3; turned the other way, -10.4. Leaning the legs as a parallelogram
(ankles and hips turned together) lifted the unloaded foot; the ankles alone did the same. `ankle_side_gain`
is 0. The hip strategy (trunk toward the capture point while it is outside the support) at 3 degrees per cm
changed no outcome of the 30 N*s row and lowered the assist's peak on a backward push to the chest from 0.238
to 0.179 of body weight; it is on at 3, forward and back only.

**Step 4, a held push.** Changed the two scenarios in which a hand presses longer than a blow
(`effector_push_chest`, `effector_push_chest_falls`, which now flinches once instead of twice); the strikes
kept their hashes, and the push matrix kept all 160 outcomes. Two more corrections:

| Tried | What happened | Now |
| --- | --- | --- |
| The lean moved the strategies' reference and the anchors only | Under 40 N the assist carried 4.7% of body weight: the push. The ankle muscles held the upright pose against the lean | The ankle targets tilt by the lean's angle (lean / centre-of-mass height) |
| The legs' turns weighted by each leg's share of the load | With the larger turns of a lean, the body rocked from foot to foot (each foot at 0 N in turn); assist up to 0.43 | Both legs turn alike |
| The held-push window equal to the hit's pressure time (0.1 s) | A 2000 N shove of 0.1 s counted as held | `held_push_s` 0.3 s |

| Scenario | Flinches | Lean | Step needed | Assist, mean / max |
| --- | --- | --- | --- | --- |
| `push_hold_chest`: 40 N backward on the chest for 2 s | 0 (the onset is gentle) | 6.2 cm (6.1 by F h / m g) | no | 0.017 / 0.143; 0.006 to 0.008 during the hold |
| `effector_hold_chest`: a hand pressed against the chest for 3 s | 1, as it lands | 10 cm (the limit) | 1.8 s of it (117 N: more than a lean can take) | 0.032 / 0.193 |

A hand held at a fixed point presses harder the more the body leans into it (the chest comes toward the hand):
moving the hand's target 4 cm further out only lowered its force from 138 to 104 N. The clean test of a lean
without a step is the scripted force.

**The body at the end of the part, against the start:**

| Figure | Start | End | Bound |
| --- | --- | --- | --- |
| Assist force share, `stand_npc` (10 s) | 0.0171 | 0.0052 | 0.0093 |
| The same, `stand_npc_10min` | 0.0171 | 0.0041 | 0.0093 |
| Pelvis drift, standing | 0.85 cm | 0.59 cm | 5 cm |
| Fastest body after settling, standing | 0.155 rad/s | 0.162 rad/s | 0.3 rad/s |
| Capture margin, standing (minimum) | 9.8 cm | 9.0 cm | 5 cm |
| `hit_head_30_stands`: assist mean, capture margin min | 0.0365, -13.3 cm | 0.0270, -14.5 cm (step needed 9 frames) | |
| `hit_chest_200_falls`: rest after the fall, energy | 0.61 s, 0.13 J | 0.79 s, 0.23 J | 3 s, 1 J |
| `effector_push_chest_falls` | 0.86 s, 0.06 J | 0.92 s, 0.00 J | 3 s, 1 J |
| `push_thigh_200_falls` | 2.43 s, 0.00 J | 3.69 s, 3.22 J | 4 s, 4 J until part 5 |

`push_thigh_200_falls`: the fall is decided 0.03 s earlier, so the rest is counted from earlier; and 2 s into
the fall the rolling body puts a foot 0.99 cm into the floor, and being pushed out gives it 3 J in one frame.
Its trajectory is chaotic: it gained 0.005, 0.008 and 3.2 J in three successive builds that changed nothing
of the fall itself. Its limits were loosened on the owner's decision until part 5.

**Push matrix at the end** (160 runs, 212 s):

| Impulse | Stood | Held | Stepped | Fell | Verdict |
| --- | --- | --- | --- | --- | --- |
| 10 N*s | 40 | 0 | 0 | 0 | judged: pass |
| 30 N*s | 37 | 3 (head at 0, 180, 270 degrees) | 0 | 0 | judged: pass, 3 pending part 4 |
| 80 N*s | 12 | 24 | 0 | 4 (head at 0, 180, 270; chest at 270) | reported only: 12 fail (pelvis at 45 and 135, thighs at most angles), 24 pending |
| 200 N*s | 0 | 0 | 0 | 40 | judged: pass |

All 44 falls were decided by the capture point. After a 30 N*s push the assist's commanded force peaks at 0.26
of body weight (median of the row; 0.53 at most), against 0.009 in quiet standing.

Repeatability: `push_hold_chest`, `effector_hold_chest` and `hit_head_30_stands` rendered with all overlays
gave the hashes of their headless runs. `Tools\bootstrap.ps1`: all 8 steps pass (40 scenarios, 27 unit
tests, the matrix, the session's self-test 14 of 14).

Pictures looked at: `push_hold_chest` at 4.8 s (leaning 6.2 cm into 40 N, ankles turned 4.7 degrees, assist 0.8%, the feet
at 389 and 396 N), `effector_hold_chest`
at 5.0 s (the trunk bent toward the hand, 117 N, "needs a step"), `hit_head_30_stands` 0.15 s after the blow.

### M3 part 4 (stagger and steps) and the audit of the body's movement, 2026-11-24

**Runs share engine processes.** A run was 14 s of engine start-up for 1 s of physics. A job queue
(`-ScenarioQueue`, `UScenarioBatchSubsystem`) now lets one process run many scenarios, each in a fresh world.
The first scenario a process runs differs bit for bit from the same scenario run later in it (35 of 48 suite
scenarios had other hashes than in separate processes), and later runs do not depend on what ran before them
(three different chains gave the same hashes). So every process runs `Tools\batch_warmup.json` first, and a
single run is a queue of one job: with that, 48 of 48 scenarios have the same hashes run alone, eight processes
at a time and twelve. Suite 110 s to 25 s (80 s with `-Long`), push matrix 213 s to 47 s.

**Part 4, figures before the audit** (baseline `m3_part4_s4_end`): suite 47 of 48 (`effector_push_chest_falls`
stood, backing away from the hand), push matrix 159 of 160, 80 N*s row 38 stepped and 2 fell, steps per push
median 1 and at most 4, no joint over 30 rad/s outside the 200 N*s row, energy gain at most 1.2 J, quiet-standing
assist share 0.0051.

**The audit** looked at the motion itself, on sheets of pictures over time (`Tools\film.ps1`), since every
check passed while the body stood with its arms out, fell like a plank and stayed in a lunge. Measured on the way:

| What | Before | After |
| --- | --- | --- |
| 80 N*s on the chest, backward | fell at 3.03 s without a step ("61 cm by the time a step lands: beyond a step") | two steps, stands |
| 120 N*s on the chest, backward | fell | three steps, stands |
| 200 N*s on the chest, backward | plank, lies straight | one stumble, sits down and curls, lands on its back |
| 200 N*s row of the matrix | 40 fell | 32 fell, 8 stepped (pelvis and thighs, 3 to 15 steps) |
| Rest after a fall (back, forward, side) | 2.6 s (back) | 1.7, 2.6, 1.6 s; 3.5, 3.9, 3.3 s while "lying" still waited for a motionless body |
| Energy gain in a fall | 0 to 1.2 J | 28, 21, 11 J: the work of folding the legs and putting the arms out |
| Collarbone under a soft arm (40 N*m/rad) | | sagged 10.2 degrees in the arms' group; pose error mean 2.0 to 1.0 degrees once in the trunk's |
| Quiet-standing assist share (10 s, 10 min) | 0.0051, 0.0041 | 0.0048, 0.0038 |
| A punch on the forearm (8.5 m/s) | moved it 10 cm (arm held out) | 1.8 cm: the arm hangs at the hip and is pressed against it |

After the audit (baseline `m3_audit_end`): suite 50 of 50 with `-Long`, push matrix 159 of 160 (the same cell,
`push_neck_01_090_030`, two steps where one is allowed), unit tests 28, session self-test 14 of 14. Against
`m3_part4_s4_end` 36 scenarios have other hashes: every one with an actively controlled body, as the stance, the
arms and the neck changed for all of them; the 10 with a passive body or none are bit for bit as they were.
Renamed: `effector_push_chest_falls` to `effector_push_chest_steps`, `push_thigh_200_falls` to
`push_thigh_200_stumbles`; new: `hit_chest_200_forward_falls`, `hit_chest_200_side_falls`.

Seen and not solved in the audit itself (solved the same day, below): `hang_npc_hand` (a body hanging by one
hand) took the pose of a fall backward and swung for the whole run, 119 J of muscle work where there were 5.5:
the fall's pose asked only whether the posture gave way, not whether the floor was coming. `hit_head_30_stands` sits on an edge: the same blow needs a
step or not (capture margin -8.7 or -4.9 cm) with the arms 3 degrees further in or out; its check is now "stands,
one step at most".

### The regrouping step, 2026-11-24

After recovery steps the body stood wherever they had left its feet: 25 to 60 cm from its stance, in a lunge.
A step that brings a foot back beside the other was written in the audit and left off, because out of a lunge
it ended in another loss of balance. What stood in its way, each measured:

| What | Seen | Now |
| --- | --- | --- |
| The ankle strategy holds the capture point over the middle of the feet | of a weight shift of 19 cm forward the pelvis went 5 | a shift moves the strategy's reference with it |
| The foot left behind lowers the pelvis so that its leg can reach | pelvis 15 cm down although that foot carried 0 N; centre of mass over the stance heel, 17 cm short, for 2 s | a foot about to lift lowers the pelvis by the share of the weight it carries: the body rises on the stance leg |
| The foot lifted once the centre of mass stood inside the stance sole | 0.5 to 1.2 s of shift with the pelvis's goal up to three times too far; the body went on past the foot, or the legs folded (pelvis 35 cm down, "falling") | lifts when the capture point at landing is 5 cm inside the support to come; the goal is led by the distance the capture point still has to go, 25 cm at most |
| What was left of the shift after landing | the anchors glide from where the body is, so the shift counted twice: a 54 cm step to the side to catch it | dropped as the foot lands |
| A foot on its toes has a sole polygon of its toes only | target sole and stance middle 12 cm too far forward | the whole sole is taken for both |

With it (`balance.regroup_wait_s` 0.6): weight shift 0.15 to 0.2 s out of a short stance, 0.7 to 1.3 s out of a
lunge of 50 to 60 cm; the feet end 0.7 to 5.0 cm from their stance (`control.stance_error_cm`, checked by
`max_stance_error_cm` 8 in the five scenarios that step). In the push matrix 48 of 160 pushes end with a
regrouping step: none gives up, none falls, one (`push_thigh_l_045_200`) needs one more step for its balance.
The matrix's outcomes are as before (159 of 160, the same cell); it does not count regrouping steps as steps the
push cost, and neither does `max_recovery_steps`.

Suite 50 of 50 with `-Long`, unit tests 28, session self-test 14 of 14, assist share 0.0048 and 0.0038. Against
`m3_audit_end` 11 scenarios have other hashes: the five that make recovery steps (they now regroup), and the six
asked steps (`step_*`), whose weight shift now moves the ankle strategy's reference (landing errors, lift times
and joint speeds as they were to the digit shown: 1.1, 1.6, 2.0 cm; 3.41 s). The other 39 are bit for bit as they
were. New baseline: `m3_regroup`.

### A body held up does not fall, 2026-11-24

`hang_npc_hand` (pinned by the right hand, dropped 60 cm onto that arm) took the pose of a fall backward, knees
to the chest, and kept it for the whole run: 119 J of muscle work. A body that is not upright, has touched
nothing of the world for 0.4 s and whose centre of mass is not 5 cm below the lowest it has been is now held up
(`thresholds.held_*`): it lets go of the fall's pose and hangs straight. Held up at 0.74 s, 34 J (the curl of the
first 0.4 s, before the body can know), checked by `max_energy_gain_j` 60. It still swings for the whole run:
that is the drop onto one arm, not the muscles. In the 160 runs of the push matrix and the other 49 scenarios
nothing is ever taken for held up: their hashes are those of `m3_regroup`.

### The last cell of the push matrix: a body on its toes, 2026-11-24

`push_neck_01_090_030` (30 N*s on the back of the head) made two steps where the table allows one, since part 4.
On the pictures the body bows its head, rises on its toes, and then steps *back* twice, away from the way it was
pushed. Rising on the toes is the ankle strategy doing its work (the centre of pressure goes forward to the
capture point); with the heels 1.5 cm off the floor the support polygon was the toes alone, and the capture
point, under the raised heels, was "4.7 cm outside the support" behind it. A decision now counts the whole sole
of every foot that touches the floor. The cell stands without a step, and the matrix passes for the first time:
160 of 160 (10 N*s: 40 stood; 30: 36 stood, 4 one step; 80: 2 stood, 38 stepped; 200: 32 fell, 8 stepped), 42
regrouping steps, none given up, none in a fall.

Against `m3_regroup` 11 more scenarios have other hashes: those in which a foot is tilted at some time while the
capture point is near the edge of what touches (`effector_hold_chest`, `effector_push_chest_steps`,
`push_hold_chest`, `push_thigh_200_stumbles`, `stand_foot_moved`, `step_forward` and its mirror,
`strike_punch_head`, `strike_punch_torso`, `tune_live_file`, `tune_live_override`); all pass. The other 38
(quiet standing, the light hits, the falls, the passive bodies) are bit for bit as they were. New baseline:
`m3_audit2_end`.

One suite run in the course of this gave `drop_npc` other hashes (1200d5db instead of c8bde50f); alone and in
three later suite runs it gave the baseline's. Not explained; the determinism mode was on. If a baseline
comparison names a scenario a change cannot have touched, run it again, and count how often this happens.
One push-matrix run gave no result for one of its 160 runs (`push_spine_04_000_080`, no output folder at all);
the same matrix run again was complete. Both happened in the first queue run after a build, which is the one
thing they share; not looked into further. (Both were found and fixed later the same day: "Runs that did not
repeat, and jobs nobody ran" below. The build had nothing to do with either.)

### The idle and the arms, 2026-11-24

Two things of the audit's list of what still looked unnatural: a standing body was a statue (0.3 to 0.5 cm of
travel per body in ten seconds, the solver's own noise), and its arms hung while it fought for its balance.

**The idle** is the motion of the engine's `MM_Idle` (a loop of 7.57 s, 228 keys), as planned for part 7. The
clip was measured first (a script in the editor): spine 0.5 to 0.9 degrees per bone, neck and head 2,
collarbones 3 to 5, upper arms 2, the pelvis travels 0.8 cm, the head 2.5, the hands 2 to 6; the feet stand.
The bones of the mannequin and of the MetaHuman body have the same axes (their reference poses agree to about
0.01 in every quaternion component, hands aside), so what a bone does in the clip relative to the clip's first
frame is added to the same bone of the body as it is, with no retargeting; the clip's own pose (one foot
forward) and its legs are not used. The pelvis's travel leans the whole body about its ankles, by the way a
lean into a held push goes.

| `stand_npc`, after 2 s of settling | without | with the idle |
| --- | --- | --- |
| Travel of the pelvis, cm | 0.3 | 1.3 |
| Travel of the chest, cm | 0.4 | 2.1 |
| Travel of the head, cm | 0.5 | 4.1 |
| Travel of the hands, cm | 0.3 and 0.3 | 6.6 and 4.1 |
| Travel of a foot, cm | 0.0 | 0.0 |
| Fastest body, m/s | 0.008 | 0.083 (a hand) |
| Fastest turning body, rad/s | 0.146 | 0.193 |
| Assist share, 10 s | 0.0048 | 0.0067 |
| Assist share, 10 min | 0.0038 | 0.0066 |

The assist's share rises by the sway (0.0052 with the turns alone): its anchors move and the body follows them.
It stays under the bound of 0.0093. With both weights 0 the six scenarios tried gave the baseline's hashes bit
for bit.

**The arms** take one of three poses on top of the stance (`poses.balance_*`), in proportion to how far the
capture point is from where the body stands (nothing under 4 cm, in full at 30, with a lag of 0.12 s). In
`stand_npc` they do nothing: its hashes are the same with and without them. In the films a push backward brings
the arms forward with bent elbows by 0.2 s and they are down again when the feet are back together; a push
sideways spreads them, the far one higher; an asked step leaves them down; a body knocked over backward
throws them forward as it goes.

| Push matrix | before | with the idle | with the idle and the arms |
| --- | --- | --- | --- |
| Verdict | 160 of 160 | 160 of 160 | 160 of 160 |
| Steps at 80 N*s (40 pushes), sum and most | 87, 7 | 88, 9 | 81, 6 |
| Stepped and fell at 200 N*s | 8 and 32 | 10 and 30 | 8 and 32 |
| Steps of those that stood at 200 N*s, sum and most | 62, 11 | 84, 13 | 59, 13 |
| Mirror pairs that end differently | 2 | 3 | 3 |

Thirteen pushes ended differently with the idle than before (all among the outcomes the table allows): the body
is at another phase of its sway when the push comes, and the cells near an edge go either way.

Against `m3_audit2_end` all 40 scenarios with a controlled body have other hashes, as they must with a body
that moves from its first second; the 10 without one are unchanged. Three limits were moved, each for the
reason given in `CLAUDE.md`: `max_settled_speed_mps` of the two standing scenarios from 0.05 to 0.15, and
`min_pose_offset_deg` of `pose_lean` from 29.9 to 27. New check `min_settled_head_travel_cm` (2 cm) in the two
standing scenarios; the suite passes 50 of 50 with `-Long`. Baseline `m3_audit3_end`.

Seen and left: of the 8 pushes of 200 N*s the body stands through, 7 take more than four steps (up to 13), as
before; a person would have fallen or stopped sooner.

(The figures of this section were taken before the solver was kept in step, below. With it the matrix is 160 of
160 with 81 steps at 80 N*s, 6 at most, and 9 stepped and 31 fell at 200 N*s; the standing figures are the same.)

### Runs that did not repeat, and jobs nobody ran, 2026-11-24

Checking the baseline of the section above, the suite named `tune_live_file` as changed in two runs of five, with
no change made. Two defects of the tooling were behind what had been written off as one-off flakes.

**A body that falls came out differently on a busy machine.** Measured by running one scenario 160 times with
16 engine processes side by side (in one process, one run after another, 47 of 47 were the same):

| Scenario | Runs of 160 that differed, before | after |
| --- | --- | --- |
| `drop_npc` | 29 (six other hashes) | 0 |
| `tune_live_file` | 5 to 15 (three other hashes) | 0 |
| `hit_chest_200_falls` | 4 | 0 |
| `tune_live_override` | 3 | not run |
| `drop_passive` | not run | 0 |

The other hashes came back again and again, so this was no noise in the last digit but a choice between a few
ways for a run to go. Each run that differed was the same as the usual one, to the printed digit, until a body
met the floor or slid off another (5.41, 5.49 or 6.87 s in `tune_live_file`), and from there a contact came a
frame earlier or later. Ruled out by measurement: the scenario run before it in the same process (47 of 47 the
same), the millisecond budget of the tree's build (`p.aabbtree.UseTimeSliceMillisecondBudget=0`: 14 of 320
differed), the threads of collision detection (13 of 319), all of the physics on one thread (17 of 320). What it
was: Chaos rebuilds the structure it finds colliding pairs with in a background task, and every step takes the
new one in if the task is done by then and carries on with the old one if not
(`FPBDRigidsEvolutionBase::ComputeIntermediateSpatialAcceleration`). The boxes of the moving bodies in it are
padded from when each was last put in, so the two structures do not give the same pairs, and a pair found a
step later is a contact a step later. The solver's determinism mode sorts constraints; it does not touch this.
Waiting for the task before each step alone made it worse (a third hash in 28 runs of 160: the task started by
the wait is itself done or not when the step looks). `FScenarioStage::KeepSolverInStep` waits for the rebuild
before every substep, twice, and lets none run during the step: 640 runs of four scenarios, no two different.
It costs nothing that shows (mean physics step of `stand_npc` 0.742 ms before, 0.739 after). It changes every
run's hashes, the passive bodies' too, since every step now uses a freshly built structure; the suite passes
50 of 50 and the push matrix 160 of 160 with it, two suite runs and a matrix run repeat the baseline in full.
Baseline `m3_audit3_end` was taken again with it.

**A job of the queue was sometimes run by nobody.** One job in about a thousand ended without a result when 16
processes shared a queue (twice in 1900 runs; once in the push matrix the day before, with 8). A process took a
job by moving its file, on the understanding that only one move can succeed. On Windows a move opens the file
and renames it through the handle; two processes can each open it before either renames, and then both renames
succeed, the second taking the file from under the first. Each then failed to read "its" file (gone, or still
held by the other), logged an error nobody read, and went on to the next job. The logs showed it once the queue
kept them: `Cannot read file '...taken/00099.json.10740'` in one process and the same job under another number
in a second. A job is now taken by writing a marker while a lock file is held open for writing, which one
process at a time can do (`UScenarioBatchSubsystem::TakeJob`): 1600 jobs with 16 processes, none lost. And a job
that ends without a result is no longer silent: `Invoke-ScenarioQueue` prints `QUEUE WARNING` and keeps the
processes' logs.

### M3 part 5 (the fall), 2026-11-24

What the audit had left of the part: knockdowns from 8 directions, the muscles' torque caps, the softness of
the joint limits, and continuous collision detection decided by measurement. `Tools\knockdowns.ps1` knocks the
standing body down with a blow of 200 N*s on the chest from each of 8 directions and judges every fall: it
falls, rests within 3 s of starting to, gains no more than 40 J (its muscles' own work), goes no deeper than
2 cm into the floor or into itself, and no joint stretches by 2 cm.

| 8 knockdowns, chest, 200 N*s | before the part | after it |
| --- | --- | --- |
| Pass | 8 | 8 |
| Rest after the fall began, s | 1.26 to 2.78 | 1.20 to 1.94 |
| Energy gained, J | 5.0 to 25.7 | 5.4 to 25.9 |
| Deepest in the floor, cm | 0.65 | 1.22 |
| Deepest in itself, cm | 0.28 | 0.42 |
| A joint past its range, degrees | up to 2.5 | up to 3.4 |

With the head as well and 300 N*s (32 falls) 29 passed before and 31 after; the one left is a head knocked
back with 300 N*s that rests after 3.89 s. Those blows are beyond anything a hand gives (200 N*s on the head
turns the body over in the air) and are run to see what breaks, not judged.

**A figure that was wrong since M2.** `max_joint_limit_violation_deg` compared the engine's
`GetCurrentSwing1`, `GetCurrentSwing2` and `GetCurrentTwist` with the limits. Those are not the angles the
solver limits (each is the twist of the whole turn about one axis): a limp wrist read 40 degrees outside its
range, a relaxed hip 23, and making every limit hard changed none of it, which is what gave it away. Reading
the engine then showed a second error of the project's own: the solver does not limit the two swings together
as an ellipse but each by itself (`FPBDJointCachedSolver::InitPyramidSwingConstraint`; the linear solver is
the default for every joint). Measured as the solver limits a joint (`FSandboxPoseRig::LimitExcessDeg`), the
same runs read:

| Furthest a joint got past its range, degrees | old figure | as the solver measures |
| --- | --- | --- |
| `drop_npc` (a passive body dropped) | 40.6 | 4.2 |
| 16 knockdowns | 13.6 | 2.5 |
| `push_thigh_200_stumbles` (steps) | 8.5 | 5.8 |
| `stand_npc` | 0.0 | 0.0 |

**Joint limits.** Every limit of the generated asset was the stock asset's soft one (stiffness 50, damping 5),
copied from the template in silence. It is now written in the spec per joint (`"limit"`). With the spec saying
what the template had, the suite's 49 scenarios came out bit for bit as before. All hard instead: joints no
better held (1.5 degrees against 2.5 in the knockdowns, 5.2 against 4.6 at the ankle in a step), and the falls
worse: 46 J gained in one against at most 26, rest after 4.0 s against 3.6, `hit_chest_200_falls` failing. The
soft limits stay.

**After the impact the muscles let go.** Seen on the sheets of pictures, not in a figure: a body knocked over
by the head went over in the air, landed on its head and shoulders and stood there, legs up, from 3.5 to 4.5 s,
held in the shape of its fall's pose; a body on its back kept its forearms in the air. The fall's pose was kept,
with the falling state's muscles, until the body was at rest. Now the muscles go over to the lying state's
`thresholds.impact_let_go_s` (0.5 s) after the trunk or the head first meets the floor. The body on its head
folds by 4.1 s and lies sprawled at 5.0; the one on its back lies flat with its arms on the floor by 5.5 s.
The value was chosen by the pictures. By the rest time it could not be: of the 32 falls, 0 to 4 take more than
3 s at every value tried from 0.15 to 1.0 s, a different few each time (0.45 and 0.5: none; 0.4 and 0.55: two
and three). What takes the time is a limb or the whole body rolling over slowly from where it came to lie, at
0.2 to 0.35 rad/s as a whole against the runner's 0.2: a body balanced on its side. Softer or more damped lying
muscles (legs' damping 0.3 to 2 s, all stiffness near 0) moved which falls were slow, not how many.

**Torque caps.** `muscles.*.max_torque_nm` were 0, no cap. They are now 250 N*m for the legs, 300 for the
spine, 80 for the arms and 50 for the neck, from published peak torques of an adult man (knee and hip
extension, trunk extension, shoulder and elbow, neck extension), one figure a group. Push matrix with them:
160 of 160; at 80 N*s 36 stepped, 2 stood and 2 fell (37, 3 and 0 without), 72 steps in all against 81; at
200 N*s 12 stepped and 28 fell (9 and 31). `effector_hold_chest` now flinches twice: the body steps back from
the hand, the hand meets it again on the collarbone with 5.3 N*s against a threshold of 5. A second touch is a
second flinch; its limit went from 1 to 2 (the defect it guards against was a flinch every 0.35 s).

**Continuous collision detection** stays off for the body. The owner's rule switches it on where a knockdown
sinks more than 2 cm into the floor; in 32 knockdowns the deepest was 1.5 cm. A passive body dropped from
1.5 m (`drop_npc`) does sink 8.3 cm and is thrown back out with 149 J, and that is not what the sweep cures:

| `drop_npc` | Deepest in the floor, cm | Energy gained, J |
| --- | --- | --- |
| As it is (8 solver iterations, no sweep) | 8.31 | 149 |
| Feet swept | 8.31 | 144 |
| Feet and head swept | 8.31 | 144 |
| 32 iterations | 3.18 | 13 |
| 64 iterations (what a body under control has) | 1.49 | 119 |

The spec can switch the sweep on per body (`"ccd"`); no body has it.

Against `m3_audit3_end`, 12 scenarios have other hashes, the ones in which a muscle asks for more than its cap
or a body falls: the three `hit_chest_200_*`, `tune_live_file`, `tune_live_override`, `hang_npc_hand`, the
four with recovery steps, `effector_hold_chest` and `pose_foot_place`. The other 38 are bit for bit as they
were. Suite 50 of 50 with `-Long`, push matrix 160 of 160, knockdowns 8 of 8, unit tests 28, session self-test
14 of 14; a second suite run and a second matrix run repeat the first in full. Baseline `m3_part5`.

**The look at Epic's Game Animation Sample** (downloaded by the owner; read by a script in the editor on the
sample's own project, 24 s, nothing rendered). Its ragdoll is a kinematic character that goes limp when hit and
is played back up by a montage; the logic is Blueprint and has no balance in it. Its clips, 34 sequences under
`Characters/UEFN_Mannequin/Animations/Ragdoll` on a skeleton with the mannequin's bone names:

| Clips | What they are | Length |
| --- | --- | --- |
| `M_ragdoll_getup_stand_B`, `_F`, `_L`, `_R` | From lying on the back, the front, a side to standing, in place (pelvis from 12 to 16 cm up to 92 cm, no travel) | 5.0 s |
| `M_ragdoll_getup_crouch_*`, `_walk_*`, `_run_*` | The same into a crouch, a walk or a run; they travel 5 to 8.7 m | 3.0 to 4.5 s |
| `M_ragdoll_getup_roll_L`, `_R` | Up out of a roll | 3.7 s |
| `M_ragdoll_poses_OnGround`, `_rolling`, `_injured` | Poses for a body on the floor, by the way it lies; seven injured states | a few frames each |
| `M_ragdoll_flail_armsFirst`, `_legsFirst`, `_armsDown` | Loops for a body in the air, chosen by the way it flies | 4.7 to 8.3 s |
| `M_ragdoll_reach_*` (nine) | Reaching for the coming impact, aimed by a blend space over direction and pitch | 2.0 s |

How its poses look on this body next to the three fall poses written here is in "M3 part 6, step 1", below.

### M3 part 6, step 1 (a recorded clip as the source of the target pose), 2026-11-24

The get-up takes the four clips of Epic's Game Animation Sample that end standing in place
(`M_ragdoll_getup_stand_B`, `_F`, `_L`, `_R`; owner's decision). They are made for the sample's mannequin, whose
bones have the names of the body's and a different reference pose. This step makes a clip a source of the
muscles' targets and shows the clips on the body; the body does not get up yet.

**How far the two reference poses are apart**, measured when a clip is read (`control.clips` of a run): the angle
between a bone's orientation in the reference pose of the clip's skeleton and of the body's mesh, in the space of
the whole body, and, for the segments of arms and legs, the angle between the ways the two point.

| Joint | Orientations apart, degrees | Directions apart, degrees |
| --- | --- | --- |
| `spine_02`, `spine_04` | 10.3, 7.6 | |
| `neck_01` (the head's bone) | 0.5 | |
| collarbone | 4.0 | |
| upper arm | 12.3 | 11.6 |
| forearm | 18.4 | 12.8 |
| hand | 21.4 | |
| thigh | 9.3 | 4.0 |
| calf | 8.8 | 2.9 |
| foot | 3.5 | |

So the difference is of both kinds: the arms are held 12 degrees differently (a copy of what each bone turns from
its reference pose would leave the hands 12 to 15 cm from where the clip has them), and thigh and calf agree in
direction to 3 or 4 degrees while their axes lie 9 apart (a copy of the rotations themselves would twist them).
`FSandboxClipPose` turns each body of the physics asset, in the space of the whole body, by what the clip's bone
has turned from its own reference pose, and first turns the segments of arms and legs to point as the clip's
skeleton holds them. Trunk, neck, collarbones, hands and feet keep their own reference: the body's own posture,
and feet that are flat when the clip's are. What comes out is one rotation for each of the 17 joints' bones, as a
pose written in joint angles gives; the bones between two bodies stay as they are, and the body of the head takes
the orientation of the head's own bone, so that the turn of the whole neck is in the one joint there is.

**What the clips ask of the joints**, the furthest beyond its range over the four get-ups (angles as the solver
limits them: twist, swing1, swing2, against the half-ranges):

| Joint | Beyond its range, degrees | Where |
| --- | --- | --- |
| hand | 73 | `_R` at 0.90 s: -60, 2, -98 against 70, 60, 25 |
| foot | 48 | `_F` at 1.83 s: -10, -73, 55 against 15, 25, 10 |
| upper arm | 43 | `_L` at 0.00 s: twist 123 against 80 |
| thigh | 29 | `_R` at 0.97 s: twist 74 against 45, swing2 -52 against 30 |
| forearm | 14 | twist 14 against a locked axis |
| `spine_02` | 14 | `_L` at 1.70 s: swing1 -31 against 18 |
| collarbone | 11 | swing2 33 against 22 |
| calf, `spine_04`, neck | 3, 2, 0 | |

Asked for as recorded, the muscles pushed the joints into their limits: mean pose error 7 to 12 degrees over a
film, a foot 3.7 degrees past its range while a pose was held. Every target is now held to its joint's range
(`FSandboxPoseRig::ClampToRange`, the box the solver limits by): mean error 2.9, 0.6 degrees past a limit. Two of
the rows are the body's model and not the clip. The wrist: the forearm's turn (palm up, palm down) is in the
wrist's twist, and the two swings are limited in the forearm's frame, so with the palm turned down the hand's
bending back is the "sideways" swing and ends at 25 degrees; a body that pushes itself up on its hands will meet
this in step 2. The foot: there is no toe joint, a foot "on its toes" in the clip is a rigid foot asked to stand
on its tip.

**What the pictures show** (`Tools\clip-film.ps1`; the body hangs by its welded pelvis, the weld carried as the
clip has the pelvis, 60 cm above the floor; `Saved\Films\clip_B_carried` and `_F`, `_L`, `_R`). All four read as
a person getting up, on this body: from the back a roll onto the side with the knees drawn up, one arm down, a
crouch, up; from the front a push-up onto hands and a knee; from a side onto hands and knees. The body stands by
2.2 to 2.5 s of the 5 s; the rest of each clip is standing still. The clip has the pelvis 11 cm above the floor
and 34 cm behind its root at the start (from the back) and 89 cm up over the root at the end. Played at its own
speed on the carried body, the muscles follow: mean pose error 3.8 degrees, no joint faster than 16 rad/s, none
more than 0.6 degrees past its range. With the weld at the clip's own height the body is pressed up to 10 cm
into the floor and its joints stretch by 5.8 cm: limbs in the way of a pelvis that gives way to nothing. That
is the rig, and says only that the get-up cannot be a pelvis dragged along the clip's path.

**The sample's other poses next to ours** (`Saved\Films\poses_fall_ours`, `poses_reach`, `poses_flail`,
`poses_onground`). Its nine reaching poses and three flailing loops are for a body thrown through the air: the
arms reach where the impact comes from, the legs trail, each pose is asymmetric as a recording is. The three
written here (`poses.fall_*`) are for a body going over from standing: backward it sits down and tucks its chin,
forward the hands come up in front of the face, sideways one arm goes out. Theirs are not better at that, and
they would need a blend aimed by the direction of the fall to be used at all; the fall keeps its own poses. The
sample's four poses for lying (on the back with the knees up, on either side curled, face down) are the first
frames of its get-ups; ours lies as it fell, with its muscles let go.

**A mistake found by a picture.** The first sheet of a carried body showed it standing at the end with its head
and trunk hanging forward. The controller had taken the body for fallen as the weld laid the pelvis down (tilted
by 35.6 degrees), and from then on the film showed the Lying state's muscles, a tenth as strong. Nothing failed,
since a film has no expectations. A body whose pose is asked for from outside no longer judges its balance.

New: `FSandboxClipPose`, a layer in `USandboxAnimInstance`, `USandboxBodyController::SetClipPose`, the scenario
event `clip_pose` and `follow_clip` on a pin, `Tools\clip-film.ps1`, scenario `clip_pose_hold` (the welded body
holds the clip's standing pose and then its crouch at 1.4 s: a calf's target 120 degrees from the reference pose,
mean pose error 2.4 degrees, nothing past a limit, no energy gained), three unit tests (31 in all). Against
`m3_part6_start` all 50 scenarios have the same hashes and one is new. Suite 51 of 51 with `-Long`,
push matrix 160 of 160, knockdowns 8 of 8 (as in part 5: 1.94 s, 25.9 J, 1.22 cm), session self-test 14 of 14.

### M3 part 6, step 2 (the GetUp state), 2026-11-24

A body that has lain still for 1.5 s gets up by one of the four clips, under full simulation. Seven scenarios:
a knockdown of 200 N*s on the chest from each of four directions and the get-up after it (`getup_back`, `_front`,
`_left`, `_right`), and three with something done to the body while it is on its hands and knees (`getup_push_weak`,
`getup_hit_strong`, `getup_held`).

| Scenario | Attempts | Stood after, s | Clip waited, s | Assist force share, mean and most | Joint past its range, degrees |
| --- | --- | --- | --- | --- | --- |
| `getup_back` (lies on its back, half on its right side; clip `_R`) | 1 | 5.2 | 2.0 | 0.47, 0.68 | 1.4 |
| `getup_front` | 1 | 4.0 | 0.8 | 0.44, 0.63 | 1.0 |
| `getup_left` | 1 | 4.7 | 1.5 | 0.47, 0.65 | 1.8 |
| `getup_right` | 1 | 4.9 | 1.7 | 0.44, 0.69 | 1.4 |
| `getup_push_weak` (30 N*s on the chest at 8.5 s) | 1 | 5.2 | 2.0 | 0.48, 0.68 | 1.5 |
| `getup_hit_strong` (200 N*s on the chest at 8.5 s) | 3 | 4.5 (the third) | 1.3 | 0.49, 0.77 | 1.6 |
| `getup_held` (two hands on the pelvis from 7.9 to 12 s) | 1 | 8.6 | 5.4 | 0.53, 0.68 | 1.3 |

Joints stretch by at most 0.3 cm and turn no faster than 38 rad/s; the body is at most 0.8 cm in the floor. The
recording stands at 2.5 s: the body takes twice as long, most of it on hands and knees while the pelvis rises
more slowly than the clip's.

**Three things that did not work, each found in the log line the controller writes four times a second** (the
clip's time, the pelvis and the feet against the clip, what the floor carries of each foot):

1. *The clip's time waiting for the pelvis alone.* The body rolled over, came onto hands and knees and stood up
   as a plank leaning 18 to 26 degrees, held by the assist with 0.9 of its weight, until the attempt was given up.
   Up to 1.3 s of the clip the feet were within 10 cm of the clip's; from 1.5 to 1.7 s the clip steps the right
   foot 45 cm forward, and ours stood where it was under 370 to 840 N.
2. *Waiting for the feet as well.* The clip stood at 1.50 s for good: the foot was 16 cm from where the clip had
   it (the limit was 15) and carried 525 N of a body that by then stood crouched on both feet.
3. *The clip laid over the feet the body stands on* (its root follows the feet that carry weight, in proportion to
   what they carry; only a foot in the air is waited for): the body stands, at the first attempt, from all four
   sides.

**The assist** was then swept down (`Tools\sweep.ps1`, six scenarios a setting):

| Pelvis cap, N | Feet cap, N | Gets up from four sides | Assist force share, mean | 200 N*s blow |
| --- | --- | --- | --- | --- |
| 500 | 200 (and lost at 45 cm) | yes | 0.61 to 0.76 | not felled: 40 cm off, back in 0.5 s with 1.2 of body weight |
| 500, 350, 250 | 200 | yes | 0.58 to 0.76 | felled (lost at 30 cm), up at the second attempt |
| 150 | 200 | no (back and right fail) | | |
| 250, 350 | 1 | no (only from the left) | | |
| 250 | 50 | yes | 0.44 to 0.48 | felled, up at the third attempt |
| 350 | 50 | yes | 0.46 to 0.51 | felled, not up within 24 s |
| any | any, pelvis pull 1000 N/m instead of 3000 | no (back and right fail) | | |

Taken: 250 N and 50 N, lost at 30 cm. The share is what the body's hands do not carry: arms of 200 N*m/rad with a
cap of 80 N*m and a wrist that cannot bend back with the palm down (step 1).

**What an interaction does** (FR-18). A hit of the flinch's threshold or more is felt as a standing body feels it,
without leaving the state: the struck part gives way and its muscles are weak for 0.35 s. The 30 N*s push delays
standing by 0.05 s. The 200 N*s blow puts the pelvis 31 cm from the clip's 0.09 s later and the get-up is given up;
the body lies on its back, tries by the clip for the back and is given up after the clip has stood at 0.76 s for
3 s (on its back with its knees up, it does not roll), rolls half over as it lets go, and gets up by the clip for
the right side. The hands land with 5 to 8 N*s each (felt as hits), press with 310 N on average for 4.5 s; the
clip's time creeps from 0.96 to 1.09 s, the pelvis stays 41 to 46 cm up, no joint is more than 1.3 degrees past
its range; 2.7 s after they leave the body stands.

Against `m3_part6_step1` the 51 scenarios before this step have the same hashes (a scenario's body gets up only
with `"get_up": true`) and seven are new: suite 58 of 58 with `-Long`. Push matrix 160 of 160, knockdowns 8 of 8,
unit tests 31, session self-test 14 of 14.

Seen and left for step 3: from a pose it did not fall into by itself (after the blow) the body needed three
attempts, and the clip for the back failed where the one for the side worked; the stand of 50 falls will say how
often. The films: `Saved\Films\getup_back`, `_front`, `_left`, `_right`, `_hit_strong`, `_held`.

## Known behaviour of the stock mannequin

- **Residual hand motion.** After the 80 N*s impulse in `impulse_passive`, the right hand keeps rotating slowly
  on the floor and the body never satisfies the rest criterion (5 cm/s, 0.2 rad/s). The scenario therefore
  has no `rest_within_s` expectation. This is a property of the stock physics asset, to be addressed by the
  project's own physics asset in M1 and M2.
- **A free constraint.** `UserConstraint_0` in the stock physics asset is not a rigid joint; the stretch
  metric skips it.
