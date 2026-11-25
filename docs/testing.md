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

### M3 part 6, step 3 (the stand of 50 falls), 2026-11-24

`Tools\getup-stand.ps1`: 50 falls drawn from a seed, each a blow or a shove (0, 0.1 or 0.2 s) of 200 to 320 N*s on
the chest or the head; 26 on the bare floor from any direction, and 8 each in the room of
`Specs\rooms\getup_room.json` with the body 45 to 90 cm from a wall, the table or the chair, knocked toward it
within 40 to 50 degrees. Table and chair are static boxes. A fall is a get-up when the body stands at the end of
40 s. The whole stand takes 90 s.

| | Falls that end standing | Floor | Wall | Table | Chair | At the first attempt |
| --- | --- | --- | --- | --- | --- | --- |
| First run | 35 of 47 (74%); 3 blows toward a wall were no falls | 23 of 26 | 4 of 5 | 4 of 8 | 4 of 8 | 18 |
| A foot is waited for 0.7 s at most; another clip after a failure; non-falls drawn again | 43 of 50 (86%) | 26 | 8 | 4 | 5 | 28 |
| The pelvis's lag judged by height (12 cm) and over the floor (12 cm) apart | 48 of 50 | 26 | 8 | 6 | 8 | 32 |
| ...height 12, over the floor 20 | 47 | 26 | 8 | 6 | 7 | 36 |
| ...height 12, over the floor 26 | 49 | 26 | 8 | 7 | 8 | 34 |
| ...height 16, over the floor 20 | 49 (98%) | 26 | 8 | 7 | 8 | 39 |
| ...and the body lost at 40 cm instead of 30 (taken) | 50 (100%) | 26 | 8 | 8 | 8 | 42 |

The four last rows differ by one or two falls, and which falls they are changes from row to row: on one seed that
is luck. On two more seeds:

| Seed | Height 12, floor 12, lost at 30 | Height 16, floor 20, lost at 30 | Height 16, floor 20, lost at 40 (taken) |
| --- | --- | --- | --- |
| 1 | 48 of 50, 32 at the first attempt | 49 of 50, 39 | 50 of 50, 42 |
| 2 | 45 of 50, 26 | 48 of 50, 36 | 49 of 50, 36 |
| 3 | 45 of 50, 37 | 48 of 50, 42 | 48 of 50, 42 |

With the values taken the body needs 1.28 attempts on average (seed 1) and stands 7 s after the blow at the median;
the assist's commanded force while it gets up is 0.51 of body weight on average over the 50 falls.

**The wider lags cost the weak push, and the suite said so.** With the clip allowed to run 16 and 20 cm ahead, the
pelvis is 18 to 23 cm from the clip's while the body rises, and the 30 N*s push of `getup_push_weak` put it 31 cm
off: lost at 30, the get-up was given up (the body got up again, but a weak push is to be ridden out). Lost at 35,
40 or 45 the push is ridden out and the 200 N*s blow still fells the body; 40 is taken. The suite's get-ups with
the final values:

| Scenario | Attempts | Stood after, s | Assist force share, mean and most |
| --- | --- | --- | --- |
| `getup_back`, `_front`, `_left`, `_right` | 1 each | 4.1, 3.3, 3.9, 3.9 | 0.45 to 0.48, 0.65 to 0.73 |
| `getup_push_weak` | 1 | 4.2 | 0.49, 0.73 |
| `getup_hit_strong` | 2 | 4.2 (the second) | 0.48, 0.77 |
| `getup_held` (two hands, 300 N each at most) | 1 | 5.4 | 0.51, 0.74 |
| `getup_pressed` (1500 N down on the pelvis for 4 s; new) | 2 | 3.4 (the second) | |

The two hands no longer hold the body down as they did in step 2: with the wider lags it rises under them, 1.3 s
later than unhindered. What it cannot lift is `getup_pressed`: the clip stands at 0.91 s for 3 s, the attempt is
given up with no joint more than 1.5 degrees past its range and the joints stretched by 0.06 cm, and the next
attempt, by another clip, stands.

**What the failures of the first run were** (from the logs of the runs):

- *The clip stood at its first key for 3 s, a foot 27 to 29 cm from where the clip has it, six attempts in a row*
  (three falls on the bare floor). The foot was off the floor, the leg across the other one: it does not come. The
  wait for a foot is now 0.7 s at most (`getup.foot_wait_s`).
- *The same clip, the same failure, five and six times.* The next attempt after one that was given up goes by
  another clip, if another starts at all like the body lies.
- *The clip stood at 1.2 s, the pelvis 13 to 15 cm from the clip's with a limit of 12, the body on its knees.* Part
  of that distance is over the floor (4 to 11 cm) and not height. Height is waited for within 16 cm, the place on
  the floor within 20.
- *Three of eight blows toward a wall felled nobody:* the body ended on its feet against the wall. Not a failure of
  the get-up, and not a fall: such a trial is drawn again (56 blows for 50 falls on seed 1).

**The falls that still do not end standing** are at the furniture (seed 1: none; seed 2: one at the table; seed 3:
one at the table and one at the chair). Other falls at the table do end standing, and the film of one
(`Saved\Films\stand_table`) shows how: the body slides under the table, and as the clip rises it backs out on
hands and knees and stands beside it. The body lies with its trunk under the table top or its legs through the
chair's, and the get-up, which is in place, has nowhere to go: the pelvis stays 20 to 30 cm below the clip's and
the attempt is given up, again and again. What would close them is the body moving out into open floor before it
rises, which this part did not build.

Against `m3_part6_step2`: the 51 scenarios without a get-up have the same hashes, the seven with one changed with the values of this step (all pass), and `getup_pressed` is new: suite 59 of 59 with `-Long`. Push matrix 160 of 160, knockdowns 8 of 8, unit tests 31, session self-test 14 of 14, the stand 50 of 50.

The stand is a step of `Tools\bootstrap.ps1` from now on.

### The stand's furniture in the middle of the room, 2026-11-24

On the owner's request the table and the chair stand in the middle of the stand's room, together, as a table and a
chair do; in step 3 each stood by itself near a corner (the table at (170, -120), the chair at (-170, 120)). Now
the table is at the origin of the room (600 by 500 cm) and the chair at (0, 50): drawn up to the table's +y long
side, its back away from the table, the front of its seat 6 cm under the table top. Nothing of the body or its
controller changed.

**Where the bodies stand.** `Tools\getup-stand.ps1` no longer has the furniture's coordinates written into it: it
reads where the pieces named `table` and `chair` stand from `Specs\rooms\getup_room.json` and places the bodies
around them. The body faces +y.

| Kind | Where the body stands | Knocked | The fall |
| --- | --- | --- | --- |
| table | 45 to 90 cm from the open long side (-y) | toward +y, within 40 degrees | forward onto the table |
| table | 65 to 110 cm from the -x end | toward +x, within 40 degrees | to its left, along the table |
| chair | 65 to 110 cm from the chair's +x side | toward -x, within 40 degrees | to its right, onto the chair and the table's corner |
| chair | 45 to 90 cm behind the chair | toward -y, within 40 degrees | backward over the chair's back onto the table |
| wall | 45 to 90 cm from a wall before or behind it, 65 to 110 cm from one at its side, in the four corner quarters of the room (140 to 220 cm from the middle along the long walls, 110 to 170 cm along the short ones) | toward the wall, within 50 degrees | as in step 3 |

**The result**, with the tuning of step 3 unchanged:

| Seed | Falls that end standing | Floor | Wall | Table | Chair | At the first attempt | Attempts a fall: floor, wall, table, chair | Blows drawn |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 1 | 49 of 50 | 26 | 8 | 7 | 8 | 39 | 1.04, 1.25, 3.00, 1.50 | 54 |
| 2 | 50 of 50 | 26 | 8 | 8 | 8 | 37 | 1.12, 1.75, 2.25, 1.50 | 57 |
| 3 | 50 of 50 | 26 | 8 | 8 | 8 | 43 | 1.08, 1.63, 1.88, 1.25 | 56 |

With the pieces apart it was 50, 49 and 48 (42, 36 and 42 at the first attempt). The difference of one or two falls
a seed is not a finding (a figure from one seed is luck, and so is one fall of 50); what three seeds do show is
that the table costs attempts: two to three a fall there, against one on the bare floor. A body that falls forward
onto the table lies across its top, or kneels at it with its trunk on the top, and the get-up in place has the
table in its way until an attempt that is given up has moved the body off it. The assist's commanded force while
getting up is 0.51, 0.51 and 0.50 of body weight on average, as before.

**What was checked besides the count.**

- *The 26 falls on the bare floor of each seed are bit for bit the falls of step 3* (78 of 78 hashes): the dice
  are rolled in the same order, and only the places in the room are other.
- *Every run repeats.* The three stands were run twice, the second time with films being rendered beside them:
  150 of 150 hashes the same.
- *No body ends on the furniture, and none in it.* The 47 falls at the table and the chair that end standing end
  with both feet on the floor beside the furniture (world positions from `trace.json`), and no body is ever more
  than 1.4 cm inside anything. One of them (seed 1, trial 36: 317 N*s on the head, forward) is thrown onto the
  table top, tries to get up there on hands and knees, slides off and stands on the floor at the third attempt.
- *The fall that does not end standing* (seed 1, trial 42: 245 N*s on the chest, forward onto the table's long
  side) folds over the table, slides down to its knees and ends on the floor with its pelvis under the table top:
  seven attempts, each given up with the pelvis 20 to 30 cm below the clip's. It is the failure of step 3, a body
  that does not move out into open floor before it rises.

**A body appeared with a finger in the furniture, before this change too.** A body is spawned in its reference
pose, the arms out, and brings them down within half a second: the fingertips are 57 cm to either side of its
axis and 26 cm ahead of it, 87 cm above the floor, below the top of the chair's back (93 cm). Step 3 put a body
as near as 45 cm to a thing at its side. In its runs `penetration.spawn_world_cm` reads 0.12 cm for a trial at the
chair (seed 1) and 0.79 cm for one at a wall (seed 2), where 0.034 cm is the soles on the floor; nothing looked
at it. A thing at the body's side is now 20 cm further off (65 to 110 cm), and the script refuses a place where
the box of the arms (58 cm to either side, 28 cm ahead, 12 cm behind), with 5 cm to spare, would touch a piece or
a wall; the first places written for the new layout were refused by it (the body beside the chair stood 34 cm
from the table's corner). Over the 72 room trials of the three seeds the figure is 0.034 cm.

**Falls at a wall and the furniture.** The falls at a wall are to be falls at a wall only. They are in the corner
quarters of the room, 130 cm and more from the furniture, and every run measures the nearest any body of such a
trial comes to a box of the furniture (`furniture_clearance_cm`: bone origins at the run's checkpoints, every
0.5 s; a hand reaches 25 cm further, so under 30 cm the script prints a warning). Nearest over the eight wall
falls of each seed: 66, 103 and 36 cm. The 36 cm is not the fall: that body (seed 3, trial 30) fell against the
wall 166 cm from the table and lay there; its first attempt was given up where it lay, the second and the third
were given up at once with the pelvis thrown 40 cm off the clip, each moving the body 55 to 65 cm toward the
middle of the room, and at the fourth it stood 30 cm from the table's end with its forearm 36 cm from the table
top. So a get-up that is given up moves the body by half a metre, and three of them in a row cross the room: in
a room of this size no place at a wall is out of reach of the furniture for good.

**A figure misread on the way.** `body.bodies[].final_location_cm` of a run is a body's centre of mass from where
the character was spawned, not a place in the world. Read as world coordinates it had eleven bodies "standing
inside the table" with their feet on the floor. The deepest penetration of those runs, under 1 cm, did not fit
that, and the trace, which is in world coordinates, had every one of them beside the furniture.

Changed: `Specs\rooms\getup_room.json` (two locations), `Tools\getup-stand.ps1` (places from the spec, the guard,
the clearance of wall falls, `-Film "<numbers>"`, and a film's camera kept inside the room's walls: placed 260 cm
out from the body as on the bare floor, it stood outside them for most places in the room). The films:
`Saved\Films\getup_stand_36`, `_38`, `_42` (table), `_43`, `_44` (chair).

Against `m3_part6`: suite 59 of 59 with `-Long`, all 59 with the same hashes (no scenario of the suite uses the
room; the physics asset, which the generator writes again on every run, is the same).

### M3 part 7, step 1, first half (the get-up's assist; a body pressed down), 2026-11-24

The owner's decisions before part 7: cut the assist of the get-up (a mean share of body weight of at most 0.3,
from 0.51), by correcting the wrist's model and giving the arms more strength; two hands hold a body down, one
slows it. What was measured turned the plan round: the assist's force went into something else, and the wrist is
a small part of it.

**The assist always pulls at its caps.** Swept down on the suite's get-ups, the mean share is 78% of what the caps
allow, whatever they are: the share measures the caps. So the question is what the body cannot do without the
pull. From its front it gets up with almost none (30 N on the pelvis, 20 N a foot: share 0.14); from its back and
its sides it does not.

**Where the pull went: dragging the body over the floor.** The log of the attempts that failed with a weak
assist, on the stand: the clip stood at its first key, the pelvis 21 to 35 cm from where the clip had it *over the
floor* and 4 cm in height, no weight on the feet. The clip's root is put where the pelvis is when the get-up
begins, and stays there unless feet carry weight. A body that turns into the clip's first pose, rolls onto its side
or comes onto hands and knees moves over the floor, and the clip then waited for the assist to drag the pelvis
back to a place that matters to nothing. `getup.pelvis_follow_s`: the clip's root follows the pelvis over the
floor, by the share of the weight that is not on the feet (the feet's share goes on following the feet, as since
part 6). With it at 0 a run is bit for bit what it was (59 of 59 hashes).

**The stand of 50 falls, three seeds each** (seeds 1, 2, 3; attempts are per fall):

| Arms while getting up | Assist caps: pelvis, feet (N an axis) | Clip follows the pelvis | Share | Falls that end standing | At the first attempt | Attempts |
| --- | --- | --- | --- | --- | --- | --- |
| 200 N*m/rad, cap 80 N*m (part 6) | 250, 50 | no | 0.51 | 49, 50, 50 | 39, 37, 43 | 1.46, 1.46, 1.32 |
| 1200, cap 120 | 250, 50 | no | 0.51 | 49, 49, 50 | 41, 39, 43 | 1.40, 1.44, 1.20 |
| 1200, cap 120 | 150, 50 | no | 0.43 | 47, 49, 49 | 34, 36, 35 | 1.70, 1.52, 1.48 |
| 1200, cap 120 | 90, 30 | no | 0.29 | 48, 49, 49 | 27, 25, 33 | 1.96, 1.90, 1.64 |
| 1200, cap 160 | 60, 20 | no | 0.20 | 43, 41, 39 | 13, 16, 21 | 3.12, 3.02, 2.46 |
| **1200, cap 120** | **90, 30** | **0.3 s (taken)** | **0.27** | **50, 48, 49** | **39, 38, 44** | **1.40, 1.48, 1.20** |
| 1200, cap 120 | 90, 30 | 0.6 s | 0.28 | 50, 49, 49 | 34, 36, 41 | 1.44, 1.42, 1.24 |
| 200, cap 80 | 90, 30 | 0.3 s | 0.27 | 48, 48, 50 | 34, 35, 39 | 1.68, 1.46, 1.30 |
| 1200, cap 120 | 60, 20 | 0.3 s | 0.19 | 50, 48, 49 | 21, 23, 26 | 1.90, 1.70, 1.58 |
| 1200, cap 120 | 30, 20 | 0.3 s | 0.14 | 46, 46, 48 | 19, 16, 15 | 2.24, 2.28, 2.04 |

With the clip following the body, an assist of 0.27 of body weight does what 0.51 did: as many falls end standing,
as many at the first attempt, as many attempts. Without the following the same caps cost half an attempt a fall.
Stronger arms are worth a tenth of an attempt a fall. Further down the body still passes the criterion (96% at
0.14), with more failed attempts, and a failed attempt is a body that flops back to the floor, which is not what a
person looks like: 0.27 is taken, and it meets the owner's aim. The four get-ups of the suite take 3.7 to 5.5 s
(3.3 to 4.1 with the stronger assist), and on the stand the body stands 8 s after the blow at the median (7 s).

**The wrist** (a variant spec with the wrist's second swing at 85 degrees instead of 25, the asset regenerated,
the spec then put back):

| Clip follows the pelvis | Caps | Falls that end standing, wrist as it is | ...wide | At the first attempt, as it is | ...wide | Attempts, as it is | ...wide |
| --- | --- | --- | --- | --- | --- | --- | --- |
| no | 60, 20 (arms cap 160) | 43, 41, 39 | 45, 48, 45 | 13, 16, 21 | 12, 19, 27 | 3.12, 3.02, 2.46 | 2.82, 2.46, 2.04 |
| no | 90, 30 | 48, 49, 49 | 48, 49, 49 | 27, 25, 33 | 29, 32, 39 | 1.96, 1.90, 1.64 | 1.84, 1.72, 1.38 |
| 0.3 s | 60, 20 | 50, 48, 49 | 49, 49, 49 | 21, 23, 26 | 26, 26, 30 | 1.90, 1.70, 1.58 | 1.84, 1.76, 1.56 |

A wrist that can bend back helps where the assist is weakest, and little once the clip follows the body. It was
not changed: the asset is as it was, and the 51 scenarios without a get-up are bit for bit the baseline. Whether
the wrist's model is corrected now or with M4's grab by the hand is the owner's to say.

**The elbow, as recorded, was not a defect of the joint.** The plan had "the elbow cannot straighten past its
reference pose". It can: the joint's range is 0 to 140 degrees from the straight arm, and with
`poses.stance.lowerarm_*.swing1_deg=-33` the standing body's arms hang straight (pose error 0.7 degrees). The
hanging arm is bent by 37 degrees because the stance does not ask for anything else. It is a change of a pose,
not of the asset, and is the second half of this step.

**What a blow does to a body getting up, with the clip following it.** The clip follows the pelvis with a lag of
0.3 s, a blow moves it faster. The second blow of `getup_hit_strong` with other impulses: 60 and 100 N*s are
ridden out, 140 N*s and more throw the pelvis 40 cm off the clip and the get-up is given up. The same threshold as
with the settings of part 6.

**A body pressed down** (owner's rule: one hand slows a get-up, two hold the body down). Three things were found.

- *With the weaker assist one hand stopped a get-up as surely as two.* The body came up into a crouch and stayed
  there, its pelvis 25 cm behind where the clip had it over the feet and 14 to 18 cm low, and the clip waited.
  Legs twice as stiff lifted it and spoilt the unhindered get-up (8.3 s instead of 4.7).
- *No strength of the body gives a clean line between one hand and two.* Its legs lift 600 N as they lift 300 N,
  or neither, depending on the phase of the clip the press finds it in. The line is now a rule: a body pressed
  with `getup.hold_force_n` (450 N) or more does not push on, its clip waits and the attempt is given up after
  `stall_s`; pressed with less, it gets up where the hand lets it (the clip follows the pelvis on its feet too,
  `pressed_follow`, and may run 22 cm ahead in height, `pressed_lag_cm`, since a body that pushes against a hand
  sags under it). What presses is the force felt in every frame of 0.3 s from other simulated bodies, the sense
  the body leans into a held push with. None of this acts on a body nothing presses: the stand's 50 falls of seed
  1 have the same hashes with and without it.
- *The scenario with two scripted hands had one hand on the body.* A scripted hand goes to a point of the world;
  the right one slid off the rising body and ended on the floor (its contact peak of 2360 N was the floor), and
  the left one alone was felt as 313 to 342 N: more than its spring's cap of 300 N, by its own weight and the
  body's push against it. A threshold of 300 N, tried first, therefore "held with two hands" a body that one hand
  pressed. The rule is now checked with scripted forces of 300 N and 600 N on the pelvis.

| Scenario | Presses | Felt at most, N | Attempts | Stood after, s |
| --- | --- | --- | --- | --- |
| `getup_back` (nothing presses) | | 0 | 1 | 4.7 |
| `getup_one_hand_slows` (new) | one scripted hand on the pelvis, to the end of the run | 342 | 1 | 5.0 |
| `getup_pressed_one_hand` (new) | 300 N down on the pelvis for 8 s | 300 | 1 | 4.9 |
| `getup_held` | two scripted hands for 4.5 s, one of which slides off | 340 | 1 | 5.3 |
| `getup_pressed_two_hands` (new) | 600 N down on the pelvis for 9 s, to the end of the run | 600 | 2, both given up as held down | does not stand |
| `getup_pressed` | 1500 N for 4 s | 1500 | 2, the first given up as held down | 3.7 (the second) |

New: `getup.pelvis_follow_s`, `hold_force_n`, `pressed_follow`, `pressed_lag_cm`; the expectation `max_get_ups`;
`control.get_up.pressed_max_n`; three scenarios. Changed values: the arms of `states.get_up` (1200 N*m/rad, cap
120 N*m), the assist's caps of `states.get_up` (90 N and 30 N).

Against `m3_part7_start`: 51 scenarios with the same hashes, the 8 with a get-up changed (all pass), 3 new: suite
62 of 62 with `-Long`. Push matrix 160 of 160 and knockdowns 8 of 8 with the figures of part 6 (their bodies do not
get up), unit tests 31, session self-test 14 of 14, the stand 50, 48 and 49 of 50.

Not done, and open: one hand slows a get-up by 0.2 to 0.6 s only, which may read as no effect at all (the owner's
to judge in a session); the films of the get-ups with the weaker assist (`Saved\Films\p7_getup_back`, `_front`)
read as before; of the 202 attempts of the three seeds 151 end standing, 22 are given up with the clip waiting,
most of them in the crouch at 1.0 to 1.5 s of the clip with the pelvis behind its feet, and 29 with the pelvis
40 cm off the clip (as many of each as with the assist of part 6: 34 and 25 of 211).

### M3 part 7, step 1, second half (the stance's elbow; a press on 50 falls), 2026-11-24

The owner decided, on the measurements above, that the wrist's model is corrected in M4 with the grab by the hand.
What is left of step 1 is the elbow, and it brought out that the figures for a pressed body had been one fall's.

**The stance's elbows: -20 degrees from the reference pose** (`poses.stance.lowerarm_l`, `_r`), which leaves 17
degrees of bend where there were 37. Chosen from pictures of the standing body from the front and the side at 0,
-15 and -25 (`Saved\Films\p7_elbow_compare.png`): as it was, the forearms are held forward of the thighs; at -25
the arm is a straight stick; a relaxed arm is in between. The hand's centre of mass is 9 cm further back and 4 cm
lower, beside the thigh. Against `m3_part7_step1a`: the 51 scenarios with a body under control all changed, as
they must (every one stands in the stance or falls out of it), and the 10 without one have the same hashes
(`drop_*`, `impulse_passive`, the portraits, the room and headset scenes).

| | Before | With the elbows at -20 |
| --- | --- | --- |
| Quiet-standing assist share, 10 s and 10 minutes (bound 0.0093) | 0.0067, 0.0066 | 0.0067, 0.0066 |
| Pelvis drift standing, cm | 0.91 | 0.91 |
| Push matrix | 160 of 160 | 160 of 160 |
| Knockdowns from 8 directions: slowest to rest, most energy gained, deepest in the floor | 8 of 8: 1.94 s, 25.9 J, 1.22 cm | 8 of 8: 2.50 s, 25.7 J, 1.33 cm |
| The stand: falls that end standing, at the first attempt, attempts a fall | 50, 48, 49; 39, 38, 44; 1.36 | 49, 50, 49; 41, 43, 45; 1.35 |
| Strikes on the forearm (`strike_punch_forearm`, `_heights`) | pass | pass |

**Three scenarios failed, none through the elbow itself: each was written for where one fall happened to put the
body.** With other arms the same blow fells the body another way (after the blow from the front it now lies on
its back and gets up by the clip for the back, where it had lain half on its right side and taken that clip).

- `getup_front` stood after 7.3 s instead of 3.7 and was in the middle of bringing its feet together when its
  16 s were over (`end_state` stagger). It has 20 s now.
- `getup_one_hand_slows`, written in the first half, pressed a scripted hand on the place where the body used to
  lie; the hand met it with 33 N*s and stayed in the air. Removed: a scripted hand cannot follow a body, and
  where a body lies changes with every change to it.
- `getup_pressed_one_hand` (300 N on the pelvis) took two attempts where it had taken one.

**A press on 50 falls instead of one.** That last failure asked how much of "one hand slows a get-up by 0.2 s"
was one fall's luck. `Tools\getup-stand.ps1 -PressN <force>` presses every fallen body down on the pelvis from
5 to 25 s and counts the bodies that stood while they were pressed.

| Press, N | Seed | Stood while pressed (5 to 25 s) | Stand at the end (40 s) | Attempts a fall |
| --- | --- | --- | --- | --- |
| none | 1, 2, 3 | | 49, 50, 49 | 1.40, 1.30, 1.36 |
| 300 (one hand) | 1, 2, 3 | 30, 35, 31 of 50 | 45, 46, 48 | 3.02, 2.32, 2.74 |
| 600 (two hands) | 1, 2, 3 | 0, 0, 0 of 50 | 44, 43, 44 | 4.68, 4.38, 4.44 |
| 300, with `pressed_lag_cm` 28 and `pressed_follow` 1 | 1, 2, 3 | 25, 31, 32 of 50 | 48, 49, 47 | 3.06, 2.52, 2.76 |

Two hands' worth keeps every body down, 150 of 150: that half of the owner's rule holds. One hand's worth does
not "only slow" a get-up: two bodies of three get up under it, with twice the attempts, and one of three stays
down until the press ends. The figure of the first half (0.2 to 0.6 s lost) was three runs of one fall. Wider
allowances for a pressed body change nothing. The single scenario now asks only that the body stands by the end
of its run; what a press of 300 N does is told by the stand.

Changed: `poses.stance.lowerarm_*` (-20), `Scenarios\getup_front.json` (20 s), `getup_pressed_one_hand.json`
(no limit on attempts), `Tools\getup-stand.ps1` (`-PressN`, `-PressFromS`, `-PressForS`); removed
`Scenarios\getup_one_hand_slows.json`.

Suite 61 of 61 with `-Long`, push matrix 160 of 160, knockdowns 8 of 8, unit tests 31, session self-test 14 of
14, the stand 49, 50 and 49 of 50.

### M3 part 7, step 2 (friction and restitution of the body's surface), 2026-11-24

The owner's decision before part 7: friction and restitution of the body against the floor are measured and chosen
before muscle strength and solver iterations are. Until now they were whatever the engine gave.

**What acted.** Every one of the NPC's 18 bodies had the engine's default physical material: friction 0.7,
restitution 0.3. The generator copies the template asset's material, and the MetaHuman template has none. Two
surfaces are combined into a contact's figures by the mode with the higher number of the two (0 average, 1 the
smaller, 2 the product, 3 the larger); everything asks for the average. The material's "override combine mode"
flags are read by nothing in the engine: a material's own mode always counts. The engine applies restitution only
to a contact closing faster than 5.6 cm/s (1000 cm/s² times the 1/180 s substep). A run now reports all of it:
`body.bodies[].material`, `friction`, `restitution`, and `body.surfaces`. Reading it changes nothing (61 of 61
hashes of `m3_part7_step2_start`).

**Tuning values.** `surface.friction` (every body but the feet), `surface.feet_friction`, `surface.restitution`,
`surface.friction_combine`, `surface.restitution_combine`. The controller makes two materials of its own, gives
them to its bodies as overrides and brings the solver's copy up to date when a value changes. With the values
that acted before (0.7, 0.7, 0.3, average) all 61 scenarios have the baseline's hashes.

**Friction of the pair, by experiment** (`Tools\surface-friction.ps1`: the body is knocked down, lies still and is
dragged by a steady force on its pelvis; from the trace, the friction is (F/m - a)/g while it is pushed and -a/g
after):

| `surface.friction` and `feet_friction` | Combine | Expected on the floor (0.7) | While pushed | After the push |
| --- | --- | --- | --- | --- |
| 0.7 (as before) | average | 0.70 | 0.69 | 0.69 |
| 0.3 | average | 0.50 | 0.51 | 0.50 |
| 0.3 | the smaller | 0.30 | 0.29 | 0.30 |
| 0 | average | 0.35 | | 0.36 |
| 0.5 | the product | 0.35 | | 0.36 |
| 1.1 | average | 0.90 | 0.89 | |
| 1.0 | the larger | 1.00 | 0.94 | 0.89 |
| 0.7, changed to 0.3 at 7.5 s of the run | average | 0.50 after the change | 0.51 | 0.49 |

A force of 500 N does not move the body and 560 N does (the threshold at 0.7 is 549 N). The settings mean what
the engine's source says, a change in the middle of a run reaches contacts that already exist, and at 0.9 and
more the figure reads low (limbs catch and roll). The first way tried, the push's impulse over the time the body
is in motion, read 0.55 to 0.58 at 0.7: the mass-weighted speed stays above its threshold while limbs settle, long
after the body has stopped. `tune_live_surface` holds the mechanism in the suite: a lying body whose friction is
taken in the middle of the run slides 7.1 m under a push of 300 N that would not move it otherwise
(`min_pelvis_drift_cm`, new).

**The sweep.** Figures are the pair's friction on the floor. Quiet standing, the push matrix and 8 knockdowns:

| Feet | Body | Restitution | Assist share standing | Push matrix | Knockdowns: pass, mean rest, ends this far away |
| --- | --- | --- | --- | --- | --- |
| 0.7 | 0.7 | 0.3 (before) | 0.0067 | 160 of 160 | 8 of 8, 1.61 s, 189 cm |
| 0.5 | 0.5 | 0.3 | 0.0067 | 159 (one fall at 80 N*s) | 8, 1.73 s, 200 cm |
| 0.9 | 0.9 | 0.3 | 0.0068 | 160 | 7 (one at rest after 3.8 s), 1.91 s, 181 cm |
| 0.9 | 0.5 | 0.3 | 0.0068 | 160 | 8, 1.67 s, 194 cm |
| 0.7 | 0.5 | 0.3 | 0.0067 | 160 | 7 (3.2 s), 1.74 s, 196 cm |
| 0.9 | 0.7 | 0.3 | 0.0068 | 160 | 7 (3.3 s), 1.84 s, 186 cm |
| 0.5 | 0.7 | 0.3 | 0.0067 | 160 | 8, 1.59 s, 188 cm |
| 1.0 | 0.4 | 0.3 | 0.0067 | 160 | 8, 1.83 s, 201 cm |
| 0.7 | 0.7 | 0.15 (0, average) | 0.0063 | 160 | 8, 1.65 s, 187 cm |
| 0.7 | 0.7 | 0 (0, the smaller) | 0.0061 | 160 | 8, 1.64 s, 187 cm |

Standing does not care about friction; without restitution the assist's share is a tenth lower. The push matrix
has the same counts in every row within two cells. The one knockdown over 3 s is another fall each time: a figure
from one fall. On 48 knockdowns (200, 260 and 320 N*s on the chest and the head) 42 to 44 pass at every setting
tried, the mean rest is 1.7 to 2.0 s and the mean energy gain 8 J; restitution changes none of it.

The stand, three seeds each (150 falls):

| Feet | Body | Restitution | End standing | At the first attempt | Attempts a fall |
| --- | --- | --- | --- | --- | --- |
| 0.7 | 0.7 | 0.3 (before) | 148 | 129 | 1.35 |
| 0.5 | 0.5 | 0.3 | 147 | 130 | 1.32 |
| 0.9 | 0.9 | 0.3 | 149 | 101 | 1.54 |
| 0.9 | 0.5 | 0.3 | 150 | 129 | 1.21 |
| 0.7 | 0.5 | 0.3 | 150 | 124 | 1.30 |
| 0.9 | 0.4 | 0.3 | 149 | 130 | 1.25 |
| 0.7 | 0.7 | 0 | 148 | 120 | 1.39 |
| 0.9 | 0.5 | 0 | 145 | 130 | 1.31 |
| 0.9 | 0.5 | 0.1 | 148 | 127 | 1.37 |
| **0.7** | **0.5** | **0.1 (taken)** | **149** | **127** | **1.33** |

One thing stands out of the noise (two or three falls, five first attempts): a body with 0.9 against the floor
gets up at the first attempt 101 times instead of 124 to 130. A body turning into a clip's first pose and coming
onto hands and knees has to slide over the floor. From 0.4 to 0.7 nothing differs.

**Grippy soles were not taken.** With the feet at 0.9 the stand was as good as any (150, 129, 1.21) and the
matrix passed, and two scenarios of the suite then failed in every combination with it: `push_thigh_200_stumbles`
fell (4 of 4 runs, 0 of 4 with the feet at 0.7), and `push_pelvis_80_side_steps` ended in Stagger (3 of 4, 0 of
4). The film of the second (`Saved\Films\sf_pelvis_side`): after two crossing steps the body stands with its legs
crossed, one foot against the other, and stays so for the 4.5 s left of the run. The matrix calls that "stepped".
In the matrices one run of 160 ends in Stagger at two of nine settings (one of them with the feet at 0.5) and
none at the others, so it is rare at any friction; it is a defect of leaving Stagger that this scenario shows
with these soles, and it is open (below). With the average, 1.1 is also what the two feet have against each other.

**Taken: `friction` 0.3, `feet_friction` 0.7, `restitution` 0.1 with `restitution_combine` 1.** On the floor that
is 0.5 for the body and 0.7 for the soles, and a restitution of 0.1 against anything that has more. The body's
0.5 is the middle of what is given for cloth and skin on a floor and well clear of the 0.9 that hinders getting
up; the soles stay where they were, since nothing measured asks for a change and more grip does harm; restitution
is near zero as a body's is, and no run could tell 0, 0.1 and 0.3 apart beyond noise. A search for published
figures gave the 0.5 that standards ask of a walking surface and nothing measured for cloth on a floor: the range
is the owner's. Dragged, the lying body now reads 0.51 to 0.54 (its feet carry part of it at 0.7).

| | Before | With the values taken |
| --- | --- | --- |
| Friction of the lying body on the floor, measured | 0.69 | 0.51 to 0.54 |
| Quiet-standing assist share, 10 s and 10 minutes (bound 0.0093) | 0.0067, 0.0066 | 0.0063, 0.0062 |
| Pelvis drift standing, cm | 0.91 | 0.91 |
| Push matrix; the 200 N*s row | 160 of 160; 15 stepped, 25 fell | 160 of 160; 12 stepped, 28 fell |
| Knockdowns from 8 directions: slowest to rest, most energy gained, deepest in the floor | 8 of 8: 2.50 s, 25.7 J, 1.33 cm | 8 of 8: 2.21 s, 25.5 J, 1.23 cm |
| ...mean rest, and how far from where it stood the body ends | 1.61 s, 189 cm | 1.55 s, 196 cm |
| 48 knockdowns up to 320 N*s, chest and head | 43 pass | 46 pass |
| The stand: end standing, at the first attempt, attempts a fall | 49, 50, 49; 41, 43, 45; 1.35 | 50, 50, 49; 38, 44, 45; 1.33 |
| ...on its feet after the blow, median and 90th percentile | 8.0 s, 14.4 s | 7.9 s, 18.4 s |
| The suite's four get-ups, s | 5.0, 7.3, 4.9, 4.7 | 5.4, 4.1, 5.7, 4.8 |

Against `m3_part7_step2_start`: the 51 scenarios with a body under control changed, as they must (each has
another surface), all pass; the 10 without one have the same hashes; `tune_live_surface` is new. Suite 62 of 62
with `-Long`, unit tests 31, session self-test 14 of 14. Baseline `m3_part7_step2`.

**How it looks** (`Saved\Films\sf_fall_new` and `_old`, `sf_getup_new` and `_old`). After the blow of
`hit_chest_200_falls` the body used to stay on its back with its knees up and its feet planted; now the legs
slide out within a second and it lies flat. The get-up of `getup_back` goes another way for it (over the front,
onto hands and knees) and takes 5.4 s instead of 5.0: one fall, and the stand says the two are level.

**For M4.** A lying body of 80 kg at 0.5 needs about 400 N to be dragged: one hand at its cap of 300 N does not
move it, two (600 N) do, with 2.5 m/s² to spare. At 0.7 it took 549 N, and two hands had 0.6 m/s².

**The stand runs shorter.** A trial ran for 40 s whatever happened, and most bodies stand 8 s after the blow. A
scenario can now end once every event is over and the body has been Standing for a time without a break
(`end_when_standing_s`; `Tools\getup-stand.ps1 -StopStandingS`, 3 s by default, 0 for the whole length). All 150
trials of three seeds agree with the full-length runs on who stood, after how many attempts and when; the mean
trial is 18.6 s. One stand takes 66 s instead of 90. `Tools\getup-seeds.ps1` runs three seeds side by side and
prints one line a seed: 106 to 114 s for the three.

**An engine process takes 1.75 GB.** Three stands side by side with 15 processes took 26 GB of the machine's 32
and a batch was stopped for want of memory. Nine processes (16 GB) leave room; `getup-seeds.ps1` has 3 a stand
by default.

**Open, found on the way.**

- *The crossed-leg stance that Stagger does not leave* (above): `push_pelvis_80_side_steps` with
  `-Tune "surface.feet_friction=1.1"` shows it.
- *A body that ends kneeling trembles.* One knockdown of 48 (260 N*s on the head from the front) ends sitting on
  its calves, propped on its hands, pelvis 40 cm up, and trembles at 4 to 6 cm/s around the rest threshold
  (0.05 m/s) for good: `rest_after_s` is null with restitution 0.3, 1.7 s without. Other fallen bodies lie at
  0.001 to 0.004 m/s.
- *Blows harder than the criterion's.* Of 48 knockdowns up to 320 N*s, 2 to 6 do not pass at any setting: at rest
  after 3 to 7 s, and one fall (320 N*s on the head, forward) with 50 J of energy gain. The criterion of part 5
  is 200 N*s on the chest, where 8 of 8 pass.

**Mistakes of mine.** A helper script had hash keys `s` and `S`, which PowerShell takes for one; the batch that
used it failed at once and was waited for. Five stand candidates were put in one batch where three would have
shown the trend. The first friction figure came from a measure that was not checked against a known case.

Changed: `Config/Tuning/body.json` (`surface`), `SandboxBodyTuning` and `SandboxBodyController` (`ApplySurface`),
`ScenarioRunnerSubsystem` and `ScenarioSpec` (materials in the result, `min_pelvis_drift_cm`,
`end_when_standing_s`), `Tools\getup-stand.ps1`; new `Tools\getup-seeds.ps1`, `Tools\surface-friction.ps1`,
`Scenarios\tune_live_surface.json`.

### M3 part 7, step 3 (the sweeps: solver iterations, muscles, assist), 2026-11-24

The owner accepted the surface values of step 2, and the sweeps were made on them. One value at a time, each
against quiet standing (`stand_npc`), the knockdowns from 8 directions and the push matrix (the 30 and 80 N*s
rows for muscles and assist, all four for the iterations); what looked like a candidate, against the whole suite
and the stand on three seeds. **No value was changed:** every one sits between two that are worse, or on a
plateau. The suite is bit for bit the baseline `m3_part7_step2`.

**Solver position iterations** (the owner's rule: the smallest count that passes every criterion and the matrix):

| Iterations | Assist share standing (bound 0.0093) | Pelvis drift, cm | Push matrix | Knockdowns | Physics step alone, ms a frame |
| --- | --- | --- | --- | --- | --- |
| 64 (as it is) | 0.0063 | 0.91 | 160 of 160 | 8 of 8 | 0.58 |
| 48 | 0.0068 | 0.91 | 158 | 7 (one at rest after 3.6 s) | |
| 40 | 0.0073 | 0.92 | 158 | 8 | |
| 32 | 0.0083 | 0.94 | 158 | 8 | 0.43 |
| 16 | 0.0136 | 1.07 | 160 | 8 | 0.36 |
| 8 | 0.0255 | 1.25 | 153 (5 pushes of 80 N*s held without a step) | 6 (2.4 cm into the floor) | |

Only 64 passes all of it. 16 and 8 break the bound on the assist's share; 32 to 48 keep it and lose the same two
cells of the matrix, mirror images of each other (30 N*s on the head from behind and to a side: two steps where
one is allowed). At 32 the stand is as it was (149 of 150, 126 at the first attempt) and two get-up scenarios of
the suite fail (`getup_held`: its scripted hand misses the body; `getup_pressed`: not yet standing at the end:
each one fall). 32 would save 0.15 ms of a frame of 11.1 ms.

**Those two cells are not a second step.** In every candidate that loses a cell of the 30 N*s row, the body makes
one recovery step and one step that brings the feet back, by its own log, and the run counts three: as the
stepping foot lands, the other one leaves the floor for 0.12 s and stands again 5.0 to 6.1 cm away. A step
measured from outside is a foot that left the floor for 0.1 s and stood again 5 cm or more away
(`StepMinLengthCm`), and this shuffle is just over it. (Four more candidates lose a cell on a blow to the head
from the side: three flights of a foot of 5 to 11 cm.) Judged again from the saved runs with a step counted from
10 cm, no cell of the 30 N*s row has a step too many at 16 to 64 iterations or in any muscle or assist candidate
but the one with no assist at all. At the values as they are, the same blow leaves the capture point under the
4 cm that call for a step, and the body does not step at all; with the pelvis's angular assist at half it is
4.4 cm out. The matrix's 160 of 160 has one row standing on two thresholds. Whether a shuffle of 5 cm is a step is the owner's
to say; the measure was not changed.

**Muscles** (stiffness at half and at one and a half or twice; damping at half and twice):

| Group | Value | What changed |
| --- | --- | --- |
| Legs, stiffness 800 | 400 | Pelvis drift 1.11 cm; 4 pushes of 80 N*s held without a step: legs too weak to step |
| | 1200 | A blow of 200 N*s on the chest no longer fells the body from two directions of eight: the table wants a fall |
| Spine, 600 | 300, 900 | Nothing beyond one knockdown over 3 s and one shuffle cell |
| Arms, 40 | 20, 80 | Nothing (fastest joint standing 1.62 and 1.40 rad/s against 1.43) |
| Neck, 40 | 20, 80 | One shuffle cell each |
| Damping: legs 0.06 | 0.03, 0.12 | Two knockdowns over 3 s at 0.03; 33.5 J of muscle work in a fall at 0.12 (25.5) |
| Damping: spine 0.06, arms 0.15, neck 0.1 | half, twice | One or two knockdowns over 3 s with the arms' at either; nothing else |

The legs are held from both sides; the rest is a plateau. "One knockdown over 3 s" is what one fall of eight does
by itself: three of the ten surface settings of step 2 had one.

**Assist.**

| What | Assist share standing | Pelvis drift, cm | Push matrix, rows 30 and 80 |
| --- | --- | --- | --- |
| As it is | 0.0063 | 0.91 | pass; 2 falls at 80 N*s |
| Pelvis horizontal 750 (1500) | 0.0051 | 1.12 | two shuffle cells |
| Pelvis horizontal 0 | 0.0033 | 1.54 | one shuffle cell |
| Feet 750 (1500) | 0.0048 | 0.91 | one shuffle cell |
| Feet 0 | 0.0030 | 0.91 | one shuffle cell |
| Pelvis angular 30 or 0 (60) | 0.0064 | 0.92 | one shuffle cell |
| Chest angular 0 (60) | 0.0063 | 0.92 | 6 falls at 80 N*s, one push of 30 held |
| Head angular 0 (10) | 0.0063 | 0.90 | two shuffle cells |
| Feet angular 0 (15) | 0.0064 | 0.91 | pass |
| Everything at half | 0.0037 | 1.12 | one shuffle cell; 4 falls at 80 N*s |
| No assist at all | 0 | 1.60 | 8 cells of the 30 N*s row fail (up to 7 steps after a blow on the head); 6 falls at 80 N*s |
| Stagger: pelvis torque cap 100 N*m (150) | 0.0063 | 0.91 | pass by the table, 6 falls at 80 N*s |
| ...cap 50 N*m | 0.0063 | 0.91 | 4 cells fail, 13 falls at 80 N*s |
| ...angular stiffness 500 (1000) | 0.0063 | 0.91 | as it is: the cap rules |

The body stands for 10 s with no assist at all. With everything at half the stand is level (148 of 150, 127 at
the first attempt) and `stand_foot_moved` falls. The cap of 150 N*m on the pelvis's upright hold in Stagger,
chosen in part 4 as the lowest that passes, is still that: at 100 three times as many pushes of 80 N*s end on the
floor.

Tooling: `Tools\suite.ps1 -Tune "<values>" -Label <name>` runs the whole suite against other values into
`Saved\Suites\<name>` and leaves the files baselines are made of alone.

### M3 part 7, before step 4 (what counts as a step; a body at rest off its support), 2026-11-24

Two things the owner decided on 24 November to have done before the clips: the push matrix counts a step from
10 cm, and the stance on crossed legs that Stagger did not leave is taken apart now.

**A step in the push matrix is a foot carried 10 cm or more** (`Tools\push-matrix.ps1 -StepMinCm`, 10 by default).
The flights of a foot are the run's own record (`control.steps`, from 5 cm); those of 10 cm or more are steps,
less the controller's regrouping steps of that length, and never fewer than the controller's own steps for
balance; the shorter ones are shuffles, counted in every cell and row and judged by nothing. No value of the body
changed.

| Check | Result |
| --- | --- |
| The values as they are | 160 of 160; against the matrix of `m3_part7_step2` 160 runs end the same way; shuffles 0, 3, 12 and 7 in the four rows |
| 32 solver iterations, the 30 N*s row | 40 of 40 (5 shuffles) |
| ...with `-StepMinCm 5` | 38 of 40: `push_neck_01_225_030` and `push_neck_01_315_030`, two steps where one is allowed, as in step 3 |
| `Tools\baseline.ps1 -CompareMatrix` | reads the new file as before (it compares outcomes and state sequences) |

**The crossed-leg stance.** `push_pelvis_80_side_steps` with `-Tune "surface.feet_friction=1.1"`: after two steps
across (the second lands at 3.57 s) the body stands on crossed legs, one foot 35 cm before the other and 1.5 cm
to its side, and is in Stagger at the end of the run with nothing in the controller's log after the second step.

*What held it.* Two questions were asked of two different supports, and the answers left a gap. A step is needed
when the capture point is more than `balance.recoverable_cm` (4 cm) outside the *whole soles* of the feet the
floor touches; Stagger ends when the centre of mass is over the part of the soles that *touches*. The body stood
on the edges of its feet. From 4.8 s to the end of the run, every frame:

| | cm |
| --- | --- |
| Centre of mass against the support that touches | 6.0 to 7.0 outside |
| Capture point against the same | 6.0 to 7.5 outside |
| Capture point against the whole soles (what the decision is made on) | 2.3 to 3.2 outside |
| Speed of the body, root mean square | 0.05 m/s, falling to 0.01 |

So no step (under 4 cm) and no Standing (not over its support), for good. A body cannot rest with its centre of
mass 6 cm off its support by itself: in Stagger the pelvis is held upright with up to 150 N*m, and 80 kg held
6 cm off is 47 N*m. The stagger's torque share was 0.11 of body weight times the height of the centre of mass on
average over the 5 s. The allowance of 4 cm is what ankles and hips bring back of a body in motion; at rest the
capture point is the centre of mass and nothing brings it back. This is the assist holding a body that needs a
step, which the owner's table calls a failure, in a place the table's check (`held`) does not see: the push
matrix read the run as "stepped". `series.csv` now has the decision's margin beside the other two
(`decision_margin_cm`, last column; all 62 hashes as before).

*How often.* The condition (in Stagger, both feet down, no faster than `balance.recover_speed_mps`, centre of
mass and capture point outside the support that touches) lasts at most 0.09 s in the 160 runs of the push matrix
and in no run longer; in the stuck run it lasts from 3.71 s to the end.

*The rule.* A staggering body in that condition for `balance.rest_step_s` (0.3 s: three times the longest
passing case, and about a person's reaction time) needs a step. Which foot and where is the stagger's own
planning: the feet take turns, so the foot that was crossed under steps out to its own side.

| | Before | With the rule |
| --- | --- | --- |
| `stagger_rest_off_support` (new: the same push, the soles made grippy by a console event at 1 s) | ends in Stagger, feet 46 cm from their stance | a third step at 4.01 s, 37 cm out to the side; Standing at 4.37 s; two regrouping steps; feet in their stance at 7.3 s |
| The same with `-Tune "balance.rest_step_s=0"` | | ends in Stagger: the scenario fails without the rule |
| Suite with `-Long` against `m3_part7_crossed_start` | 62 of 62 | 63 of 63; the 62 have the same hashes, one new |
| Push matrix | 160 of 160 | 160 of 160, every run ends the same way |
| 80 runs (80 and 200 N*s) with the soles at 1.1 | 2 end in Stagger | 1, and that one is a regrouping step that landed 0.14 s before the run's end; the rule asked for a step in 2 runs |
| Knockdowns | 8 of 8 | 8 of 8 (2.21 s, 25.5 J, 1.23 cm: the same) |
| The stand, three seeds | 50, 50, 49; 38, 44, 45 at the first attempt | 50, 50, 49; 38, 44, 45; the rule asked for no step in 150 falls |
| Unit tests, session self-test | 31; 14 of 14 | 31; 14 of 14 |

How it looks (`Saved\Films\stagger_rest_off_support\sheet.png`, from the front): crossed legs at 3.6 to 4.05 s,
the right foot out to the side at 4.15 to 4.25 s, a wide stance at 4.4 s, the feet together from 7.3 s.

Not changed, and seen on the way: the second step of that run is itself a step across. The left foot is put
before the right one because the capture point is heading 2 cm past the right foot's line, and a target past
that line makes a step a crossing one whatever the distance. It is what puts the feet in one line; the third
step now undoes it. Whether a target a few centimetres past the line should cross is a question of how the steps
look, for the clips.

Changed: `Tools\push-matrix.ps1`; `SandboxBodyController` (`UpdateState`: the rule), `SandboxBodyTuning`
(`balance.rest_step_s`), `Config/Tuning/body.json` and its `README.md`, `ScenarioRunnerSubsystem` (the series'
last column), `Scenarios\stagger_rest_off_support.json`, `Scenarios\README.md`.

### M3 part 7, step 4 (the figures of the gate, the clips), 2026-11-24

All on the build with the rule above; no value of the body changed in this step.

| What | Figure |
| --- | --- |
| Suite with `-Long` | 63 of 63 in 85 s |
| Unit tests; session self-test | 31; 14 of 14 |
| Push matrix (a step from 10 cm) | 160 of 160; 10 N*s: 40 stood; 30: 36 stood, 4 stepped; 80: 3 stood, 35 stepped, 2 fell; 200: 12 stepped, 28 fell; shuffles 0, 3, 12, 7; worst joint stretch 0.33 cm |
| Quiet-standing assist share (bound 0.0093) | 0.0063 over 10 s (0.0115 at most), 0.0062 over 10 minutes; pelvis drift 0.91 cm; the head travels 4.1 cm with the idle |
| Knockdowns, 200 N*s on the chest from 8 directions | 8 of 8; at rest after 1.26 to 2.21 s; 5.6 to 25.5 J of muscle work; 1.23 cm into the floor at most, 0.45 cm into itself |
| 48 knockdowns, 200, 260 and 320 N*s on the chest and the head | 46 pass; `neck_01` 315 degrees 260 N*s at rest after 5.62 s; `neck_01` 90 degrees 320 N*s gains 50.3 J |
| The stand, seeds 1 to 3 | 50, 50, 49 of 50; 38, 44, 45 at the first attempt; 1.36, 1.26, 1.38 attempts a fall; assist share 0.28, 0.28, 0.27 |
| The suite's get-ups from the back, the front, the left, the right | 5.4, 4.1, 5.7, 4.8 s, each at the first attempt, assist share 0.26 to 0.28 |
| The longest runs after 200 N*s (pelvis and thighs; 12 of 24 cells stay up) | 8 steps and 1.70 m (`thigh_r` 45 degrees), 7 and 1.86 m (`pelvis` 45), 7 and 1.87 m (`thigh_l` 135); 2.5 to 3.3 s in Stagger |

**The clips** are 25 sheets of pictures under `Saved\Films\m3_*` (listed in `Saved\Films\m3_gate.txt` and in the
M3 section of `docs/PRD.md`), made by one script that calls `Tools\film.ps1` for each; the camera of a fall, a
get-up and a run is put beside the way from where the body stood to where its pelvis ends. Runs of the push
matrix and trials of the stand are filmed from their generated scenario files (`-File`).

What the sheets show that the figures do not:

- *The fall that never stands* (`m3_getup_on_chair`, seed 3, trial 45): the body goes over the chair's back and
  hangs there bent double, its legs on one side and its trunk on the other. Each of 14 attempts is given up in
  the frame it starts in: the pelvis is 40 cm from where a clip that begins on the floor has it. It is not
  "under the table"; it is a body lying on a piece of furniture, for which there is no clip.
- *A get-up given up on the bare floor* (`m3_getup_given_up_floor`): on its back, the body lifts its
  legs and an arm and stays so for about 5 s before the attempt is given up (the clip waits for a pelvis 28 cm
  away); it drops back, and stands by another clip in 4.3 s.
- *The runs* read as a body carried off sideways with its arms out, stepping fast for a second and standing in a
  wide stance, then bringing its feet together.

Mistakes of mine in this part: the first cameras of the three runs stood 5.2 m away and the body was a fifth of
a tile high (filmed again from 3.6 m); the overlay sheets were first made from the `quarter` view, too far for
the text and the lines to be read (again from 3 m).

### M3 gate: the review of the clips, a bound on a get-up's waiting, one hand measured again, 2026-11-24

The owner handed the review of the clips and the open questions of the gate to the agent ("decide by the data and
the plan, and go on"). All 26 sheets were looked at. What follows is what the look found, what was changed for it,
and one figure of the gate that turned out to be stale.

**What the sheets show.** Pushes of 30 and 80 N*s, the held push, the hand that pushes the body back, the three
falls and the four get-ups read as a person: a head that gives way and comes back, one step and the feet brought
together, a lean into a hand and a lunge away from it, a fall onto the seat with the knees up or onto the forearms,
a roll onto hands and knees, a crouch, standing. The three runs after 200 N*s read as a body carried off sideways,
arms out, that steps fast for a second and a half, stands wide and brings its feet together by 5 to 6 s; nothing in
them calls for a limit of steps. Two things do not read as a person, both in get-ups that are given up:

- *At the table* (`m3_getup_given_up_table`) the body held one pose, propped on its hands with its back under the
  table's edge, from 10 s to 22.9 s: 13 s of an attempt of 15.6 s.
- *On the bare floor* (`m3_getup_given_up_floor`) it lies on its back with its legs and an arm raised for about
  4 s of an attempt of 8.5 s.

**Why an attempt could last 15.6 s with `getup.stall_s` at 3.** The stall is counted while the clip's time stands
still and is set back to 0 whenever the clip moves on. A body that hovers at the lag at which the clip waits lets
it move on by a frame now and then, and the count never reaches 3 s. Of 199 attempts on three seeds, 18 were given
up for a stall; they lasted 3.6 to 15.6 s, 6.6 s at the median, three of them over 10 s. Attempts that end
standing last 4.9 s at the median, 6.6 s at the 95th percentile, and 2 of 151 over 8 s.

**The rule.** An attempt is also given up when its clip has waited `getup.wait_s` in all, counting only the time in
which nothing presses on the body (a body that a hand slows is not hurried). Chosen on three seeds:

| `getup.wait_s` | End standing | At the first attempt | Attempts a fall | Longest attempt, standing / given up |
| --- | --- | --- | --- | --- |
| 0 (no bound) | 50, 50, 49 | 38, 44, 45 | 1.33 | 12.0 s / 15.6 s |
| 5 | 49, 50, 49 | 38, 44, 44 | 1.36 | 7.7 s / 7.3 s |
| 4 | 49, 50, 49 | 38, 44, 44 | 1.38 | 7.2 s / 6.8 s |
| 3 | 50, 50, 49 | 35, 38, 43 | 1.43 | 6.0 s / 5.8 s |

5 is taken: every attempt over 8 s is gone, the first attempts are as they were, and 3 costs eleven of them. The
one fall it costs (seed 1, trial 36, at the table) stood before at the second attempt after a long wait in which
the body backed out from under the table; with the bound it is at its fourth or fifth attempt when the 40 s end.
That is the limit of the furniture that is already written down, not a new one, and one fall of 150 is not a
finding by itself.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m3_gate_wait_start`, the bound off (`wait_s` 0) | 63 of 63, the same hashes |
| ...with `wait_s` 5 | 64 of 64; the 63 have the same hashes (no get-up of the suite waits 5 s), one new |
| `getup_wait_bounded` (new: the pelvis of the lying body is pinned to the world, `stall_s` set to 30 by a console event) | given up at 12.56 s, "the clip waited 5.0 s in all", a second attempt at 14.78 s; with `-Tune "getup.wait_s=0"` one attempt and the scenario fails |
| The pressed stand, 300 N, `wait_s` 0 and 5 | 13, 23 and 16 of 50 stand while pressed with both; 143 and 144 of 150 stand in the end |
| Unit tests | 31 |

A scripted force is felt as a press (it is what the stand's `-PressN` uses), so the first guard, a force of 1500 N
on the pelvis, did not count as free waiting and passed with and without the rule; a pin is not felt. The
expectation `min_get_up_attempts` is new for it.

How it looks now: at the table the body tries, drops, tries by another clip, and stands at 36.0 s as before
(36.5), with no pose held for more than 5 s; on the floor the first attempt ends at 13.8 s instead of 15.1 s and
the raised legs are held for about 4 s still. The wait itself is as it was: a clip asks for a pose the body cannot
reach from where it lies, and the body holds the nearest thing to it. That stays a known limit.

**One hand on a body getting up: the gate's figure was stale.** The gate's table said that under 300 N two bodies
of three get up. That was measured in step 1 and not again after step 2 changed the body's friction. Measured now
(the stand with `-PressN 300`, three seeds, bodies that stand while they are pressed):

| Surface | Stand while pressed, of 150 | Stand in the end |
| --- | --- | --- |
| As accepted in step 2 (`surface.friction` 0.3: 0.5 on the floor) | 52 (13, 23, 16) | 143 |
| `surface.friction` 0.7 alone | 103 (33, 35, 35) | 145 |
| Every value as before step 2 | 96 (30, 35, 31), the figure of step 1 | 139 |

So it is the friction, not the restitution: on a floor that gives 0.5 a pressed body's hands and knees slide, and
one hand keeps two bodies of three down for as long as it presses, where the owner's rule is that one hand slows
a get-up. Not changed here: the friction is a measured property of the surface, the lying body's drag (400 N: one
hand does not move it, two do) rests on it, and the hand itself, its force cap and its mass are tuned again in M4
on recorded motions. It goes to M4 with the grab, to be met by the rule for a pressed body
(`getup.hold_force_n`, `pressed_*`), not by the surface.

Mistakes of mine here: the first guard scenario pressed with a scripted force and could not fail; the helper that
ran the suite, the tests and a stand in a row was started in the background without a first run in the foreground
(it worked). And a mistake of the earlier parts that this one found: a figure of the gate was carried over from a
step before the body's surface changed. The figures of a gate are measured at the gate.

Figures on the final build: suite 64 of 64 with `-Long`, unit tests 31, the stand 49, 50 and 49 of 50 with 38, 44
and 44 at the first attempt and 1.36 attempts a fall; the push matrix, the knockdowns and the session's self-test
are in the table of the M3 section of `docs/PRD.md`.

Changed: `SandboxBodyController` (`UpdateGetUp`: the bound), `SandboxBodyTuning` (`getup.wait_s`),
`Config/Tuning/body.json` and its `README.md`, `ScenarioSpec` and `ScenarioRunnerSubsystem`
(`min_get_up_attempts`), `Scenarios\getup_wait_bounded.json`, `Scenarios\README.md`.

### M4 part 1, step 1 (the hands recorded and replayed), 2026-11-24

What a session's hands are asked for can be kept and replayed by a scenario. Nothing of the body or of an existing
scenario changed: the 63 scenarios of the baseline `m3_gate_wait_start` have its hashes, `getup_wait_bounded` has
the hashes of the commit before, and one scenario is new (`replay_demo_hands`).

**What is built.** The rig adds the target each hand was given to a recording on every `UpdateTargets`
(`ASandboxEffectorRig::SetRecording`), so only frames on which the world steps are kept, whatever source the
targets came from. The pawn starts and ends a recording (the K key, `StartRecording`, `StopRecording`) and hands a
finished one on; it writes no file and knows no character. The session (`USandboxPlaySubsystem`, `-Record=`) writes
it with a scenario that replays it and with what it saw of the outcome. A recording is text, one line a frame, 17
significant digits (a unit test reads one back bit for bit); 240 frames of two hands are 49 kB, a minute about
1.1 MB. A scenario names a recording in place of `effector` events (`"recording"`, `"recording_start_s"`), and the
runner plays it back frame for frame through `FSandboxRecordedTargetSource`.

**The check** is `Tools\record-replay.ps1`: a session without a person uses the hands alone (`-PlayDemo`: at frame
60 the recording begins, the right hand reaches into the chest from frame 90 to 115, the left from 170 to 190, the
recording ends at frame 300), the recording is replayed as a scenario of the same length, and the replay is held
against what the session saw.

| | Session | Replay |
| --- | --- | --- |
| States of the body | standing, flinch, stagger, standing, flinch, standing, stagger | the same |
| The frames they began in | 0, 99, 110, 161, 182, 214, 267 | 0, 99, 110, 161, 181, 213, 266 |
| Pelvis at the end, cm from the spawn point | 3.59, -6.77, 84.32 | 3.64, -6.51, 84.41: 0.27 cm apart |
| Right hand: first touched, impulse, speed of arrival | `spine_04`, 40.55 N*s, 3.26 m/s | `spine_04`, 40.55 N*s, 3.26 m/s |
| Left hand | `spine_04`, 14.84 N*s, 3.09 m/s | `spine_04`, 15.26 N*s (2.8% more), 3.13 m/s |
| Two replays | | the same hashes |
| The session a second time | the same recording, byte for byte | |

Up to and through the first hand's contact the two runs agree in every figure shown (the flinch in frame 99, the
stagger in 110, standing again in 161, the impulse to four digits), which is what says that the recording's first
frame is put on the right step of the body's life: one frame off would move all of them. After it they drift
apart by a frame and a quarter of a centimetre. A session and a scenario are not the same run: a session's process
runs no warm-up scenario (the first run of a process differs bit for bit from later ones), its hands exist from
its first frame and are made in another order relative to the body. The limits of the check (a frame, 1 cm, 5%)
were set after this one measurement, on this one scene, and are close to what was measured; a recording of a
longer or rougher motion will need wider ones, and what the check is for is a recording that is wired wrongly, not
the last digit.

**The plan said "the same hashes" and that was wrong.** A run's hashes are of every frame of that run, and a
session is another run. Written down in `docs/PRD.md` as it is: a replay ends as its session did within stated
limits, and a replay repeats bit for bit.

**What a recording cannot do.** It is made of points of the world, so against a body that moves otherwise the
hands reach for where the session's body was, as a scripted hand does. The drag handle, the impulse tool and the
reset are not in it (they join in part 5); a recording made across a reset is written with a warning. Pause and
slow motion are no hindrance.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m3_gate_wait_start` | 65 of 65; 63 with the same hashes, 2 new (`getup_wait_bounded`, as committed; `replay_demo_hands`) |
| Unit tests | 32 (one new: a recording comes back bit for bit, and is played back frame for frame) |
| Session self-test | 14 of 14 |
| `replay_demo_hands` (the demo's recording kept under `Recordings\`, 7 s) | hands deliver 55.8 N*s, 0.006 cm into the body at most, no tunnelling, the body staggers twice and ends standing |

Changed: `SandboxTargetRecording` (new), `SandboxEffectorRig`, `SandboxPawn`, `SandboxInteraction.Build.cs` (Json),
`ScenarioSpec`, `ScenarioRunnerSubsystem`, `SandboxPlaySubsystem`, `InteractionTests`; `Tools\play.ps1` (`-Record`,
`-Demo`), `Tools\record-replay.ps1` (new), `Tools\bootstrap.ps1` (a twelfth step); `Recordings\demo_hands.json`,
`Scenarios\replay_demo_hands.json`, `Scenarios\README.md`.

### M4 part 1, step 2 (the hands' source for a headset), 2026-11-24

The third source of the hands' targets is built and tested on made-up poses, and was then run in the headset
for a first look. What follows first is what was known from the engine's source and from tests before the
headset was on; "In the headset" below is what the first sessions showed.

**What was read in the engine (5.8.4), not remembered.** `IMotionController::GetControllerOrientationAndPosition`
has an overload that returns the linear and angular velocity with the pose; the OpenXR plugin fills it from
`xrLocateSpace` with a velocity structure chained on, for the time the frame will be shown at
(`FOpenXRHMD::GetPoseForTime`). The motion sources `LeftGrip` and `RightGrip` are the grip poses. Position and
linear velocity come in Unreal's axes, scaled by the world-to-metres value passed in; the angular velocity comes
as an axis times rad/s and is negated on the way, because the change of axes is a reflection, so `v = w x r`
holds with the engine's cross product as it is. A camera component locked to the headset has its relative
transform set to the head's pose every frame, so its parent is the origin of tracking space. With no mapping
context given to it before the session begins, the plugin makes an action of every controller key and sends
them as key events (`BuildLegacyActions`); its list of interaction profiles has no PlayStation controller, so
which profile's keys SteamVR fills for the PS VR2 controllers is not known here.

**What is built.**

| Piece | What it does | Knows of a headset |
| --- | --- | --- |
| `ISandboxControllerTracker` | "Where is the controller of this hand": pose, velocities, squeeze, in tracking space | no |
| `FSandboxTrackedTargetSource` | Pose through the play space's origin into the world; the body at an offset from the grip pose; its velocity `v + w x r`; the grip; what was tracked, counted | no |
| `FSandboxXRControllers` | Answers the interface from the engine: motion controller devices for the pose, the player's input for the squeeze; puts the tracking origin on the floor | yes, the only file |
| `ASandboxPawn` | A root of its own with the camera under it. In a headset: the origin of the play space, level, no flying and no mouse; the tracked source in place of the desktop one | asks `IsHeadsetOn` once |
| `USandboxPlaySubsystem` | Puts the play space 80 cm from the character; ends a `-vr` session that has no headset; does not hold the frame rate in a headset, only counts it; writes each hand's tracking into the session file | asks whether stereo is on |
| `Tools\play.ps1 -VR` | Leaves the OpenXR plugin on, passes `-vr`, prints what was tracked afterwards | |

New tuning values, all in one build with their names: `effectors.hand.offset_thumb_cm`, `offset_palm_cm`,
`offset_knuckles_cm` (0, 0, 0), `tools.headset.stand_distance_cm` (80), `tools.headset.hands_height_cm` (100).

**The unit test** (`DictatorSandbox.Interaction.Source.TrackedHandsFollowControllers`) gives the source made-up
controllers: a pose 30 cm ahead and 20 to the right of a play space that faces +y lands where it should in the
world with its velocity turned; the same three offsets put the two hands at mirrored places; a controller that
turns on the spot at 2 rad/s moves a body 3.6 cm off its axis at 7.2 cm/s; a controller that flies and spins about
an axis that is none of the three moves its target, over a step of 0.1 ms, by the velocity the target reports
(which ties the sense of the angular velocity to the sense of the rotations); a runtime without velocities gets
the difference of two poses, not across a pause, and the frames are counted; a lost controller leaves its hand
where it was, at rest, and comes back without a velocity from before; the grip is held to 0..1. With the cross
product's arguments swapped the test fails in three places, so it can fail.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m4_p1_step1` (saved before the change) | 65 of 65, all with the same hashes |
| Unit tests | 33 (one new) |
| Session self-test, without a renderer and rendered | 14 of 14 both; the picture shows the hands in front of the camera as before |
| `Tools\record-replay.ps1 -Twice` | 10 of 10 comparisons, every figure as in step 1 |
| `Tools\play.ps1 -VR` with no headset on | `PLAY ERROR` after 37 s: "-vr was asked for and no headset is showing the world" |
| `Tools\play.ps1 -VR -Smoke` | refused: a headset session has a person in it |

**One thing moved, by a sign of zero.** The pawn's view is now the camera component's pose under a root, not the
actor's own. The recording of the session without a person differs from the one kept in `Recordings\` in 480
characters: the hands' rotation has `0` where it had `-0`. Both replay to the same hashes
(`22131048`, `8c4513fe`), and the session's figures are the ones of step 1 to the last digit shown. The kept
recording is left as it is.

**What is not known until the headset is on** (step 4 looks at each first; the log line of each hand's first
pose and the `tracking` block of the session file answer the first three):

- whether SteamVR gives the PS VR2 controllers' grip poses and both velocities through this path;
- whether the squeeze arrives, and under which profile's key (the largest of four is taken);
- whether the headset runs at the simulation's 90 frames a second: at 120 the world would run a third faster
  than the clock, and the session's log has the rate every 900 frames;
- whether the spheres are where the hands are (the three offsets), and whether 80 cm is where the owner wants to
  stand;
- whether the HUD's text shows in the headset; nothing depends on it.

**In the headset, the same evening** (the owner put it on for a first look of a few minutes; four launches).

| Launch | What happened |
| --- | --- |
| 1 | The engine crashed 7 s after its start, in `XR_APILAYER_MBUCCHIA_toolkit.dll` (OpenXR Toolkit, an API layer on this machine since January that hooks every OpenXR application), called from the D3D12 RHI. `Tools\common.ps1` now leaves the layer out of a `-VR` run by the variable its own manifest names (`DISABLE_XR_APILAYER_MBUCCHIA_toolkit`), for that process only. M1's headset runs had gone through with the layer in |
| 2 | Ran; closed by accident after a minute |
| 3 | 79 s: both controllers tracked with both velocities from the runtime, 89.7 frames a second; hits on the neck and the chest, a fall and a get-up in 4.9 s. **No recording:** `-Record` asked the pawn to record when the session adopted it, before the pawn had made its hands, and the refusal was silent. The path had never been run (step 1 checked the one without a person). The session now starts the recording once the hands exist and says so, or logs an error |
| 4 | 72 s, recorded from the first frame: `Saved\Recordings\vr_check` (6506 frames, 2.2 MB) |

What the fourth session shows:

| Question of the list above | Answer |
| --- | --- |
| Grip poses and both velocities through this path | Yes: 4292 frames each hand, none without a velocity |
| The squeeze, and under which key | Yes, as `OculusTouch_Left_Grip_Axis` and `OculusTouch_Right_Grip_Axis`. It reads 0 or 1 and nothing between: the grip of these controllers is a button. No trigger key was seen (not known whether one was pressed) |
| The headset's rate | 89.9 frames a second against the simulation's 90 |
| A replay against its session | **The same to the last digit shown:** the 36 states of the body in the same frames, the pelvis 0.000 cm apart after being pushed a metre, the hands' impulses 175.92 and 494.48 N*s in both. The session without a person drifts by a frame and a quarter of a centimetre; this recording begins on the frame the hands are made, as a scenario's hands are |

Three things the recording shows that the plan did not expect:

- **The runtime's velocity trails the motion of the runtime's own poses by about 38 ms.** Held against the
  difference of the recorded positions, the recorded velocity fits best when the positions are taken 3.4 to 3.5
  frames earlier, and is then still smaller (slope 0.73 to 0.78; correlation 0.92 to 0.94 along x and y, 0.76
  to 0.84 along z; the axes and signs are right). Read as a pose extrapolated with its velocity, the lead is
  47 ms. So the pose is predicted for the time the frame is shown at and the velocity is not: the spring's damper
  is given a velocity that is late for the target it is given. "The velocity from the tracking rather than from
  the difference of two poses" was the plan's premise; whether the hand follows better with the one or the other
  is now a measurement, and step 3 makes it on this recording.
- **A controller out of the cameras' sight is not reported as lost.** From 62 s the right controller's pose
  wandered a metre and a half over five seconds, up to 199 cm above the floor, with a velocity of 0.01 m/s, and
  at 67.9 s jumped 116 cm in one frame to where the hand was. No frame counted as lost, the hand went after the
  jump at 13.1 m/s with the spring asking for 3437 N, and nobody had moved. Following a pose that is given but
  not marked as tracked was my decision of the afternoon, and it is wrong for this headset.
- **The two hands touched each other first** (26.7 N*s between them over the session): they are two bodies
  that collide, as designed, and a person brings them together without meaning to.

Also seen: the hands stood 22 and 25 cm from the controllers when these were first tracked, 24.6 s into the
session (they were asleep), and went there on their springs.

Mistakes of mine in these launches: after the first one I told the owner the session was running when it had
crashed 7 s in; I had looked for the line of its start and not at whether the process lived. `Tools\play.ps1`
reported that crash as "PLAY ENDED"; it now reports a crash as `PLAY ERROR` with the first lines of the stack,
and says so when `-Record` was asked for and nothing was written. A session can end by itself
(`-Seconds`, `-PlaySeconds=`), which is also how the recording from the first frame is now checked without a
person (3 s on the desktop: 270 frames, replayed to the same figures).

Mistakes of mine before the headset: the first unit test of the turning term asked for 0.01 cm/s where the arm's own turn
changes the answer by 0.09, and failed on its first run; a helper script was first written as a heredoc and broke
on an apostrophe, the pitfall that is written down.

Changed: `SandboxTrackedTargetSource` and `SandboxXRControllers` (new), `SandboxPawn`, `SandboxInteractionTuning`,
`SandboxEffectorRig.h` (a comment), `SandboxInteraction.Build.cs` (HeadMountedDisplay), `InteractionTests`,
`SandboxPlaySubsystem`; `Config/Tuning/effectors.json`, `tools.json` and `README.md`; `Tools\play.ps1` (`-VR`,
`-Seconds`, a crash and a missing recording reported), `Tools\common.ps1` (the API layer left out of `-VR` runs);
`Scenarios\README.md`.

### M4 part 1, after the first look in the headset, and step 3 (the hand against its target), 2026-11-24

**What the owner said of the first look** (asked one question at a time): the spheres sit on the fists, no offset
seen; the place is right, in front of the character at arm's length, the floor where the floor is; the spheres
follow the hands with no lag or tremble to the eye (the fastest swing was 3.9 m/s); both triggers were pressed
and neither reached the game, though the grips did; the HUD's text is drawn across both eyes, the left eye sees
the lines' beginnings and the right eye their ends, and cannot be read. The right controller's wandering pose at
the end of the session was the controller laid on a table and the headset taken off: no fault during play, and a
hand behind the back has not been tried yet.

**Changed for it, on the owner's decisions of the same evening.**

- *No text in the headset* (`ASandboxHUD` draws nothing there). The window on the monitor mirrors an eye, so it
  has no text either; what a session has to say is printed when it ends.
- *A controller the headset does not see leaves its hand where it was.* Only a pose the runtime marks as tracked
  is followed (`GetControllerTrackingStatus`); a pose given without that mark is counted (`estimated_frames`).
  Whether the runtime sets the mark as hoped is not known: the first session did not record it. For that the
  source also counts tracked frames in which the pose moved faster than 20 cm/s with a velocity under 3 cm/s
  (`moved_without_velocity_frames`), which is what the untracked pose looked like; on the recording that sign
  begins at 62.0 s, when the headset came off, and also marks 6 to 18 frames during play, so nothing acts on it.
- *A target does not jump.* Found again, tracked for the first time away from where the hand waits, or moved
  further in a frame than a hand moves (`tools.headset.max_hand_speed_mps`, 20), the controller is caught up
  with at `tools.headset.catch_up_speed_mps` (1.5) on top of its own speed. A velocity is not differenced
  across such a jump.
- *Every controller key that reaches the player's input is named at the end of a session* (the first version
  looked at grips and triggers only), for the trigger that did not arrive and for choosing a button.
- A run with hands writes `hands.csv`: each hand's gap to its target, its speed, the target's reported speed,
  the force the spring law asked for and the contact force, frame by frame. Read only: the suite's hashes are
  the baseline's.

**Step 3: how far the hand is behind its target** (`Tools\swing-lag.ps1`; the hand as it is, 1.5 kg on 3000 N/m
with a cap of 300 N). The right hand's target travels 60 cm with a minimum-jerk profile; the spring is told the
target's velocity in four ways. The gap is hand to target at the start of each step, the largest during the swing:

| Peak speed | exact velocity | difference of two poses | as the runtime gave it (38 ms late, 0.78) | none |
| --- | --- | --- | --- | --- |
| 2 m/s | 1.9 cm | 2.1 cm | 4.6 cm | 10.4 cm |
| 4 m/s | 4.5 cm | 5.3 cm | 11.6 cm | 19.3 cm |
| 6 m/s | 7.4 cm | 8.9 cm | 19.0 cm | 26.4 cm |
| 8 m/s | 10.5 cm | 13.1 cm | 26.6 cm | 32.1 cm |

After the target has stopped the hand is within 1 cm of it in 0.02 to 0.08 s with the exact velocity, 0.02 to
0.09 s with the difference, 0.02 to 0.14 s with the runtime's; it is furthest off by 0.2 to 3.0, 0.4 to 5.0 and
1.0 to 10.1 cm. The spring law asks for 67, 189, 365 and 581 N at the four speeds, more than its cap of 300 N
from 6 m/s, but only for a frame or two of a swing: the sweep further down shows that the cap is not what
holds a free hand back.

- **With the true velocity the gap is one frame of the target's travel** (2.2, 4.4, 6.7 and 8.9 cm at 90 frames a
  second) and little else. The hand is given the place for the start of the coming step and arrives there at
  its end. That frame is the floor of this design; a target led by its velocity over one step would take it
  out, and is a choice for part 2.
- **The difference of two poses is nearly as good as the truth, and the velocity as the runtime gives it is
  twice as bad.**

The same on the owner's recording (`vr_check`, 26 to 61 s, frames in which the hand touches nothing; the mean gap
by how fast the target moved):

| Velocity given to the spring | under 0.5 m/s | 0.5 to 1 | 1 to 2 | 2 to 3 | over 3 m/s | 95th percentile | largest |
| --- | --- | --- | --- | --- | --- | --- | --- |
| The runtime's, as recorded | 0.49 cm | 1.14 cm | 2.27 cm | 5.75 cm | 8.24 cm | 2.17 cm | 10.9 cm |
| The difference of the recorded positions | 0.23 cm | 0.71 cm | 1.40 cm | 3.18 cm | 4.72 cm | 1.30 cm | 5.9 cm |
| None | 1.13 cm | 3.53 cm | 5.86 cm | 10.95 cm | 14.06 cm | 5.71 cm | 17.5 cm |

About 5100 to 5600 frames each; 35 to 44 of them over 3 m/s, so the last column of speeds is thin. The three
replays part after the first contact, so they are not the same frames. Not measured: whether the difference of
predicted poses makes the hand tremble more (the owner saw no tremble with the runtime's velocity).

**What it means for the plan.** The owner's order for part 1 was "the hand's velocity from the tracking rather
than from the difference of two poses". On this headset the tracking's velocity trails its own poses, and the
difference follows them: a hand behind a fast target by 4.7 cm instead of 8.2. The source can take either or a
mix (`tools.headset.runtime_velocity_share`; 1, the plan's, until the owner decides).

**Starting point for part 2** (the force cap and the mass are tuned there): at a punch's 8 m/s the hand is 10 to
13 cm behind its target with a good velocity, nearly all of it the one frame. The recordings of step 4 are put
through the same tool.

**The cap, the mass and the stiffness hardly move a free hand** (the night of 24 November; the same swings, the
largest gap in cm at 2, 4, 6 and 8 m/s, with the exact velocity / with the difference of poses):

| Hand | 2 m/s | 4 m/s | 6 m/s | 8 m/s | Spring asked for, at 8 m/s |
| --- | --- | --- | --- | --- | --- |
| As it is: 1.5 kg, 3000 N/m, cap 300 N | 1.9 / 2.1 | 4.5 / 5.3 | 7.4 / 8.9 | 10.5 / 13.1 | 581 N |
| Cap 450, 600 or 900 N | 1.9 / 2.1 | 4.5 / 5.3 | 7.4 / 8.9 | 10.3 / 12.7 | 570 N |
| 1.0 kg (damping 0.0365 s) | 1.8 / 1.9 | 4.1 / 4.7 | 6.6 / 8.0 | 9.1 / 11.3 | 465 N |
| 1.0 kg, cap 450 N | 1.8 / 1.9 | 4.1 / 4.7 | 6.6 / 8.0 | 9.1 / 11.3 | 465 N |
| 6000 N/m (damping 0.032 s) | 1.8 / 1.9 | 3.8 / 4.4 | 6.1 / 7.4 | 9.1 / 11.4 | 882 N |
| 6000 N/m, cap 600 N | 1.8 / 1.9 | 3.8 / 4.4 | 6.1 / 7.4 | 8.5 / 10.4 | 823 N |

Three times the cap takes 0.2 cm off the gap at 8 m/s; a third less mass or twice the stiffness take about a
centimetre. In free air the hand's lag is the frame, not the hand. I had written "from 6 m/s the cap is what
holds the hand back" from the force the spring law asked for, without running the sweep: the law asks for more
than the cap for a frame or two of a swing, and that costs nothing to speak of. What the cap and the mass do
decide is contact: what a strike delivers and how hard a hand pushes. Part 2 tunes them on that.

| Check | Result |
| --- | --- |
| Suite with `-Long` against `m4_p1_step1` | 65 of 65, the same hashes (`hands.csv` is only read) |
| Unit tests | 33; the tracked source's test now also holds the lost pose, the catching up, a punch at 12 m/s that is not a jump, the waiting pose and the share of the runtime's velocity |
| Session self-test, `record-replay.ps1 -Twice`, `record-replay.ps1 -Recorded vr_check` | 14 of 14; 10 of 10; 9 of 9 |

Not run in the headset: the lost-pose rule, the catching up and the list of keys. The next launch there is a
minute of pressing every button, a hand behind the back, and taking the headset off with the session running.

Mistakes of mine here: the catch-up's first version let a differenced velocity of 104 m/s across a jump carry
the target over the jump in one step (the unit test caught it); the test's count of frames was off by one; a
string with a line break in it went into C++ through a heredoc, the written pitfall once more.

Changed: `SandboxTrackedTargetSource`, `SandboxXRControllers`, `SandboxPawn`, `SandboxPlayerController` (the
HUD), `SandboxInteractionTuning`, `InteractionTests`, `SandboxPlaySubsystem`, `ScenarioRunnerSubsystem`
(`hands.csv`); `Config/Tuning/tools.json` and `README.md`; `Tools\swing-lag.ps1` (new); `Scenarios\README.md`.

### M4 part 1, the night of 24 to 25 November (the owner away; decided by the agent on the data and the plan)

**The target's velocity comes from the poses** (`tools.headset.runtime_velocity_share` 0). The owner chose it for
the next launch, and the one thing that could speak against it was measured on the recording: whether a hand
told the difference of predicted poses trembles more. In the frames in which the controller moves under 5 cm/s
and the hand touches nothing (about 100 a hand: a person seldom holds still), the hand's speed is 3.1 and 3.3
cm/s on average with the difference of poses and 3.7 and 3.8 with the runtime's velocity (95th percentile 4.7
and 4.8 against 7.3 and 6.5); its speed changes by 2.6 and 1.8 m/s2 a frame at the 95th percentile against 2.1
and 1.3. No more tremble, then, on thin data; the owner's eye in the headset has the last word.

**A new take** (the owner's "a button that puts the character back and starts a new recording"). Which buttons
reach the game is not known (the triggers did not), so the take is asked for by what is known to arrive: both
grips held closed for a second with both hands raised above the eyes (`tools.headset.new_take_hold_s`,
`new_take_raise_cm`; closed grips alone would begin takes by themselves, since a person who punches holds both
closed); the controllers buzz when it begins. On the
desktop it is the N key. A take ends the recording that runs, puts the character back, makes the hands anew on
their targets and begins a recording of its own (`<name>`, `<name>_2`, ...), so that each recording starts with
a fresh body and replays as a scenario of its own.

What it took to make a take begin as a scenario begins:

- *The reset is carried out after the frame's physics* (`USandboxPlaySubsystem::RequestReset`, also for the R
  key). Put into the world in the middle of a frame, the character had half a frame of its own.
- *The hands are made anew* (`ASandboxEffectorRig::RemakeEffectors`), after the character, as a scenario's are.
- *What a take's session file says of the hands is the take's*, not the session's so far.

And how close it comes (`Tools\record-replay.ps1 -Takes`: three takes without a person, a touch of the chest a
second into each, left hand, right hand, left hand):

| Take | Session | Replay |
| --- | --- | --- |
| 1 (left hand) | flinch in frame 107, 22.38 N*s | frame 108, 21.79 N*s: 2.6% off, pelvis 0.55 cm apart |
| 2 (right hand) | frame 108, 21.90 N*s | frame 108, 21.84 N*s: 0.2% off, 0.71 cm |
| 3 (left hand) | frame 107, 22.37 N*s | frame 108, 21.79 N*s: 2.6% off, 0.39 cm |

Within the check's limits (a frame, 1 cm, 5%) and not to the digit, where the recording of a session's first
body, from its first frame, replays to the last digit (`vr_check`). It is not the hand: swapped, the odd takes
are off again and the even one is not, and the hand arrives at 3.25511 m/s in the session and in the replay. It
is the body: three frames into its life the pelvis of an odd take's body stands 0.03 cm from where an even
take's stands, and standing turns that into half a centimetre within a second, before anything touches it.
What makes every second body of a world differ was not found (the order in which the solver takes bodies that
are put into a world with a history is the guess; the alternation fits freed handles being used again). A shove
instead of a touch shows what it can do: the session's body took three steps where the replay's took one, on
the 4 cm that decide a second step. So a take's replay is a run of its own that begins as the take began: its
scenario's expectations are written from the replay, as for every recording.

**Toward part 2, without the headset** (the same night; nothing of the body's or the hands' physics changed:
the suite's 65 scenarios have the baseline's hashes after each of these).

- *The hand has a frame of its own,* whatever moves it: x along the forearm toward the knuckles and the fingers,
  z toward the thumb, y through the palm. A tracked hand is turned into it from the grip pose
  (`effectors.hand.grip_pitch_deg` -90, `grip_yaw_deg` 0, `grip_roll_deg` 0: by OpenXR's definition of the grip
  pose the fingers lie along its -z and the thumb along its x). Whether PS VR2's driver keeps to that definition
  is not known, which is why the three angles are tuning.
- *A hand is a fist or open, by its grip* (`effectors.hand.fist_grip` 0.5, with 0.1 to either side before it
  changes). Desktop and scripted hands are fists, the balls they have always been. An open hand shows a flat
  plate, 18 by 9 by 3 cm (`effectors.hand.palm_*`), in the hand's frame. **The plate collides with nothing:** it
  is there so that the owner can say, at the next launch, whether the palm faces the way the real palm does,
  before the plate becomes a shape. The shape itself was not built tonight: its orientation cannot be checked
  without the headset, and the runner's measures of a hand's penetration and tunnelling take the hand for a
  ball.
- *The controllers vibrate with what their hands press or strike with:* in proportion to the hand's own contact
  force, full at `tools.headset.haptic_full_n` (300 N, the spring's cap: a hand held against something is at full
  strength, a blow goes beyond it for a frame). From the hand's contacts, not from the body: the pawn knows
  nothing of what a hand touches. Not felt by anybody yet.
- *The body tells what happens to it* (`USandboxBodyController::OnBodyEvent`, `GetEvents`): every hit and every
  change of state, for sound, a face, a health system, a result file. A hit is told in every state, once a
  contact, when it has brought what makes a standing body flinch; a new hit is a contact after nothing was felt
  for the flinch's window, or a blow that lands while something presses already. A run's `control.events` lists
  them and `control.hit_events` counts the hits; the expectation `hit_events` holds the count (set in six
  scenarios: one slap is one hit, four punches four, a held force none, a blow on a body that gets up and
  another on it getting up two).

How the rule for a hit was found, since the first two were wrong: counted as the flinch counts, with the sum
set back at each telling, a shove of 200 N*s over nine frames was nine hits and a held hand eighteen; counted
once a stretch of rising force, a hand held at the chest was six hits in six seconds, because a hand presses
now more and now less. Once a contact, the hand is three: it lands, and the body steps away from it twice and
meets it again.

**A report on recordings** (`Toolsecording-report.ps1 -Name "takes*"`): every recording that matches is replayed
and read. On the owner's first recording it finds 13 hits in 72 s: the hand arriving at 0.3 to 4.1 m/s, 432 to 648 N
in the hardest frame of a strike (the spring's cap is 300 N: a moving hand gives up its momentum too), the neck
struck eight times, the chest four, the upper arm twice. It was this list that showed one strike told as two
hits: its force rose over two frames, the sum passed the threshold in the first and the second was a blow by
itself. A blow into a contact now counts only after the force of the hit told before it has stopped rising.
The ten minutes of step 4 are read by the same tool, take by take.

**The open hand as a body** (later the same night; behind `effectors.hand.palm_collides`, which is 0: nothing
of today changes until it is switched on).

Of the three ways to give one hand two shapes, the one taken has only one body in the world at a time and
leaves the fist's code untouched: the open hand is a second body, a box of the plate's size
(`USandboxPalmComponent`), made the first time a hand opens. When the grip opens it takes the ball's pose and
motion, its own spring (held at the point the ball's centre was) and its own mass (`palm_mass_kg`, 1.5 as the
ball's until part 2 tunes it); the ball stops colliding and being simulated, and follows as a marker. When the
grip closes the ball takes the hand back the same way. A hand that never opens never has the second body, so
every scenario of before has its hashes: 65 of 65. Whoever wants the hand asks the hand
(`GetHandTransform`, `GetHandVelocity`, `GetProbe`), not the component; the runner's measures of penetration
and tunnelling take the hand's own shape, with half the plate's thickness (1.5 cm) where the ball has its
radius. A scripted hand's keys say `"grip"` (1 a fist, the default; 0 open), and a turn puts the palm where it
is wanted.

Four scenarios with an open hand, in the suite:

| Scenario | What happens |
| --- | --- |
| `palm_slap_head` | The open left hand, palm leading, on the side of the head at 6.2 m/s: 7.2 N*s to the neck, one hit, a flinch, 0.00 cm in the body |
| `palm_slap_head_edge` | The same with the plate's thin edge leading: 10.2 N*s, 0.02 cm |
| `palm_push_chest_both` | Both open hands pushed into the chest and held: 30 and 33 N*s, 355 N at most, the body steps back and stands |
| `palm_open_close` | A hand that opens and closes in the air and then strikes the chest as a fist at 6.8 m/s: two changes of body, 15.5 N*s |

Tunnelling, as the ball was tried in M2: slaps on the head, the chest and the forearm, palm leading and edge
leading, with the target's speed raised and the force cap at 6000 N so that the hand can follow (30 runs; the
hand's speed is what it reached):

| Hand's speed | Deepest in the body | Tunnelled |
| --- | --- | --- |
| 6.1 m/s | 0.24 cm (chest, palm leading) | no |
| 10.0 m/s | 0.03 cm | no |
| 13.4 to 14.8 m/s | 0.03 cm | no |
| 17.8 to 19.6 m/s | 0.01 cm | no |

The measure follows the plate's middle, so it cannot see an edge that passes a limb: from 17.8 m/s the slap
aimed at the forearm with the palm leading touched the trunk first and never the forearm, where at 6 to 13.5
m/s it touched the forearm. That is twice a punch's speed; noted, not chased.

**A hand changes its body only where the other one has room.** The ball and the plate have other shapes: a
ball put where a plate lies flat on a chest would be 3 cm inside the chest and be thrown out of it, and the
fingers of a plate put where a fist rests against something would be 9 cm inside it. Before a change the hand
asks the world whether the other body, put where the hand is, would lie in anything
(`USandboxEffectorComponent::WouldOverlap`, a scene query); if it would, the hand keeps the body it has and
changes when there is room. A grip that closes while the open hand lies on something is counted
(`grips_on_body`, with `last_gripped`): that hand is not making a fist, and it is what part 3's grab starts
from. `palm_close_on_chest` holds it: the open hand pushed into the chest closes its grip there, stays a plate
(0.01 cm in the body at most), and is a fist once it has gone back.

Three more scenarios: `palm_close_on_chest` (above), `palm_slap_leg` (the open hand on the outside of the
thigh at 6.2 m/s; it meets the character's own hand first, as the ball's slap does, 23.6 N*s) and
`strike_punch_shoulder` (a fist from the front on the left collarbone at 8.7 m/s: 7.2 N*s, one hit, a flinch;
aimed 6 cm higher it only grazed, 4.1 N*s and no flinch).

**The owner's recording with open hands as bodies.** The recording of the first session was made before the
hand had a frame of its own, so a copy of it has its rotations turned by the tuning's quarter turn; replayed
with `palm_collides` on (`Tools\recording-report.ps1 -Name vr_check_plates -Tune ...`): 72 s of a person's
wrists, 14 changes between ball and plate (three squeezes a hand), 12 hits, no frame of tunnelling, 0.12 cm in
the body at most, the body on its feet at the end. The plates reach further than the balls did and land two
blows the balls missed. It is another run than the session, as it must be; what it shows is that a plate on
its spring is not thrown about by a real wrist. It assumes the quarter turn is right.

**A recording replayed with another hand is another fight after its first contact.** The owner's recording with
the force cap at 450 and 600 N and with a hand of 1.0 and 2.5 kg: 12 to 18 hits where the session had 13, the
hands' totals anywhere from 176 to 957 N*s, one run with a fall. The targets are points of the world; once the
body has gone another way the hands press on whatever is there now. So the hand's values are tuned on the first
contact of each take, which is what the takes of step 4 are for (one kind of strike a take, a fresh body each),
and not on a long recording's totals.

**The triggers that did not arrive: what SteamVR's own files say.** For the session SteamVR made a binding from
the engine's suggested bindings for the Touch profile and "auto-remapped" it to `playstation_vr2_sense`
(`Steam\config\openxr\system.generated.openxr.dictatorsandbox.unrealeditor-cmd.exe_oculus_touch_1_binding.json`).
In it the trigger is bound as the grip is: `/user/hand/left/input/trigger` pull to `oculustouch_left_trigger_axis`,
click and touch beside it; so are the sticks and the four face buttons. The controller's render model names L2
and R2 as its triggers and L1 and R1 as its grip buttons. So the runtime had the triggers bound, and the reason
they were not seen lies after it (the engine's key events, the window's focus at that moment) or in which
buttons were pressed. The next launch lists every controller key that arrives, which settles it.

On pictures of the bare scenario stage the plate is black on black (a flat face turned from the one light), so
the overlay's hands layer now draws an open hand's plate as its edges; the slap's picture shows it upright
beside the head, flat toward it.

Not known without the headset, and the reason the switch is off: whether the plate lies in the real palm
(the three angles), how an open hand feels against the body, and whether a plate on a spring that can also
turn (20 N*m) behaves when a person's wrist does what a script's does not.

**A second pair of eyes on the night's code.** With 41 files changed and nobody watching, the whole diff was
read by an agent that had not written it and was given the code, not my account of it. It found no crash, no
object used after its end in any order of destruction it traced, and the body's flinch logic untouched by the
events; and these, all checked against the code and the engine's source before they were fixed:

| Finding | What was done |
| --- | --- |
| The engine makes a vibration last one frame (`FOpenXRInput::SetHapticFeedbackValues` gives it the frame's own duration), and the pawn asked for one only when the strength changed: the buzz of a new take would have been an 11 ms tick, and a hand held at full force would have fallen silent after its first frame. The frequency of 1 was handed on as 1 Hz | A vibration is asked for in every frame it is to go on, and once more to stop it; the frequency is left to the runtime (0, OpenXR's "unspecified") |
| With the open hand a body: when the grip closes on something the hand stays a plate, but the plate was hidden because the grip was closed and the ball was hidden because the plate was the body. Nothing was drawn for as long as the player squeezed | What is seen follows the body that is the hand: the plate is shown while the hand is open or the plate is the body |
| The guard on a headset run looked for SteamVR's processes, not for a compositor that has finished coming up, which is the very thing that crashed the driver | It also refuses while the compositor is younger than 30 s, and while SteamVR's own log says anything but ready or standby |
| `play.ps1` took any call stack in the log for a crash; an ensure that the engine survives prints one too | A crash is a fatal error; a call stack is only quoted then |
| A new take waited for good for a reset that nobody carries out, in a session without the play subsystem | Without a listener the take begins as things are |
| Three constants of behaviour in code against the rule: the grip's slack, the gesture's grip threshold, the take's buzz | `effectors.hand.fist_grip_slack`, the gesture reads `fist_grip`, `tools.headset.take_buzz_s` and `take_buzz_strength` |
| A hand's `final_location_cm` was the marker ball's, a step stale, while the plate was the body | It is the hand's |

The reader also saw that a lost controller's target, found again, caught up in its place only and took the
controller's turn at once; the turn now goes the same share of its way in every frame and arrives with the
place. The grip is the controller's at once, as a person's squeeze should be.

**Before the next launch: a loss must not freeze a punch.** The rule of the evening leaves a hand where it was
as soon as the runtime stops marking its controller as tracked. Whether this headset drops that mark for a few
frames in a fast swing, when the hand leaves the cameras' sight for a moment, is not known: the first session
did not record it. If it does, the hand would stop in the middle of a punch and come after it at walking pace.
So a pose the runtime only guesses is followed for 0.15 s after the controller was last tracked
(`tools.headset.estimated_follow_s`; over such a time a gyroscope carries a pose well) and counts as a loss
after that, which still stops the wandering by a metre that took five seconds. The session's file counts both
(`estimated_followed_frames`, `lost_frames`), and the next launch says which of the two this headset needs.

Mistakes of mine on the way: a take's first version put the character back in the middle of a frame; the
session file of a take first carried the hands' figures of the whole session; I took the difference for the
hands' order of creation and for the first seconds of a process, and neither was it (a wait of 300 frames
changed no digit).

### M4 part 3, step 1 (the grab), the night of 24 to 25 November, behind a switch that is off

Built by the agent in the owner's absence, on the plan's own words ("a grip that closes on a body grabs it; one
that closes in the air is a fist"; "grab creates a constraint between the effector and the touched NPC body; the
force cap is what makes an 80 kg body heavy to lift and drag"). It is off (`effectors.hand.grab` 0) and every
scenario of before keeps its hashes (73 of 73 against the baseline `m4_p2_night`; the 82 with the grab's own are kept as `m4_p3_grab`, and a second run repeats all 82); eight scenarios `grab_*`
switch it on for themselves (nine with `grab_two_hands_one_arm`, added after the code was read by another). With it switched on for the whole suite, one scenario of the 80 changes, the one
whose grip closes on the chest (`palm_close_on_chest`). The owner's recording from the headset, replayed with
the grab on, has no grab in it and ends on the same hashes: fists that close in the air stay fists.

**What a grab is.** A joint (`FSandboxGrabJoint`) that keeps the hand's own point and one simulated body
together, both free to turn about it: a hand without fingers holds a place, not a direction. The joint has no
force cap of its own; the hand follows its target with its capped spring (300 N) and what it holds follows the
hand. A hand that holds something collides with no simulated body, and collides again once it has let go and
lies no deeper in one than `grab_clear_cm` (1). A grip takes hold when it closes (the grip passes `fist_grip` upward) with a simulated body within
`grab_reach_cm` (3) of the hand's shape, and takes the nearest. It lets go when the grip opens, when the hand has
been further than `grab_slip_cm` (45) from its target for `grab_slip_s` (0.25: the grip slips), when the joint
has carried more than `grab_max_force_n` (450) for `grab_overload_s` (0.1: the grip is torn open), and when what
it held is gone. The controllers vibrate with the force the hand pulls with, as they do with the force it
presses with.

**Three things the first version got wrong, each found by a run.**

- *Touch as the condition.* The first rule was "the hand touched the body within the last 0.1 s". The head
  could not be taken: the neck is soft, the head leans back from the hand that arrives, and at the moment the
  grip closed nothing was touching. A player in a headset has the same trouble from the other side (nothing
  tells a hand that it touches). The rule is reach now, asked of the world when the grip closes.
- *A skeletal mesh answers an overlap query as one thing.* The item index of the result, which the engine's
  comment says is the body's index, was -1; each body of the mesh is asked by itself
  (`FBodyInstance::OverlapTest`).
- *A hand that holds one segment of the trunk was caught against the next.* Jointed to `spine_02` and pulled
  toward the head, the hand's ball pressed against `spine_04`, and the joint carried 556 N of a 300 N hand
  until the overload rule tore the grip open, within a second. A hand that holds something now collides with
  no simulated body (it holds a point; what it holds does not move against it anyway) and collides again once
  it has let go and lies no deeper than a centimetre in one (in none, at first: a hand that had let go then
  sank into what it had held, toward a target a little way inside it). After that the joint carries the spring's force and no more: 294 N at most on
  average over a pull, with single frames up to 479 N (the hand's own 1.5 kg thrown about by a head that whips).

**The joint's force, and its unit.** `FConstraintInstance::GetConstraintForce` gives the joint's impulse over
the last substep, not a force (and not the "force times step squared" a comment in the solver suggests):
a hand that drags at a steady 231 N of its spring reads 124 kg*cm/s at 180 Hz, which is 223 N, the rest being
8 N of contact. `FSandboxGrabJoint::GetForceN` divides by the substep.

**The plan's row "grab and drag": lift an arm, pull a standing NPC, drag a fallen NPC 2 m with one and two
hands; joint stretch under 3 cm, hand force at its cap, no blow-up.**

| Scenario | What happens | Grab's joint | Body's joints |
| --- | --- | --- | --- |
| `grab_lift_arm` | The left forearm is taken from the side and lifted forward and up by 40 cm; the body stands, the arm follows and hangs again when it is let go | 0.003 cm, 141 N | 0.01 cm |
| `grab_palm_lift_arm` | The same with the open hand as a plate (`palm_collides`): the plate takes hold and stays the body while it holds | 0.003 cm, 86 N | 0.04 cm |
| `grab_pull_standing` | The forearm pulled to the side by 90 cm at 0.9 m/s: the body comes along in five steps, arms out, and stands 71 cm from where it stood | 0.005 cm, 314 N | 0.06 cm |
| `grab_drag_fallen_two` | A body on its back, two hands on the chest, pulled head first by 2 m in 5 s: it is dragged the 2 m, limbs trailing; each hand pulls with 196 and 171 N on average | 0.000 cm, 227 N | 0.06 cm |
| `grab_drag_fallen_one` | The same with one hand: the body moves 6 cm, the hand is left behind and its grip slips after 1.9 s | 0.000 cm, 303 N | 0.06 cm |
| `grab_getup_held` | A body that is to get up, held by the chest from before it begins for 6.9 s: one attempt is given up, it stands 2 s after the hand lets go | 310 N | 0.06 cm |
| `grab_overload_tears` | The two-handed drag with grips that may carry 150 N: both are torn open as the body begins to resist | 188 N | 0.06 cm |
| `grab_in_air` | A grip that closes 13 cm from the forearm takes hold of nothing | none | 0.00 cm |
| `grab_two_hands_one_arm` | Two hands on one forearm; the lower comes up into the upper one's place, the upper lets go there and leaves: it collides again only once it is clear of the other hand, and neither goes faster than 0.9 m/s | 205 N | 0.01 cm |

So M3's figure holds with a grab as it did with a scripted force: a lying body is dragged from about 400 N, one
hand does not move it and two do.

**`Tools\grab-pulls.ps1`: 93 pulls.** Seven bodies (forearm, hand, upper arm, head, chest, thigh, calf), five
directions, the hand's target leaving at 1, 2 and 4 m/s for 60 cm and staying there 1.5 s. Where each body's
surface is, the tool measures first (a hand of 20 N sent to the body's middle shows where it comes to rest),
so the pulls do not depend on where a change to the body puts an arm. 93 of 93 within the limits: the grab's
joint stretched 0.020 cm at most, the body's joints 0.16 cm, 17 J gained at most (the one fall), no body faster
than 5.6 m/s. What the body did: pulled by an arm, the head or the chest sideways, forward or back it comes
along in 1 to 6 steps and stands (one fall of 93: the upper arm pulled back at 4 m/s); an arm pulled up is
lifted; nothing pulled straight down or up by the trunk moves an 80 kg body, and the grip slips (43 of 93).

Two more rows of 31 pulls each, both within the limits: with the target leaving at 8 m/s (one fall, the same
upper arm pulled back; 442 N in the worst frame), and at 2 m/s with a grip that never slips
(`-Tune "effectors.hand.grab_slip_cm=0"`: the hand stays behind at its 300 N for the whole 1.5 s, nobody falls,
334 N in the worst frame). So the slip is a matter of where the player's hand is, not of the body's safety.

The reach, on 21 pulls (seven bodies, three directions, 1 m/s): with a reach of 0 cm nine take hold of nothing
(the upper arm, the head and the chest, which give way to the hand that comes), with 1 cm three (the chest),
with 3 cm and with 6 cm none, and no pull takes another body than the one it was sent to. 3 cm is the
smallest of these that takes every body.

**Open, written down for the owner and for part 3 proper.**

- *A standing leg does not come.* A thigh or a calf pulled by one hand in any direction stays where it is and
  the grip slips; the body does not step. During a pull of 276 N forward on the thigh the centre of mass is
  8 cm off the support that touches for a moment and the assist carries up to 0.26 of body weight. A person
  would hop. It is the body's side (what the assist may hold while a force is kept up), not the grab's.
- *The body does not feel a grab.* It feels contacts; a pull through a joint reaches it only as motion. So it
  does not lean into a pull as it leans into a push, and a get-up under a holding hand is not "pressed"
  (`getup.hold_force_n`). The first half of the remedy is built: the run and the session, which know both
  sides, tell the body every frame which of its parts the hands hold and with what force
  (`FScenarioStage::TellBodyOfHolds`, `USandboxBodyController::TellHolds`), and the body tells its listeners
  of a grab and of its end (`control.events`: `grab`, `release`; the expectation `grab_events`). The body does
  nothing else with it yet: no force, no target and no decision reads the holds, and every hash is as it was. A unit test
  holds the telling (`DictatorSandbox.Body.Events.HoldsAreToldAsGrabAndRelease`; 34 unit tests).
  What it should do with them (lean, count a hold as a press on a get-up) changes how the body behaves and is
  left for the owner to see first.
- *The stand itself* is as it was at M3's gate at the end of this night: 49, 50 and 49 of 50 on three seeds,
  38, 44 and 44 at the first attempt.

**One hand slows a get-up, two hold it: measured with hands, on the stand, on three seeds.** M3 left this rule
to be set again with the grab; it was last measured with a scripted force on the pelvis (52 bodies of 150
stood under 300 N). Two things were needed to ask it of hands. A scripted hand has to find a body wherever its
fall has put it: an `effector` event may say `"at_bone"`, and its keys are then measured from where that part
of the body is when the event begins (`Scenarios\README.md`; `grab_getup_held` uses it). And the stand of falls
got holding hands (`Tools\getup-stand.ps1 -HoldHands 1` or `2`, `Tools\getup-seeds.ps1 -HoldHands`): with the
grab on for the trial, one or two hands come down on the chest of the fallen body, close their grips and keep
their place for 15 s; so that they arrive on a body at rest, a held body begins its get-up after 4 s of lying,
not 1.5.

| Hands | Bodies taken hold of, of 150 | By the chest itself | A grip came off by itself | Pulled free and stood before the 15 s were up | Stand in the end |
| --- | --- | --- | --- | --- | --- |
| one | 146 (48, 48, 50) | 123 | 77 | 37 (12, 7, 18): a quarter | 141 |
| two | 146 (48, 48, 50) | 107 | 11 | 0 | 140 |

A body cannot stand with a grip on it that keeps its place 12 cm above where the chest lay: the grip slips
when the hand is left 45 cm behind. So "stood while it was held" is, for hands, "pulled free and stood before
the time was up", and every such body is also among those whose grip came off. Read that way the rule holds as
the grab is built, with no rule of the body added for it: from one hand at its 300 N half the bodies pull free
and a quarter are on their feet within the 15 s; from two hands 11 of 146 pull one grip off and none stands.
After the hands let go the bodies get up as they do unhindered (144 and 143 of 150 end standing, against
148). A hand takes the body nearest to it, and for a fifth to a quarter of the falls that was not the chest
itself but what lay on or beside it (the neck, an arm, the next segment of the trunk); the four bodies of 150
the hands did not take hold of had moved from where their chest was when the hands set out. One seed with the
hands sent to the pelvis instead: from one hand 23 of 50 pull free and 3 stand in time, from two 5 and none.

Read by another as well (the fourth reading of the night, of the hand sent to a part of the body and of the
stand's hands): nothing that could move a hash or crash; fixed were what the summary's words claimed, a
mistyped part's name that sent the hands to the middle of the room without a word, the two options
(`-PressN`, `-HoldHands`) given together, and which of two trajectories of one hand that begin together is
moved.

In the one fall there is a scenario for (`grab_getup_held`), one hand on the chest keeps the body on its hands
and knees for as long as it holds; the attempt is given up as the hand lets go, and the body stands 8 s later.
- *On the desktop the G key holds the hands open,* so that the grab can be tried without the headset
  (`Tools\play.ps1 -Grab`): G held, a mouse button held to reach, G let go while the
  hand presses on the body, and the hand holds; G again lets go. The grip has to close while the hand still
  presses: a body that is pushed gives way, and half a second later it stood 3 cm out of the hand's reach (the
  first run of the check below). The proper verbs are part 5.
- *In a headset the take's gesture and the grab share the grips.* They do not collide (a grip that closes in
  the air above the head takes hold of nothing), but a button is still the better way once it is known which
  ones arrive.
- *A hand that holds something passes every simulated body,* the other hand and, later, props among them. With
  held objects (part 4) this becomes a matter of pairs.
- *The path "what it held is gone"* (a reset or a new take while a hand holds the character) no scenario
  reaches, since a scenario has no reset. A session without a person does (`Tools\play.ps1 -Demo -Grab`, a step
  of the bootstrap): the right hand takes hold of the chest, the character is put back while it is held, the
  hand has lost what it held and holds nothing, comes back, takes hold of the new character, pulls and lets go.

**The code read by another, again.** The same reading as for the evening's code, of the grab alone, before any
of it was called done. No fault in the lifetimes (every order of destruction traced, the engine's handling of
a joint whose body is removed checked in its source) and none in the path taken with the grab off. Found and
fixed: two hands that overlapped were set back to colliding inside each other, because the test for "clear"
left the whole rig out (it leaves out only the hand's own two bodies now; `grab_two_hands_one_arm`); the force
a hand reports kept its last value after a snap and would have gone on vibrating the controller; the pulls'
tool gave 0.5 and 1 m/s the same scenario name, accepted a hold of 0 s and reported "0 of 0" as passed; a prop
of another object type could have been taken by a hand that still collided with it (only what a holding hand
passes is taken now); a hold that ended by a slip was counted as a grip that closed on a body. And one thing
about the engine: a joint's force is kept for the last substep of a frame only, so the "largest force" of a
grab is the largest at the end of a frame.

**And a third time, for what was written after that reading** (the hold events, the hand that collides again,
the G key, the session's grab demo, the tools). Nothing that touches a run with the grab off. Found and fixed:

- *The kept recording of the desktop's hands had every grip at 0.* It was made in step 1, before a hand had a
  grip; since the evening a desktop hand is a fist with a grip of 1. Replayed, the kept recording had open
  hands for the whole run, which changes nothing while the open hand is only shown, and would have made
  plates of them with `palm_collides` on. `Recordings\demo_hands.json` is made anew; `replay_demo_hands` keeps
  its hashes.
- *A recording carries the grip and not what the grip could do.* A session started with the grab on, replayed
  from its recording with the tuning files as they are, closed its grips on nothing, and nothing said why. A
  session's replay scenario now switches on, before its first step, what its own hands had switched on (the
  grab, the open hand as a body); checked by a session recorded with both on (9 of 9 comparisons hold).
- *The check `grab_events` promised more than it tested:* "each grab has ended or is still held" was true by
  the way the body keeps its list. It now holds that what the body was last told is held is what the hands hold
  at the run's end, and the body is told once more as a run ends (a grip that closed in the last frame was
  counted by the hand and never told).
- Smaller: a snap during a hold was counted as a grip closed on a body; the depth at which a hand collides
  again may be 1.5 cm at most (half the open hand's thickness); the pulls' tool refuses speeds that round to
  the same tenth and a distance of 0, and prints 0.5 m/s as 0.5; `Tools\play.ps1 -Grab` without `-Demo` did
  nothing and is now a person's session with the grab on.

### M4, what the hands do as they are tuned, and the leg that does not come (the night of 24 to 25 November)

Two measurements with no value changed, made after the grab, on the question the plan leaves to part 2
("the fist's mass, the force cap and the stiffness, on the recordings") and on the oddity the grab's pulls found.

**A hand that goes on through the body fells it, as it is tuned.** Scripted hands from 70 cm in front of the
chest to 150 cm behind it (a shove) at 1, 2 and 4 m/s with one hand and with two, and punches at the head and
the chest that go 40 cm in and stay a second, at 6 and 10 m/s; each with the hands as tuned (1.5 kg, 300 N),
with a cap of 600 N, with 600 N and 3 kg, and with 1200 N and 3 kg (40 runs).

| What | As tuned | With the stronger and heavier hands |
| --- | --- | --- |
| A shove at 1 m/s, one hand or two | three steps back, then a fall; 262 to 273 N*s | the same; 258 to 272 N*s |
| A shove at 2 and 4 m/s | one step and a fall; 133 to 154 N*s | none or one step and a fall; 145 to 197 N*s |
| A punch into the chest that stays in | one step and a fall; 114 to 116 N*s | the same; 130 to 140 N*s |
| A punch into the head that stays in | two steps, stands; 76 to 77 N*s | two to four steps, stands; 65 to 94 N*s |

So what fells the body is not the hand's strength but how long it goes on pushing: 300 N for half a second is
150 N*s, and the push matrix has the body fall from 200 N*s and step from 80. A punch that comes back at once
(the suite's strikes, and every punch of the owner's first recording) delivers 12 to 28 N*s and makes it
flinch. Twice the cap and twice the mass change the peak force of the contact (up to 3200 N from 1750) and
not the outcome. For the tuning of part 2 this says: the cap and the mass decide how hard a quick punch is; a
knockdown is already there for a hand that follows through. Two scenarios hold it, one for each verb of the
plan that had none with hands: `effector_push_chest_falls` (the push: three steps back, then the fall) and
`effector_shove_knockdown_gets_up` (the knockdown: two hands, the fall, the get-up).

**A standing leg that is pulled does not come: mostly because a hand is not strong enough.** A foot that
carries half the body presses on the floor with about 400 N, and at a friction of 0.7 for the soles it takes
280 N to slide it; the hand has 300. The balance assist adds to that: its pull on the pelvis carried up to
0.26 of body weight during a pull of 276 N on the thigh. With that pull capped (`assist.pelvis.max_force_n`,
which is 0, no cap):

| Cap on the pelvis's assist | A thigh pulled forward | A thigh pulled sideways, a calf pulled sideways or forward | Push matrix |
| --- | --- | --- | --- |
| none (as tuned) | stands, the grip slips | stand, the grip slips | 160 of 160 |
| 200 N, 120 N | stands, the grip slips | stand, the grip slips | 160 of 160 (four more shuffles in the 80 N*s row at 120 N) |
| 80 N | comes along in three steps | stand, the grip slips | 160 of 160 (the same four) |

Not changed: the stand of get-ups and the knockdowns were not run against a cap, and whether a body should
come along by its thigh under one hand is the owner's to say. If it should, 80 N is where it begins, at no
cost to the push matrix.

### M4 part 5, a first step: the kicking foot (the night of 24 to 25 November)

The kick was the one verb of the plan with no way to be made at all. It is an effector of a kind of its own:
`foot_left` and `foot_right`, the same body on the same spring as a hand (`USandboxEffectorComponent`), with a
leg's mass and strength (`effectors.foot.*`) and none of what makes a hand a hand: it has no grip, no open
shape and no grab, and is always a ball. A scenario scripts it as it scripts a hand. No scenario of before is
touched (their hashes are the same).

**The values, and what decided them.** 5 kg (the lower leg and the foot), a ball of 6 cm, a spring of
10 000 N/m (the hand's natural frequency at this mass), and a force cap that was measured: with 900 N, three
hands' worth, a kick that goes 30 cm through the trunk and stays 0.15 s delivers 152 N*s and the body takes five
steps and stands, and 214 N*s into the thigh drive it back ten steps; with 1800 N the same kick through the
trunk delivers 173 N*s in less time and fells it, with 8 kg and 3000 N likewise. 1800 N is about what a leg
pushes with, and it is the smallest of the three that gives the plan's "or the NPC falls". Each is the
owner's to change.

| Scenario | The kick, at 8 m/s | What happens |
| --- | --- | --- |
| `strike_kick_thigh` | from the front 10 cm into the left thigh, and back | 61 N*s; the thigh gives 23 cm, three steps, stands |
| `strike_kick_calf` | from the front 10 cm into the left calf, and back | 51 N*s; the calf gives 30 cm, the body stands without a step |
| `strike_kick_torso` | from the front 10 cm into the belly, and back | 73 N*s; two steps, stands |
| `strike_kick_torso_through_falls` | the same, 30 cm through and held 0.15 s | 173 N*s; one step and a fall, at rest after 1.5 s |

All four: no tunnelling, 0.00 cm in the body, the fastest joint 5 to 21 rad/s (the limit is 30), joints
stretched 0.07 cm at most. Kicks at 4, 6, 8, 10 and 12 m/s into the thigh and the calf from the front and into
the calf from the side (15 runs): no tunnelling, 2 to 20 rad/s, and the impulse does not grow with the speed
(24 to 74 N*s): what a kick delivers is the leg's push while the foot is on the body more than the foot's
momentum, as with the hands. With the cap at 900 N the kick at the calf from the side had the ankle at 50 rad/s
at 8 m/s and at 5 and 12 rad/s at 6 and 4 m/s: a figure that jumps as the speed changes smoothly; at 1800 N
it is 5 to 20. It is not in the suite; the calf is kicked from the front there.

**On the desktop the F key kicks.** A desktop session's rig has the right foot as a third effector. It rests
below the camera (`tools.kick.rest_*`: 20 cm ahead, 12 cm to the right, 100 cm down, the camera taken as
level), and F sends it in a straight line to what the camera aims at and 15 cm through it, at 8 m/s, and back
(`tools.kick.speed_mps`, `through_cm`; with nothing aimed at, 110 cm ahead). The foot is in a session's
recording as the hands are, and a replay kicks as the session did; the kept recordings, made without it, replay
as before (`replay_demo_hands` and the owner's `vr_check` keep their hashes; a fresh recording of the session
without a person has three effectors and its two replays agree, 13 of 13 comparisons). In a headset there is
no foot yet: a kick there waits for a button that is known to arrive. The session without a person
(`Tools\play.ps1 -Demo -Grab`) ends with a kick at the thighs: the character tells of a hit of 28 N*s, and the
foot delivers 140 N*s in all.

Read by another, the desktop's kick had three faults, fixed before it was called done. A kick had no limit of
reach: aimed at a wall across the room, the foot would have gone there, pushing at 1800 N on whatever stood in
its line; a kick now goes no further than `tools.kick.reach_cm` (130) from where the foot rests. The resting
foot was an unseen body with a leg's strength under the camera: with a low camera it lay in the floor and
pressed on it, and over a fallen character it would have pressed on that; at rest the foot is now held with
`tools.kick.rest_force_n` (40 N) and has its strength only while it kicks. (That strength is part of the
foot's target, and of a recording, a twelfth number a target; recordings made before have eleven and read as
full strength. Set on the effector from outside, as it first was, the replay of a session had a foot of 1800 N
where the session's had 40, and the bootstrap's record-and-replay step said so: the session's foot had brushed
the floor and the replay's had not. The foot also rests 100 cm below the camera now, not 120.) A second
reading found what the weak foot had brought with it: its target came home from a kick at 8 m/s and the foot,
weak again that same frame, would have flown on past its rest (5 kg at 8 m/s against 40 N); it keeps its
strength for `tools.kick.settle_s` (0.4 s) after a kick, and after any jump of the view, which a weak foot
crossed by drifting through whatever stood between. The session's demo now also wants the foot home within
5 cm after the kick, never having been put there. And a take that begins during a kick ends the kick before
the frame's targets are made, not after. A third reading: "after any jump of the view" was first written as
"after any frame that follows a pause", which in slow motion is every frame, so that the foot was never weak
there; a jump is now a rest that has moved further in one step than a kick goes. And a strength of nothing, in
a recording written by hand, would have been a spring without a cap; an effector keeps a hundredth of its
strength at the least. And the demo's "the kick landed"
was true if the foot had touched anything since the session began; it now wants an impulse from the foot after
the key and a hit told by the character. The three standing kicks of the suite turned their targets at the
body's surface, where a body a few centimetres further back is touched by a foot already on its way home; they
go 10 cm in now.

### M4 part 4, a first step: a thing with mass in a scenario (the night of 24 to 25 November)

Part 4 is objects and movable furniture. Its first need is a simulated thing in the world at all, and that is
what this is: a scenario may list `"props"`, boxes with a name, a size, a mass and a place
(`Scenarios\README.md`), and the stage puts each into the world before the first step
(`FScenarioStage::SpawnProps`): a box component that simulates, with a cube attached to be seen. It collides
with the floor, the character and the hands; a hand can take hold of it, since it is of the kind of body a
holding hand passes; the character feels it as it feels any simulated body that touches it, a hit or a
pressure. A run's `props` say where each ended, how far it went, how fast it ever was and how far it is tipped;
the expectations `min_prop_travel_cm`, `max_prop_travel_cm` and `max_prop_speed_mps` hold them. Scenarios without
props run as before (82 of 82 hashes).

| Scenario | What is done | What happens |
| --- | --- | --- |
| `prop_push_box` | a fist pushes a box of 5 kg along the floor | it slides 129 cm at 1.2 m/s at most; the hand gave it 48 N*s; the character, two paces away, notices nothing |
| `prop_lift_box` | an open hand comes down on the box, closes, lifts it 70 cm, carries it a metre, sets it down, opens | the box comes along as it was taken and stands where it is set down, 6 cm from the place; the grab's joint carries 51 N on average, the box's weight, and 157 N at most; no slip |
| `prop_drop_box_on_head` | the same hand lifts a box of 5 kg over the character's head and lets go | it falls 20 cm onto the head: one hit told (15 N*s felt), a flinch, the character stands; the box tumbles to the floor, 5.5 m/s at most |
| `prop_throw_box` | a box of 2 kg is taken, lifted, swung away from the character and let go in the swing at 5 m/s | it leaves the hand and lands 6 m away (6.9 m/s at most on its way); the joint carried 74 N at most |
| `prop_lift_heavy_one_hand` | one hand takes a crate of 40 kg by its top and its target rises 60 cm | the crate does not move; the hand is left 55 cm behind its target at its 300 N and the grip slips; nothing is thrown (the crate 0.0 m/s) |
| `prop_lift_heavy_two_hands` | the same with two hands | the crate is lifted 53 cm and set down again; each hand carries 169 N on average and 253 N at most, no slip |
| `prop_fall_on_box` | a box of 20 kg stands behind the character, which is knocked over backward onto it | it falls across the box (259 N*s felt), is 0.33 cm in the world at most, gains 25.5 J as in any fall and lies at rest after 4.0 s, where a fall on the bare floor takes 2.2; the box is moved 11 cm |

**A thing that is held keeps its direction in the hand.** A grab on a limb holds a point and lets the limb
turn; a thing held that way only hangs, and a stick cannot be struck with. So for a thing (whatever is not a
part of a character) the grab's joint also has a spring with a torque cap that turns the thing toward the way
it lay in the hand when it was taken (`effectors.hand.grab_hold_nm_per_rad` 150, `grab_hold_damping_s` 0.05,
`grab_hold_max_torque_nm` 40). The box of `prop_lift_box` swung under the hand before and ended 35 cm from
where it was set down; now 6 cm. `prop_stick_strike_arm`: a stick of 90 cm and 0.8 kg is taken from the floor
by one end, lifted to shoulder height, carried to the character's side and swung through its upper arm at
5.7 m/s. It does not pass through (0.04 cm in the world at most), and the body feels 2.5 N*s, which is not a
hit to it. Two things about that. A swing whose target leaves at full speed at once does not strike at all:
the hand is thrown sideways at 130 m/s^2, the wrist has 20 N*m to turn the stick with, and the stick trails
behind the hand and is drawn past the body lengthwise (seen on the film); the scenario's swing gathers speed
over 0.4 s. And how hard a held thing hits is the wrist's strength and the thing's mass: with the hand's turning
spring and the hold at three times their values the same stick delivers 6.7 N*s, a stick of 2 kg 4.0 N*s, both
7.0 N*s, none of them a hit. The plan's stick, whose end goes at up to 12 m/s and whose blow is a hit, needs
those values set in part 4; they are as they were.

**A handle.** The owner's rule for held objects: "a handle named in the object's spec draws the hand in when
it is near; anywhere else the object is held where it was touched". A prop may name one (`"handle_cm"`, a point
of the thing from its middle); a hand that closes within `effectors.hand.grab_handle_cm` (15) of it takes the
thing by that point, and the joint draws the handle into the hand. The hand still knows nothing of the thing: it
looks for a place with a tag on whatever it takes hold of. The stick of `prop_stick_strike_arm` names its end;
taken 10 cm from there it is held by the end (`handle_grabs` 1, the expectation `effector_handle_grabs`), and
its swing through the arm is then felt as 12.7 N*s, where held 10 cm further in it was 2.5; the body still
does not tell of a hit (the stick slides along the arm: no single frame brings what a hit is). By the engine's
source, read by the reviewer and not measured: the joint closes the distance in one substep, shared by the two
masses, and the engine's projection moves only the hand; a light stick comes most of the way to the hand, a
table of 30 kg would have the hand jump to its handle and then hang behind its target while 300 N drag the
table. Nothing is thrown.

The plan's criterion for the stick ("strikes head, chest, shoulder and forearm at several heights with its
end at up to 12 m/s: no tunnelling, and the impulse grows with the speed") is not met by this, and the sweep
says where it stands: the same stick, taken by its handle, swung at 95, 120, 140 and 162 cm at 3, 5 and 7 m/s
(12 runs; the stick itself reaches 3.7 to 8.3 m/s). It is held in all, no grip slips. The body feels 13 to
53 N*s at the three lower heights and tells of one hit in the twelve (the forearm, at 120 cm and 7 m/s); what
it feels does not grow with the speed (13.8, 29.6 and 17.8 N*s at 95 cm); at 140 cm and 7 m/s the stick is
1.53 cm in the body for a moment and only 3 N*s are felt; at the head's height it passes without touching
(the stick hangs below where the hand holds it). So: a held thing can be swung and does not go through at
these speeds, and what a blow with it is, is still to be made: the wrist's strength, the stick's mass, and how
the body counts a contact that slides along it.

A session puts the things back where the scenario has them whenever the character is put back, so that a
take's replay starts from the same places, each under the name it had (`Prop_<name>`: what a hand that touches
or holds it reports); and a run with props has a check of its own, `props_simulated`: a
limit on how far or how fast a thing went says nothing of a thing that was never made.

Known of the body and things, from the code's reading and one run: to the body every contact with a thing is
a hit or a press, never support. A trunk that comes down on a box does not count as having met the floor (the
muscles' let-go waits for the floor), a body that rests on things alone counts as held up, and a thing that
presses with 450 N or more would keep a get-up waiting. In the one case run, the body that fell across the
20 kg box got up at the first attempt in 4.5 s and felt no press. A thing that lands on a fallen body is not an
"input" to the energy watch and would read as energy from nowhere. Nothing refuses a prop that is put inside
the character or the furniture of a map.

Measured, as the starting point of part 4's step for the body (four runs, nothing changed): a body put on a
movable plate (120 by 120 by 10 cm, 40 kg) stands on it for the whole run, and its sense of balance has no
foot on the ground in any of the 720 frames: no support polygon, no margins. It tells of a hit as it lands on
the plate, and feels its own weight as a press of 799 N. Struck with 30 N*s there it stands without a step
(13.5 cm of pelvis drift, the assist at 0.15 of body weight at most), a second flinch. A box of 5 kg pushed
into its shins by a hand is one hit and a press of 282 N (366 N*s felt in all), a box of 20 kg 369 N; it stands
both, without a step. So the owner's rule for part 4, "a steady force from below is support, a sharp impulse
from the side or above is a hit", is not there in any part: every simulated thing is a hand to the body. That
step changes what the body feels and stands on, and is for a day with a baseline before it and three seeds
after.

**The step, behind a switch that is off** (`reactions.thing_support`). The owner's rule as one condition in
the place where the body takes a contact (`USandboxBodyController::OnBodyHit`): a simulated body that pushes
up on a part of the character (the impulse within 45 degrees of straight up) is treated as the floor is: it
goes to the sense of balance and is not felt. On a foot whatever pushes up is support, the landing of a step
too; on any other part only a push that is in one substep no harder than `reactions.thing_support_max_ns`
(5 N*s, which kept up is 900 N; a hand's uppercut at 6 m/s is 9 N*s and stays a hit). Anything from the side or
from above is felt as before. With the switch off every scenario keeps its hashes (93 of 93). With it on:

| On a movable plate of 40 kg | Switch off | Switch on |
| --- | --- | --- |
| standing | no foot on the ground in any frame; its own weight felt as a press of 799 N; one hit told as it lands | both feet in all 720 frames, the centre of mass 8.7 cm inside its support, the assist at 0.0052 of body weight (the floor's figure), no hit when the switch is on from the first frame |
| struck with 30 N*s | stands, 13.5 cm of pelvis drift, a second flinch | stands, 8.7 cm |
| struck with 80 N*s | falls (it has no feet to step with) | three steps, stands |
| knocked down with 200 N*s on a plate of 4 m, to get up | not run | falls, and gets up on the plate at the second attempt; its own fall and get-up are told as nine hits (5 to 36 N*s), the parts that come down on the plate harder than the limit |

Three scenarios switch it on for themselves (`prop_stand_on_plate`, `prop_step_on_plate`,
`prop_getup_on_plate`). And the plan's "a hand that lifts or presses must keep reading as it does now; that is
checked, not assumed": the whole suite run with the switch on has 90 of 95 scenarios bit for bit as they are
without it; the five that differ are the three on the plate, the fall onto the box (the body lies on it) and
one with a hand, `grab_getup_held`, where the hand that holds the chest of the lying body is under part of
it. The body cannot tell a hand from a thing, so a hand under a foot is support too. Not measured with it on:
the push matrix, the knockdowns and the stand (none has a thing in it), a thing that tips or slides away under
the feet, a body sitting or propped on a thing, a stick underfoot.

**The stand of 50 falls among movable furniture, three seeds** (the plan's criterion for part 4;
`Tools\getup-stand.ps1 -Only floor -Floor 50 -PropsFile Tools\stand_furniture_props.json`): a table of 30 kg
110 cm behind the body, a seat of 8 kg a metre to its left and a box of 3 kg at its right, all three simulated.

| Support from things | Falls that end standing | Falls in which a thing was moved | Of those: end standing | Of those: at the first attempt |
| --- | --- | --- | --- | --- |
| off (as tuned) | 50, 50, 50 of 50 | 26, 27, 29 | 26, 27, 29 | 25, 24, 26 |
| on | 50, 50, 50 of 50 | 26, 27, 29 | 26, 27, 29 | 21, 24, 25 |

Every body gets up, with the switch on and off: furniture of this weight is shoved aside by a falling body and
does not keep it down. With the switch on five more get-ups of 82 need a second attempt, which is the row of
hits a fall onto a thing is under the rule's limit (below). Heavier furniture, furniture a body gets wedged
under and furniture of a room's map are not in this.

What the reading by another and the runs showed of the rule's limits, for the owner's decision before it is
switched on:

- *A fall onto a thing is a row of hits.* The limit that keeps an uppercut a hit also makes a hit of every
  part that comes down hard on the thing; on the floor nothing of a fall is a hit. Without a limit a blow from
  below would be support. The contact alone does not tell the two apart; the thing's mass would (a body can
  lie on 20 kg, not on a hand of 1.5 kg), which is a step toward knowing what touches it.
- *The support is taken to stand still.* The capture point is worked out from the body's velocity in the
  world, and the assist's anchors are fixed in the world: a plate that slides at 0.13 m/s puts the capture
  point 4 cm out and the body steps, and a plate that carries the body 20 cm is pulled against by 300 N at the
  pelvis and at each foot. No scenario moves the plate: a hand at its 300 N against the plate's edge does not
  move it with the body on it (120 kg on the floor take about 820 N to slide), and a scenario has no force
  for a thing.
- *A hand that lifts is support.* A hand that pushes up on a body in the air counts as the world being
  touched, so the body is never "held up" (`UpdateHeld`) and keeps the pose of its fall; and an upward touch on
  a swinging foot ends the step in the air.
- *A foot that is touched from below is standing.* A body hung up by a hand whose foot something pushes up
  on no longer counts as held up, and a blow on the sole of a lying body is not felt.
- *With the grab on as well, a held get-up goes another way.* The suite with every switch of the night on at
  once (the grab, the open hand as a body, support from things) passes 93 of 95: `palm_close_on_chest`, whose
  grip closes on the chest and now takes hold of it, and `grab_getup_held`, which does not end standing in
  its 30 s: the hand that holds the chest is support to the body. The push matrix, the knockdowns and the
  stand on three seeds are the same with support from things on as off (160 of 160; 8 of 8; 49, 50 and 49 of
  50 with 38, 44 and 44 at the first attempt): none has a thing in it.
- *Things are not checked against the space a body needs as it appears* (its arms out, 58 cm to either side),
  as the stand's furniture of a room is.
- *The scenarios begin with a flinch:* a console event reaches the body after the first step, in which the
  landing on the plate was still a hit. With the switch on from the start (`-Tune`) there is none.
- The first version of `prop_getup_on_plate` passed with a body that had fallen off the plate and got up on
  the floor; no expectation says where a body stands. The plate is 4 m now, and the run was looked at.

Not done, and what part 4 proper has to decide: furniture of a room spec with mass (this is a scenario's
own list; the rooms' tables and chairs are still part of the map and static); a grip that opens under overload
by the thing's own load is the grab's own two rules (the slip and the torn grip) and was not measured on a
heavy thing; how
strong the wrist is, by which a stick's blow becomes a hit; the difference between a thing that supports the body and one that hits it, which the
body is to read from the contact; the stand of falls among movable furniture. The body's rest criterion for a
fall onto a thing is 5 s in the scenario, against the 3 s of a fall on the floor.

### The machine's reboot of 24 November 2026, 22:03

The fifth launch in the headset that evening ended in a blue screen and a reboot. What the logs say:

| Source | Finding |
| --- | --- |
| System log | `Kernel-Power` 41 at 22:03:45; bugcheck `0x0000003b` (SYSTEM_SERVICE_EXCEPTION, access violation `0xc0000005`), "possibly related driver: nvlddmkm.sys"; the first bugcheck in 60 days |
| SteamVR's logs | The owner shut SteamVR down at 21:50:28. At 22:03:26 `vrserver` is starting again: the launch's own OpenXR instance started it |
| The engine's log of that launch | `xrCreateInstance` at 22:03:24 to :26, then the D3D12 device: the last lines are "Aftermath initialized" and "Emitting draw events for PIX profiling" at 22:03:27. In a run that goes on, "Aftermath enabled" and the device's interfaces follow within 60 ms |
| Machine | RTX 5080, driver 32.0.16.2114 (621.14) of 27 October 2026; Windows 11 build 26300.9974; hardware-accelerated GPU scheduling on |

So the graphics driver failed in the kernel while the engine created its device and SteamVR's compositor was
taking the headset's display at the same moment. The four launches before it found SteamVR running and ready.
The dump could not be read (no administrator's rights in this session), so the driver's function is not named.

What was done: `Tools\play.ps1 -VR` and `Tools\scenario.ps1 -VR` refuse to start unless SteamVR's server and
compositor are running, and `Invoke-Editor -VR` refuses as a last guard; a run in the headset is started only
on the owner's word that the headset is on; in unattended work what is not committed is copied to
`Saved\Backups` before work goes on. After the reboot: the 27 changed files intact, `git fsck` clean, build,
33 unit tests, suite 65 of 65 with the baseline's hashes.

What only the owner can do (administrator): read the dump (`C:\Windows\Minidump\112426-16843-01.dmp`) for the
driver's function; update the NVIDIA driver; consider switching hardware-accelerated GPU scheduling off for the
headset. None of it is needed for the work to go on.

## Known behaviour of the stock mannequin

- **Residual hand motion.** After the 80 N*s impulse in `impulse_passive`, the right hand keeps rotating slowly
  on the floor and the body never satisfies the rest criterion (5 cm/s, 0.2 rad/s). The scenario therefore
  has no `rest_within_s` expectation. This is a property of the stock physics asset, to be addressed by the
  project's own physics asset in M1 and M2.
- **A free constraint.** `UserConstraint_0` in the stock physics asset is not a rigid joint; the stretch
  metric skips it.
