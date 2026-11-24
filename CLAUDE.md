# DictatorSandbox

Unreal Engine 5.8 C++ prototype: one photoreal room, one active-ragdoll NPC the player can hit, push, grab
and drag, on desktop and PCVR. Requirements, architecture and milestones are in `docs/PRD.md`.
Current milestone: **M1 done** (MetaHuman by script, physics asset and room from JSON specs, baked lighting,
launch in the headset, 7.1 ms of GPU time per stereo frame). **M2, active ragdoll core: all five steps
are built and its six criteria are met; the owner's hand check of the desktop session is deferred and does
not hold up M3** (the deferred list is at the end of the M2 section of `docs/PRD.md`).
The NPC stands under full simulation for 10 minutes (muscles and balance assist through Physics Control);
every tuning value lives in `Config/Tuning`, is hot-reloaded and can be overridden from the console and
the command line; the body has the states Standing, Flinch, Stagger, Falling, Lying and GetUp, joint limits from anatomy
that mirror left to right, and passes the push matrix; the player has physical hands (effectors: bodies
with mass on force-capped springs) that the body feels through its own contact events, a desktop pawn with
test tools, and scenarios can script the hands; debug overlays show the controller's work in a session and
on scenario screenshots; slaps at 6 m/s and punches at 8 m/s on head, chest, thigh and forearm pass without
tunnelling. **M3, balance and recovery, is done: closed on 24 November 2026 with its five criteria met, the fifth by a
review of the clips that the owner handed to the agent; the owner's own look in a session is deferred with M2's
and does not hold up M4** (the seven parts, the gate's table, the review and the known limits are in
the M3 section of `docs/PRD.md`). **M4, full interaction and VR, is in progress: part 1 of 6, the hands in the
headset, has step 1 of 4 done** (its order of parts and what M3 hands over to it are in the M4 section). Step 1:
a session's hands can be recorded (`Tools\play.ps1 -Record`, the K key) and a scenario replays the recording frame
for frame (`"recording"`; `replay_demo_hands`); `Tools\record-replay.ps1` holds a replay against its session
(the same states of the body within a frame, the pelvis within 1 cm, the hands' impulses within 5%) and two
replays against each other by their hashes. Next is step 2, the OpenXR target source. The body senses its own balance (standing feet, support polygon, capture
point); every run measures energy, rest after a fall and penetration of the body
into the world and into itself; the suite runs eight scenarios at a time, and there are tools for baselines
and parameter sweeps. Three defects of earlier milestones were found and fixed in that part: the generated
physics asset had no self-collision, the standing character slept one substep in six, and a run on a busy
machine could differ from the one before it (the solver's determinism mode is now on). Part 2, the source
of the target pose (a native animation instance, poses from text), is done: its step 0 gave the
body collarbones (18 bodies; the shoulder is two links) and the hands three times their inertia, so that a
punch on the forearm does not snap the wrist. Steps 1 to 3 gave the body a native animation instance as the
source of its target pose, poses written as joint angles in `Config/Tuning/poses.json`, and the flinch's
pose offset (the struck part gives way, shoulders up, head in); step 4 two-bone leg placement
(`USandboxBodyController::SetFootTarget`, scenario event `foot_target`). The idle from `MM_Idle` moved to
part 7 then, and was done in the audit after part 4. Part 3, standing balance: the capture point decides whether the body stands, needs a step (held by
the assist until part 4, reported as `step_needed_frames`) or falls; ankle and hip strategies are turns of the
leg targets; the assist's anchors follow the feet; a held push is leant into and makes the body flinch only as
it lands; quiet-standing assist share 0.0052 (bound 0.0093); the push matrix names each outcome (stood, held,
stepped, fell) and judges it by the owner's table (the 80 N*s row from part 4). Part 4, Stagger and the recovery step: a body whose
capture point leaves what the ankles and hips bring back staggers and steps (to where the capture point will be
when the foot lands, across the other foot if it must, again if one step is not enough), and can be asked for a
step (`RequestStep`, scenario event `step`). An audit of how the body moves (November 2026, on the owner's
request) followed it: the stance has the arms down, arms and neck are soft, the body always tries to step and
falls only when its posture gives way, a falling body takes a protective pose and a lying one lets go
(`poses.fall_*`), after recovery steps the body brings its feet back together (`balance.regroup_*`), a standing
body carries the motion of the engine's `MM_Idle` on top of its stance (breathing, the shoulders, a slow sway;
`poses.idle.*`, the idle planned for part 7), and its arms join the fight for balance (`poses.balance_*`,
`balance.arm_*`). **Part 5, the fall, is done but for one look that waits for the owner:** knockdowns from 8
directions pass (`Tools\knockdowns.ps1`), the muscles have torque caps from published strength figures, they
let go after the body meets the floor, joint limits are written in the spec, and continuous collision detection
stays off by measurement; whether the poses of Epic's Game Animation Sample are better than these is looked at
once the owner has downloaded it (done: its get-up clips are taken, its logic is not). **Part 6, the get-up, is
done (three steps).** A recorded clip is a source of the target pose (`FSandboxClipPose`: the four
get-ups of Epic's Game Animation Sample, made for another skeleton, carried over to the body's 17 joints;
`USandboxBodyController::SetClipPose`, scenario event `clip_pose`, `Tools\clip-film.ps1`). **Step 2 is done:** the
state GetUp. A body that has lain still for `getup.rest_s` takes the clip whose pelvis lies most like its own and
gets up by it under full simulation: the muscles are asked for the clip's pose, the clip's time waits for the body,
the assist pulls the pelvis and the feet toward where the clip has them with capped forces, and the clip is laid
over the feet the body really stands on. It gets up from all four sides in 3.3 to 4.1 s; a 30 N*s push does not
stop it, a 200 N*s blow fells it and it tries again, two hands at their 300 N each only slow it, and a force it
cannot lift (1500 N on the pelvis) is waited out for 3 s, given up and tried again (scenarios `getup_*`). A scenario's body gets up only with `"get_up": true`. **Step 3, the stand:** `Tools\getup-stand.ps1`
knocks the body down 50 times from a seed, 24 of them near a wall, a table and a chair (static boxes of a room
spec, `Specs/rooms/getup_room.json`: the table in the middle of the room, the chair drawn up to it, since the
owner's request of 24 November), and counts the falls that end standing: 49, 50 and 50 of 50 over three seeds
(the criterion is 90%), 39, 37 and 43 of them at the first attempt (50, 49 and 48 with the two pieces apart near
the corners). **Part 7, the gate, is done (four steps and the review).** The get-up's assist is
0.28 of body weight on average, from 0.51, with the stand unchanged (49, 50 and 49 of 50): the clip follows the
body over the floor while the feet carry nothing (`getup.pelvis_follow_s`), the arms are stronger while getting
up, and the assist's caps are 90 N on the pelvis and 30 N on a foot. A body getting up that is pressed with
450 N or more does not push on (`getup.hold_force_n`): 600 N keeps every body of 150 down, under 300 N one of
three gets up while it is pressed (`Tools\getup-stand.ps1 -PressN`; two of three before the body's friction on the
floor went from 0.7 to 0.5, so "one hand slows" is set again in M4 with the grab). The stance's elbows hang at 17 degrees of bend, not 37
(`poses.stance.lowerarm_*`). The wrist's model waits for M4's grab by the hand (owner's decision). **Step 2,
friction and restitution, is done:** the body's surface is tuning (`surface.*`: on the floor a friction of 0.5 for
skin and cloth and 0.7 for the soles, where both were the engine's 0.7; restitution 0.1, taken as the smaller of
the two surfaces'), measured by dragging the lying body (`Tools\surface-friction.ps1`: 0.69 before, 0.51 to 0.54
now). A body with 0.9 against the floor gets up at the first attempt 101 times of 150 instead of 124 to 130;
otherwise standing, pushes, falls and get-ups hardly notice friction from 0.4 to 1.0 or restitution from 0 to
0.3. A lying body needs about 400 N to be dragged: one hand does not move it, two do. **Step 3, the sweeps, is
done and changed no value:** 64 solver iterations is the only count that passes everything (16 and 8 break the
bound on the standing assist's share, 32 to 48 lose two cells of the push matrix; 32 would save 0.15 ms a frame),
the legs' strength is held from both sides (at 400 the body does not step, at 1200 a blow of 200 N*s on the chest
does not fell it), the other muscles and the dampings are on a plateau, and the body stands with no assist at all
but then takes up to seven steps after 30 N*s on the head. **Before step 4 (owner's decisions of 24 November), both done:** the push matrix counts a
step from 10 cm (`-StepMinCm`; a foot that hops less is a shuffle, counted and not judged: the cells lost at 32
iterations and with a weaker assist were such hops of 5 to 6 cm), and the stance on crossed legs that Stagger did
not leave is closed: a staggering body that has come to rest with its centre of mass off the support that touches
steps once more (`balance.rest_step_s`; scenario `stagger_rest_off_support`), where it had neither stepped (the
capture point within 4 cm of its whole soles) nor stood (the centre of mass not over the edges it stood on), kept
up by the stagger's upright hold on the pelvis. **Step 4, the clips and the criteria table, is done, and so is the review**
(`Saved\Films\m3_*`, listed in `Saved\Films\m3_gate.txt`; the table is in the M3 section of `docs/PRD.md`). The
review left the runs after 200 N*s and the step across as they are, and changed one thing: a get-up attempt is
given up once its clip has waited 5 s in all with nothing pressing on the body (`getup.wait_s`; a body had held
one pose for 13 s under a table's edge; scenario `getup_wait_bounded`). The stand is 49, 50 and 49 of 50 with 38,
44 and 44 at the first attempt, and no attempt lasts longer than 7.7 s.
DLSS is deliberately deferred to M5.

Everything is driven from the command line. Do not rely on the Unreal Editor GUI.

## Commands

Run from the repo root in PowerShell. Each script prints a one-line verdict first (`BUILD OK`,
`SCENARIO FAIL: ...`), returns a meaningful exit code and writes a JSON result under `Saved\`.

| Task | Command |
| --- | --- |
| Clean clone to verified build | `powershell -ExecutionPolicy Bypass -File Tools\bootstrap.ps1` |
| Compile | `powershell -ExecutionPolicy Bypass -File Tools\build.ps1` |
| Unit tests | `powershell -ExecutionPolicy Bypass -File Tools\test.ps1` |
| One scenario, headless | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -Name drop_passive` |
| One scenario with a screenshot | add `-Shot` (renders offscreen, saves `final.png` next to the metrics); `-ShotAt "3.07,3.25"` adds pictures at those simulated times (`shot_3.070.png`) |
| Debug overlays in the pictures | add `-Overlay all` to a `-Shot` run or to `play.ps1` (or layers: `com,support,capture,joints,assist,contacts,state,hands`); in a session the V key, in any console `Sandbox.Overlay all\|off\|toggle\|<layers>` |
| One scenario in the headset | add `-VR` (headset on, SteamVR running; takes the scenario's duration in real time) |
| One scenario with other body tuning | add `-Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"`, or `-TuningDir <dir>` for another set of tuning files |
| All scenarios | `powershell -ExecutionPolicy Bypass -File Tools\suite.ps1` (eight engine processes take the scenarios from one queue; also holds the whole set to 2 cm of joint stretch and a 1.5 ms mean physics step, and prints the worst scenario of each; `-Long` adds the scenarios over 60 s, `stand_npc_10min`; `-Parallel 1` for physics step times nothing disturbed; `-Tune "name=value,..." -Label <name>` runs the whole suite against other values into `Saved\Suites\<name>`, leaving what baselines are made of alone) |
| A motion as a sheet of pictures over time | `powershell -ExecutionPolicy Bypass -File Tools\film.ps1 -Name hit_chest_200_falls -Times "3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0"` (`-Camera side\|front\|back\|quarter`, `-Events <file>`, `-Tune`; `Saved\Films\<name>\sheet.png`) |
| What a recorded clip asks of the body, as a sheet of pictures | `powershell -ExecutionPolicy Bypass -File Tools\clip-film.ps1 -Clip M_ragdoll_getup_stand_B -ClipTimes "0,0.4,0.8,1.2,1.6,2.0,2.5" -Carry -RaiseCm 60` (the body hangs by its welded pelvis and is asked for the clip's pose at each time; `-Carry` moves the pelvis as the clip does; several clips separated by commas; `Saved\Films\clip_<name>\sheet.png`) |
| Keep the suite's results, or see what changed since | `powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save <name>` before a change, `-Compare <name>` after it (or `suite.ps1 -SaveBaseline <name>` and `-Baseline <name>`): which scenarios have other hashes, and what moved in them |
| Scenarios over a set of tuning values | `powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name legs -Scenarios "stand_npc,hit_head_30_stands" -Vary "muscles.legs.stiffness_nm_per_rad=400,800;muscles.legs.damping_s=0.03,0.06"` (every combination, or `-OneAtATime`; `-Metrics` chooses the columns; table in `Saved\Sweeps\<name>`) |
| Push matrix (160 pushes, outcome by the owner's M3 table) | `powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1` (`-PushSeconds 0.1` for pushes instead of blows, `-Tune` to try values, `-Impulses "30" -Enforce "30"` for one row, `-Rules m2` for the old gate; a step is a foot carried 10 cm or more, `-StepMinCm`, and shorter hops are reported as shuffles); against a baseline: `Tools\baseline.ps1 -CompareMatrix <name>` |
| The stand of 50 falls and get-ups (the criterion of M3 part 6: 90% end standing) | `powershell -ExecutionPolicy Bypass -File Tools\getup-stand.ps1` (26 falls on the bare floor, 8 each near a wall, the table and the chair; `-Seed 2` for other falls, `-Only "table,chair"`, `-Tune`, `-Film failed` for a sheet of pictures of every fall that does not end standing, `-Film "36,42"` of those trials; `-PressN 300` presses every fallen body down for 20 s and counts those that stood under it; a trial ends once its body has stood for 3 s, `-StopStandingS 0` runs each for the whole 40 s; the places are worked out from where the room spec has the table and the chair) |
| The stand on three seeds side by side, one line a seed | `powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label <name>` (`-Tune`, `-Seeds "1,2,3"`, `-PressN`, `-Only`; three engine processes a stand, nine in all; results in `Saved\Matrix\getup_seeds\<name>`) |
| Friction between the lying body and the floor, by experiment | `powershell -ExecutionPolicy Bypass -File Tools\surface-friction.ps1` (knocks the body down and drags it by the pelvis with `-ForceN 800` for `-PushS 1.0`; the friction from its acceleration; `-Tune` to try values) |
| Knockdowns from 8 directions, judged by the fall's criteria | `powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1` (a blow of 200 N*s on the chest from each; `-Impulses "200,300" -Bodies "spine_04,neck_01"` for harder ones and the head, `-Tune`, `-CVars`, `-Film` for a sheet of pictures of every fall) |
| One generated scenario file | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -File <path.json>` |
| Interactive session (a window, for a person) | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1` (`-Name <scenario>` opens that scenario's scene; keys are on the screen) |
| Self-test of the session, no window | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Smoke` (`-Smoke -Shot` renders it and saves `final.png`) |
| Record the hands of a session | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Record <name>` (from the first frame; the K key ends a recording and starts the next; `Saved\Recordings\<name>\`: the recording, a scenario that replays it, what the session saw) |
| A session's hands recorded and replayed, held against each other | `powershell -ExecutionPolicy Bypass -File Tools\record-replay.ps1` (a session without a person uses the hands alone, `play.ps1 -Demo`; `-Recorded <name>` for a recording a person made; `-Twice` also wants two replays with the same hashes; `-PelvisCm`, `-ImpulsePct`, `-Frames` are the limits) |
| Run-to-run deviation | `powershell -ExecutionPolicy Bypass -File Tools\repeatability.ps1 -Name drop_passive -IncludeRendered` |
| Compare two runs | `powershell -ExecutionPolicy Bypass -File Tools\compare-runs.ps1 -A <dir> -B <dir>` |
| Regenerate assets from specs | `powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1` (`-Force` also rebuilds up-to-date maps) |
| Bake a map's lighting | `powershell -ExecutionPolicy Bypass -File Tools\bake-lighting.ps1 [-Quality Preview] [-Force]` |
| Create, rig and assemble the MetaHuman | `powershell -ExecutionPolicy Bypass -File Tools\metahuman.ps1` (cloud steps need an Epic sign-in; see below) |

Typical times on the reference machine: incremental build 4 to 35 s, full build 80 s, one scenario 8 s
(the 10-minute stand scenario 60 s), unit tests 15 s, the whole suite 25 s (64 scenarios; 85 s with `-Long`), a record and replay 25 s,
the push matrix 50 s (one row of 40: 15 s), the knockdowns 18 s, the stand of 50 falls 66 s (three seeds side by
side 110 s), regenerating the physics asset 10 s, a film
of 12 pictures 15 s, the session's self-test 10 s, the whole
bootstrap about 5 minutes. Engine start-up is 14 s of a run against 1 s of physics, which is why runs share
processes; `SANDBOX_SEPARATE_PROCESSES=1` goes back to one process per run.

The loop: edit, `build.ps1`, `test.ps1`, `suite.ps1`, read the JSON, repeat. A task is done when its
scenarios pass, not when the code compiles. For anything visual, run a scenario with `-Shot` and look at the PNG.

Before a change to the body or its controller, take a baseline (`suite.ps1 -SaveBaseline <name>`); after
it, `suite.ps1 -Baseline <name>` names every scenario whose hashes moved. Scenarios the change should not
touch must not be in that list, and the ones that are get explained in the part's report.

## Layout

| Path | Holds |
| --- | --- |
| `Source/SandboxCore` | Shared services: log category `LogSandbox`, JSON helpers, `FSandboxParamStore` (tuning files, overrides, hot reload, the `Sandbox.Tune*` console commands). Depends on no other Sandbox module. |
| `Source/SandboxBody` | The simulated body: `ASandboxRagdollActor`, `USandboxBodyController` (muscles, balance assist and the states Standing, Flinch, Falling, Lying; the sense of balance, `GetBalance` and `MeasureBalance`: standing feet, support polygon, capture point; the decision on it, `IsStepNeeded`; `FollowSupport` moves the assist's anchors with the feet; `UpdateStrategies` turns the leg targets (ankle, hip, lean into a held push); `StartGetUp` and `UpdateGetUp` get a lying body up by a recorded clip (the state GetUp: the clip chosen by how the pelvis lies, its time waiting for the body, the pelvis's and the feet's assist aimed where the clip has them, the clip's root following the loaded feet); `ApplySurface` gives the bodies their friction and restitution (`surface.*`); `FeelFrame` splits what is felt into hit and pressure; `GetControlView` hands its targets to overlays), `FSandboxBodyTuning` (the body's tuning values by name, read from the store, poses among them), `FSandboxBodyProbe` (measurements: the support polygon, the capture point, energy, penetration into the world and into itself), `USandboxAnimInstance` (the target pose the muscles hold: a base pose blended from one to the next, turns added on top such as the flinch's; a native instance with its own proxy, no Animation Blueprint), `FSandboxPoseRig` (joint angles of a pose to bone rotations, on the axes of the physics asset's joints), `FSandboxIdleMotion` (the motion of a recorded idle clip, as each bone's turn from the clip's first frame, added to the pose of a standing body), `FSandboxClipPose` (a recorded clip made for another skeleton as target poses for the body's joints: each body turned, in the space of the whole body, by what the clip's bone is turned from its own reference pose, arms and legs pointed where the clip's point, every joint kept within its range; laid over the base pose by the animation instance's layer, `USandboxBodyController::SetClipPose`). |
| `Source/SandboxInteraction` | The player side. `USandboxEffectorComponent` (a hand: a sphere with mass) held by `FSandboxWorldSpring` (spring with a force cap to a moving world point); `ASandboxEffectorRig` (the hands plus an `ISandboxTargetSource`: desktop, scripted, recorded, later VR; it can keep the targets of every stepped frame, `SetRecording`); `FSandboxTargetRecording` (what the hands were asked for, frame after frame, as a text file) and `FSandboxRecordedTargetSource` (plays one back frame for frame); `FSandboxDragHandle`; `ASandboxPawn` (free camera, tools, time control, input made in C++), `ASandboxPlayerController`, `ASandboxHUD`, `ASandboxGameMode`; `FSandboxInteractionTuning`. |
| `Source/SandboxScenario` | `UScenarioBatchSubsystem` (the job queue: one engine process runs many scenarios, each in a fresh world, with its own `run.log`, after a warm-up run; `-ScenarioQueue=<dir>`), `UScenarioRunnerSubsystem` (also measures the hands from outside: penetration, tunnelling, what a strike moved; and for the body, every frame: balance, energy, rest after a fall, penetration, written to `series.csv`), `FScenarioSpec`, `FScenarioStage` (builds a scenario's scene), `USandboxPlaySubsystem` (the interactive session, `-Play=`; the only place where pawn and character meet in code), `USandboxOverlaySubsystem` (debug overlays, `Sandbox.Overlay`; here because it needs both the character and the hands), automation tests. |
| `Source/SandboxEditor` | Editor-only `GenerateAssets` commandlet; `FPhysicsAssetGenerator` builds physics assets from specs, joints from their anatomy (`ComputeJointFrames`). |
| `Specs/` | Text specs for generated assets (physics assets, room maps with their furniture); format in `Specs/README.md`. |
| `Scenarios/` | Scenario JSON files; format in `Scenarios/README.md`. |
| `Recordings/` | Recordings of a session's hands that scenarios replay (`"recording"` in a scenario file); new ones are written under `Saved\Recordings` and copied here to be kept. |
| `Tools/` | The CLI scripts above. `common.ps1` holds shared helpers (`Invoke-ScenarioBatch` hands scenario runs to a queue that several engine processes empty, for the suite, the push matrix, sweeps and single runs; a run's own log is `run.log`, the process's `process.log`). |
| `Config/` | Engine and game settings. Physics timing lives in `DefaultEngine.ini`. |
| `Config/Tuning/` | Every tunable number, as JSON: `body.json` (the character), `effectors.json` (the hands), `tools.json` (the desktop tools); names, units and ways to change them in its `README.md`. |
| `docs/` | `PRD.md` (plan), `testing.md` (repeatability policy and measurements). |

## Rules

- **Module dependencies.** `SandboxBody` and `SandboxInteraction` must not depend on each other; they meet
  only as rigid bodies in the physics world. Both may depend on `SandboxCore`. `SandboxScenario` may depend
  on all of them; nothing depends on it.
- **No Blueprint logic, no Animation Blueprints.** Gameplay, input and animation logic are C++.
- **Text is the source of truth.** Never hand-edit `.uasset` or `.umap` files. Generated assets come from
  `Specs/` through the `GenerateAssets` commandlet; imported art is imported by script.
- **Tuning values live in `Config/Tuning`, not in code.** A new tunable number gets a name in its module's
  list (`VisitValues` in `SandboxBodyTuning.cpp` for the body, in `SandboxInteractionTuning.cpp` for hands
  and tools) and a value in the JSON file; no default in C++. A value missing from the files, or a name the
  code does not know, is an error by design.
- **The pawn does not know the character and the character does not know the pawn.** The character feels a
  hand through its own contact events (`USandboxBodyController::OnBodyHit`); the drag handle pulls whatever
  body was clicked. What needs the character itself (the calibrated impulse, the reset) the pawn only
  requests through a delegate, and `USandboxPlaySubsystem` in `SandboxScenario` carries it out.
- **A scenario run has no pawn.** The runner replaces it with a bodiless spectator, so a scenario's world
  holds only what its file puts there. Hands in a scenario come from `effector` events.
- **Overlays only look.** `USandboxOverlaySubsystem` reads bodies through const accessors and listens to
  their contact events; it writes nothing to a body and changes no setting of one. Without a renderer it
  stays off, does not tick and listens to nothing. A run must give the same hashes with overlays and without.
- **Measuring changes nothing.** What the runner and the body's balance sense read (bodies, scene queries,
  contact events) must leave a run's hashes as they were. A new metric is checked that way before it is used.
- **The body can be interacted with at every moment** (owner's requirement, FR-18). In every state, getting up
  included, it is a simulated body whose muscles are asked for a pose: never kinematic, never played back, never
  deaf to a contact. A recorded clip is a target for the muscles, not a motion of the body, and every state has
  to say what a hit, a push and a grab do to it.
- **A body under control never sleeps** (`USandboxBodyController::Start` sets its bodies to never sleep). It
  has to sense its balance and answer a touch at once; see the pitfall about the solver's sleep below.
- **Time control never changes the physics step.** Pause, single step and slow motion decide on which
  frames the world steps; a step is always 1/90 s in 2 substeps. No time dilation.
- **Physics timing is fixed.** Synchronous physics, 90 Hz frame, 2 substeps (180 Hz). Scenarios force a
  fixed engine timestep. Do not read wall-clock time in simulation code. The solver's determinism mode is
  on (`p.Chaos.Solver.Deterministic=1` in `Config/DefaultEngine.ini`); every scenario run checks it. Every
  scene a scenario or a session builds also keeps the solver's background rebuild in step with the simulation
  (`FScenarioStage::KeepSolverInStep`), without which a fall depends on how busy the machine is.
- **Units.** Unreal units in code (cm, kg, cm/s). Scenario files and metrics use the unit in the field
  name (`_cm`, `_s`, `_ns` for newton-seconds, `_mps`).
- **Tests are named `DictatorSandbox.*`.** The filter is a substring match, and engine plugins already use
  the word "Sandbox".
- **Result files over exit codes.** Scripts decide success from the JSON a run writes; an editor process
  can exit 0 after an error.
- **Close the Unreal Editor before building.** It locks the module DLLs. Live Coding is not used.
- Use `UE_LOG(LogSandbox, ...)` for project logging.

## Things that have already bitten

- **`/Engine/Tutorial/.../TutorialTPP` is not a usable ragdoll.** It explodes when all bodies simulate.
  Use the stock mannequin copied by `Tools\fetch-stock-content.ps1`
  (`/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`).
- **Physics assets can contain non-rigid constraints.** The stock mannequin has one (`UserConstraint_0`).
  `FSandboxBodyProbe::MaxJointStretchCm` only measures joints with all linear axes locked.
- **Headless runs do not render, so nothing is "recently rendered".** Skeletal meshes must use
  `AlwaysTickPoseAndRefreshBones` and disable update-rate optimisation, as `ASandboxRagdollActor` does.
- **Keep the `AndroidFileServer` plugin disabled in the `.uproject`.** When it is enabled, the editor
  appends a section with a generated `SecurityToken` to `Config/DefaultEngine.ini` on first run, which
  must not be committed.
- **Toolchain.** Engine 5.8.4 from the Epic launcher; Visual Studio 2026 with MSVC 14.50 and Windows SDK
  10.0.22621. The MSVC 14.51 toolset breaks 5.8. `Tools\common.ps1` finds the engine from the launcher's
  install list, or from `UE_ROOT` if set.
- **Do not make one ragdoll body kinematic to hold it in place.** Kinematic bones follow the component, and
  the component follows the simulated root, so the body is dragged along. Constrain it to the world instead
  (see the `pin` event in `UScenarioRunnerSubsystem`).
- **The stock MetaHuman physics asset cannot be simulated as it is.** Three defects, all fixed by the
  generator for `PA_SandboxNPC` and all still present in `PHYS_SandboxNPC` (scenario `drop_metahuman`):
  joint anchors sit 1.5 to 3.6 cm apart on all 15 joints, because they were placed for another body's
  proportions (M1 reported only the worst one, `spine_02`); every joint has an orientation motor that pulls
  it away from the reference pose; and the foot shapes do not lie flat on the floor. See `Specs/README.md`.
  A body that jumps or changes shape in its first frames has one of these; `metrics.json` shows the anchor
  gap and the spawn-pose angles per joint.
- **Physics Control takes its muscle targets from the mesh's pose after the mesh has ticked.** With no
  animation that is the reference pose, constant, as wanted. World-space controls must not use that pose
  (`bUseSkeletalAnimation` off): it is relative to the component, and the component follows the simulated
  pelvis. `USandboxBodyController` ticks the Physics Control component by hand, after the mesh.
- **Leave Physics Control's acceleration mode off for muscles.** It is the plugin's default and scales each
  drive by the inertia of the two jointed bodies only: an ankle got about 18 N*m/rad and the pose crept
  under the body's weight. The controller uses force mode with stiffness in N*m/rad. Chaos multiplies
  angular drive stiffness and damping by 1.5; the controller divides that out.
- **Eight solver position iterations are not enough for a standing body.** A heavy body on light feet
  through stiff joints does not converge: the feet rock at 0.5 rad/s and walk across the floor. The
  controller asks for 64 on the NPC's own bodies (`solver.position_iterations`); it costs 0.4 ms per frame
  instead of 0.26. Shock-propagation and mass-ratio console variables made no difference.
  Measured again in M3 on a body that does not sleep, the gap is smaller: feet at 0.23 rad/s with 8, 0.12
  with 16, 0.15 with 64, pelvis drift 1.5, 1.2 and 1.0 cm. Part of the M2 figure was the sleep below.
- **Chaos puts a standing character's island to sleep, and the controller's next tick wakes it.** A body
  that holds still is still enough for the solver's sleep thresholds. Under control it then slept one
  substep in every six: no gravity, no contacts and no contact events in that substep. Nothing looked
  wrong; the feet reported five sixths of the body's weight, the balance assist's load read 0.047 of body
  weight instead of 0.0185, and feet at 8 solver iterations rocked twice as fast as they do awake. Found
  only because the floor's push on the feet did not add up to the weight. The controller's bodies are now
  `ESleepType::NeverSleep`. When a sum of forces or impulses is short by a simple fraction, suspect sleep
  (`-CVars "p.Chaos.Solver.Sleep.Enabled=0"` to compare).
- **`FPhysicsAssetUtils::CreateNewBody` switches collision off between the new body and all earlier ones**
  unless `FPhysAssetCreateParams::bDisableCollisionsByDefault` is cleared. The generated NPC asset had every
  one of its 120 pairs off from M1 to M3: no self-collision, a forearm 17 cm inside the chest after a fall.
  The generator's log listed "colliding bodies" from its own plan, not from the asset. Read such a fact
  back from the asset: `penetration.self_collision_pairs` in `metrics.json` is what the run-time asset
  allows (127 for the NPC), and a metric that reads exactly 0 in every run is a metric to distrust.
- **A strike scenario can pass because it lands on a lucky spot.** `strike_punch_forearm` passed M2 at
  10 rad/s; 2 cm higher the same punch snapped the wrist at 37 rad/s, and adding the collarbones moved the
  arm enough to land in that band. Sweeping the point of impact (and the stiffness: 11, 49, 41, 35, 30,
  38 rad/s for 100 to 300 N*m/rad) showed it. A figure that jumps back and forth as a setting changes
  smoothly is a scenario on an edge; strike more than one point (`strike_punch_forearm_heights`).
- **A muscle target that jumps puts energy into the body.** The flinch's pose offset first ended with the
  Flinch state; a hit that knocked the body over ended it in one step, the muscles snapped the falling body
  back, and the run gained 13.8 J (`max_energy_gain_j` caught it). Anything added to the target pose fades by
  its own clock, not with a state.
  The same holds for a foot placement switched on at full weight (the calf snapped at 84 rad/s): weights
  ease in over `poses.blend_s`.
- **A point fixed in the world is a moving target for a body that turns.** A leg placed at a world point
  on a body pinned by a ball joint tipped the body, the point moved against it, the leg chased it, and the
  body fell. Test a placement on a welded pelvis (`"lock_rotation": true` on a `pin` event).
- **A pose of zeros must leave a run bit for bit as it was.** `FSandboxPoseRig::Build` keeps the reference
  rotation itself for a joint at 0, and the animation instance writes only bones some pose has turned:
  `Ref * Frame * Identity * Frame^-1` need not round to `Ref` exactly (not measured; the code does not
  rely on it), and a hash moves with the last bit.
- **Chaos's inertia conditioning is global and slows a fallen body's rest.** Raising
  `p.Chaos.Solver.InertiaConditioning.Distance` (default 20) calmed the wrist and the standing feet, but a
  lying body then took 3.3 to 6.7 s to come to rest instead of 2.6. The project scales one body's inertia in
  its spec instead (`inertia_scale`, the hands).
- **Bodies that collide must not overlap in the reference pose.** They push each other apart from the first
  step: with the two thighs colliding (1.4 cm into each other) the standing character's feet turned at
  1.3 rad/s and the assist's load went from 0.05 to 0.22. Such pairs go under `collision.disable` in the
  spec; `penetration.spawn_self_cm` and the check `spawn_pose_clear_of_itself` find them.
- **Without its determinism mode, Chaos can give a run that differs from the one before it.** It gathers
  contact constraints from worker threads in the order they finish. On a busy machine one run of
  `drop_passive` in about 25 came out differently from the tenth digit on; 96 runs made on purpose did not
  show it again. The mode (`p.Chaos.Solver.Deterministic=1`) changes neither the usual results nor the step
  time. A baseline comparison that names a scenario the change cannot have touched is how this shows: run
  that scenario again before believing it.
- **A console variable the engine does not know is accepted in silence.** `-dpcvars` creates a dummy
  variable and the run is unchanged, which reads like "this setting makes no difference"
  (`p.Chaos.Solver.SleepEnabled` instead of `p.Chaos.Solver.Sleep.Enabled` cost a wrong conclusion for a
  few minutes). `Tools\scenario.ps1` now prints `SCENARIO WARNING` for such a name.
- **PowerShell variable names do not tell case apart, and a typed parameter converts what is assigned to
  it.** In a script with a `[string]$Metrics` parameter, `$metrics = Read-JsonFile ...` stores the text of
  the object, and every field of it reads as empty. Give locals names no parameter has.
- **A passive body runs on the engine's 8 solver iterations; the NPC's 64 come from its controller.** A
  figure taken from a passive scenario is not the NPC's: hanging from one hand, the wrist stretches by
  2.9 cm at 8 iterations and 0.5 cm at 64 (`-CVars "p.Chaos.Solver.Iterations.Position=64"` to compare).
- **The engine's joint axes: twist is the frame's X, swing1 its Z, swing2 its Y, and limits are symmetric
  around the parent-side frame.** An asymmetric range needs that frame turned to the middle of the range;
  `FPhysicsAssetGenerator::ComputeJointFrames` does it. The two swings are limited each by itself, as a box,
  not together as an ellipse: the engine's joint solver (the linear one, which every joint uses unless told
  otherwise) takes the turn apart into a twist and a swing and limits the swing's two angles separately
  (`FPBDJointCachedSolver::InitPyramidSwingConstraint`). The project believed in the ellipse from M2 to M3
  part 5, and added the collarbones for it.
- **`FConstraintInstance::GetCurrentSwing1`, `GetCurrentSwing2` and `GetCurrentTwist` are not the angles the
  solver limits.** They are the twist of the whole turn about each axis. Compared with the limits they read a
  wrist that swings and twists at once as 40 degrees outside a range it was inside of, and that made hard
  limits look as soft as soft ones. `FSandboxPoseRig::LimitExcessDeg` measures as the solver does; a run's
  `metrics.worst_limit_angles_deg` and `worst_limit_ranges_deg` show the joint that was furthest out. When a
  figure does not move with the setting that should move it, suspect the figure.
- **A soft joint limit of the stock asset is not soft.** Stiffness 50 and damping 5 in the engine's
  acceleration mode hold a joint within 1 to 2.5 degrees of its range in a knockdown; a hard limit holds it
  no better and bounces (46 J gained against 26, rest after 4.0 s against 3.6). What a joint does at the end
  of its range is written in the spec (`"limit"`), not taken from a template without knowing.
- **The pose of a fall is for meeting the floor, not for lying on it.** Held until the body was at rest it
  made a stiff shape of the body: knocked over by the head, it stood on its head with its legs in the air for
  a second, and on its back it kept its forearms up. The muscles let go `thresholds.impact_let_go_s` after the
  trunk or the head first meets the floor.
- **Continuous collision detection does not keep a heavy body on light feet out of the floor.** A passive
  body dropped from 1.5 m sinks 8.3 cm in and is thrown back out with 149 J, with the feet swept or not; with
  64 solver iterations instead of the engine's 8 it sinks 1.5 cm. It is the solver not converging, not a body
  passing between two steps. A body under control has the 64.
- **Do not judge "at rest" by the fastest body.** A hand trembling against the floor (0.5 kg) kept a lying
  character "falling" for good. The controller uses the mass-weighted root mean square over all bodies.
- **Scenario time and controller time differ by one frame:** the controller's first tick comes one frame
  after the body is spawned. Result files use the run's own frame count; do not mix the two.
- **A bash heredoc breaks on an apostrophe in its body when run through the agent's shell tool,** and one
  that survives has its backslashes halved (`\\n` arrives as `\n`, `\t` in a path as a tab). Write the
  script to a file with the file tool and run that.
- **Chaos ignores new solver iteration counts on a body whose simulation island does not change.** The
  island's counts are worked out only when something joins or leaves it (`FPBDIslandManager`; the engine
  source has a todo about it). Set before the first step they apply; set on a standing character they were
  silently dropped: same hashes as without the change. `USandboxBodyController::RefreshSolverIsland` adds
  a constraint that restricts nothing for one frame, which counts as a change. Scenario `tune_live_solver`
  guards it.
- **A tuning change that "applied" has not necessarily reached the solver.** The case above was accepted by
  every layer of our code. When a value becomes tunable at run time, change it in the middle of a run and
  check that the hashes differ from the unchanged run.
- **The JSON reader takes a number for a boolean and a boolean for a number.** `TryGetBool` succeeds on `1`.
  Where the kind matters (scenario expectations, tuning values), check `FJsonValue::Type`.
- **Automation tests fail on any logged error.** Code that tests call must hand its errors back
  (`FString& OutError`) and leave logging to the caller, as `FSandboxParamStore` does.
- **Regenerating a map discards its baked lighting,** and the bake re-saves the map. So map generation is
  skipped while the map is newer than its spec, and the bake keeps a `.bake.json` stamp beside the map to
  know whether it is current. File times alone cannot tell.
- **The first lighting bake on a machine takes about 90 s, later ones 12 to 17 s** (Preview quality, the
  test room).
- **`-nohmd` does not stop the OpenXR plugin from creating an OpenXR instance at start-up.** With SteamVR
  as the runtime and no headset on, that call took 26 s per launch and ended in "Instance is not viable".
  So every tool launch passes `-nohmd -DisablePlugins=OpenXR` (`Invoke-Editor` in `Tools\common.ps1`); a VR
  run must leave both out. The plugin stays enabled in the `.uproject`.
- **Do not switch anti-aliasing back to TSR for VR.** At the headset's render target it cost 8.5 ms by
  itself and halved the frame rate. Renderer experiments go through `Tools\scenario.ps1 -VR -CVars "a=1,b=2"`,
  and every rendered run records the deciding console variables in `metrics.json` (`renderer`).
- **A simulated body at rest falls asleep, and a new target for a joint drive does not wake it.** A hand
  that had rested for 2 s stayed where it was while its spring asked for 1900 N. `FSandboxWorldSpring` wakes
  the body it pulls on every update.
- **The engine caps a joint drive's force axis by axis, not by size.** Along a diagonal a drive with one
  cap for all three axes is up to 1.7 times stronger than that cap. `FSandboxWorldSpring::AxisCapsN` sets
  the three caps every frame in proportion to the direction of the force asked for.
- **A drive's target is measured in the frame of the joint's second body.** With `InitConstraint(Body,
  nullptr)` that is the world, so with an identity `Frame2` the targets are world poses. (Chaos solves with
  the second body first: its index 0 is the engine's `Body2`.)
- **A contact's peak force is not limited by a spring's force cap.** A moving hand that is stopped gives up
  its momentum too: 14 N*s in one frame reads as 1270 N. Check a cap with a hand held against something
  (`final_contact_force_n`), as `effector_force_cap` does.
- **A substep in which a body sleeps has no contact event.** Contact events exist only for contacts that
  carried an impulse in that substep, so anything added up from them is short by the substeps slept
  through. See the pitfall about the solver's sleep.
- **Contact events arrive once per pair of bodies and substep, and the impulse vector's sign follows the
  solver's order of the pair.** Use `Hit.ImpactNormal` times the impulse's size to add them up. The floor
  under the feet reports the character's weight in every frame (8.7 N*s at 90 Hz, above the flinch
  threshold), which is why the character counts only contacts with other simulated bodies.
- **A skeletal mesh's bodies ask for contact events only when the component's own flag is set too.**
  `USkeletalMeshComponent::SetNotifyRigidBodyCollision` sets the flag of every body and not the component's
  `BodyInstance.bNotifyRigidBodyCollision`, and `FBodyInstance::BuildBodyFilterData` wants both. With the
  bodies' flags alone the character heard of a contact only when the other body asked for events itself, as
  the hands do: every hand scenario passed while the floor, and any prop to come, went unreported. Found
  in step 5 because the overlay had no foot contacts to draw. `USandboxBodyController::Start` sets both.
- **A body spawned in the middle of a frame is ticked in that frame, before its mesh has a pose.** The
  session's reset does that: the new character's controller ran with no bone transforms to take the muscles'
  targets from, and Physics Control logged 30 warnings ("Failed to find bone data", two per muscle) that no
  script looked for. The controller now waits for the mesh's first pose. A body spawned before the world's
  first tick never had this, so no scenario changed.
- **A component's physics state is destroyed before its `OnUnregister` runs.** A constraint on the
  component's own body is released in `OnDestroyPhysicsState` (`USandboxEffectorComponent` does so for its
  spring), not in `OnUnregister`.
- **Chaos sweeps a body with CCD only when it moves more than 0.4 of its smallest extent in one substep**
  (`p.Chaos.CCD.EnableThresholdBoundsScale`). The 9 cm hand at 6 m/s stays under that: the run is identical
  with CCD switched off. From 6.5 m/s it is swept. With CCD nothing tunnels up to 28 m/s; without it a
  hand at 25 m/s ends 6.5 cm inside the chest (`-CVars "p.Chaos.Solver.UseCCD=0"` to compare). Far above
  strike speeds CCD has a price: from 22 m/s some hits deliver about half the impulse they do without it.
- **What a strike does is over long before a run's last frame.** `-Shot` alone shows a standing character.
  Use `-ShotAt` with the times of the hit, and `-Overlay all` to see the contact and the flinch.
- **Overlay text reaches screenshots through the HUD's actor overlays.** With `AHUD::bShowOverlays` set and
  the actor added by `AddPostRenderedActor`, `DrawHUD` calls the actor's `PostRenderFor` with the scene
  canvas, whatever class the HUD is (the engine's plain `AHUD` in a scenario run, `ASandboxHUD` in a
  session). `AHUD::OnHUDPostRender` hands out the debug canvas instead. The lines are a `ULineBatchComponent`
  the overlay owns, emptied and filled again every frame, in the foreground depth group so that they show
  through the character.
- **The character's bodies do not answer the visibility channel** (the engine's `Ragdoll` profile ignores
  it). Aim traces go by object type (`ASandboxPawn::TraceAim`).
- **With a fixed timestep the engine does not limit the frame rate.** An interactive session would run as
  fast as the machine allows. `USandboxPlaySubsystem::HoldFrameRate` waits each frame out; it is the one
  place that reads the clock, and it must stay outside simulation code.
- **A pause set during a frame takes effect on the next one.** The pawn decides at the end of its tick
  whether the next frame steps. The engine also stops updating the camera of a paused game unless the
  player controller has `bShouldPerformFullTickWhenPaused`, and input actions need `bTriggerWhenPaused`.
- **A static mesh component with the engine's basic sphere did not become a simulating body here** (valid
  body, `IsInstanceSimulatingPhysics` false; the cause was not chased). Hands are `USphereComponent`s with a
  mesh attached for looks.
- **A sustained push by a hand made the character flinch again every time a flinch ended** (0.35 s), until
  it fell: each contact frame added to the felt impulse. Since M3 part 3 only the impulse beyond the
  smoothed pressure is a hit (`reactions.pressure_s`): one flinch as the hand lands (`effector_hold_chest`).
- **Add a tuning name and its value in the same build.** A build that does not know a name refuses the
  file, so a new tuning file dropped in while an older build is running a suite fails every active scenario
  after it ("is not a tuning value").
- **Scripts target Windows PowerShell 5.1.** No `&&`, ternaries or null-coalescing.
- **`| Select-Object -First N` stops the script that feeds it.** `suite.ps1 -SaveBaseline x | Select-Object
  -First 15` ended the suite after 15 lines, with exit code 255 and no baseline. Redirect to a file and filter
  that (`*> file; Get-Content file | ...`).
- **A capture point compared with the support "now" lets the assist hold anything.** It runs away by
  e^(omega t) while a step is under way; compared now with a 30 cm reach, a 200 N*s push was held by the assist
  with 110% of body weight. Compare where it will be when a step lands (`balance.step_time_s`).
- **A decision that waits lets the assist take back the blow.** The capture point is the whole body's momentum
  and a blow gives it in one step; held for 0.05 s, a 200 N*s blow was undone by the assist (three times the
  body's weight) in three frames. Losing balance is decided at once; only "a step is needed" waits.
- **A foot below the standing threshold is still support.** After a sideways blow the far foot carried 14 to
  30 N, under the 20 N of a standing foot; the support polygon shrank to one foot and a step "was needed".
  Decisions take every foot the floor touches (force above 0).
- **Leg turns must not depend on each leg's load.** Weighting the strategies' turns by load gave a feedback
  loop: the lighter leg turned less, the body rolled onto the other, and under a held push it rocked from foot
  to foot with the assist at 0.43 of body weight.
- **Sideways, turning the ankles' targets after a blow lifted the unloaded foot, whichever way it turned**
  (`balance.ankle_side_gain` is 0). A parallelogram lean (ankles and hips together) did the same.
- **Leaning into a held push needs the ankle targets to tilt.** Moving only the strategies' reference and the
  assist's anchors left the ankle muscles holding the upright pose: the assist carried the whole 40 N push.
- **A filter that lets a blow count as pressure counts its momentum twice.** A blow's momentum is in the
  capture point already. The lean and the decision use the force felt in every frame for
  `reactions.held_push_s` (0.3 s); a shove of 0.1 s is not a held push.
- **A hand held at a point pushes harder the more the body leans into it.** Moving the target 4 cm out lowered
  the force only from 138 to 104 N. Test a lean with a scripted force (`impulse` with `duration_s`).
- **A falling body kept reaching for a placed foot's point** and gained 1.7 J; the controller now lets placed
  feet go (eased) when it falls.
- **`Sandbox.Tune` with a value out of range changes nothing, and the run goes on.** A scenario's console
  event did that (`step_reach_cm=500`, range 300): check `control.tuning_changes` after such an event.
- **Physics Control damps a muscle against the target pose's own turning speed** (`AngularTargetVelocityMultiplier`,
  1 by default). A target that jumps in one frame is then a blow: a leg's target that moved 20 cm at lift-off put
  600 J into the body and snapped the calf at 84 rad/s. The muscles' `pose_velocity_share` is 0; only the swinging
  leg, whose target is a smooth path, follows its target's speed (`balance.swing_pose_velocity_share`).
- **A leg's target must never jump, whatever changes under it.** Every change of a foot's point or of the frame
  its leg is solved in glides from what the leg was solved for last (`FFootPlacement::GlidedPoint`,
  `FSwing::StartOffset`), not from where the foot happens to be.
- **The first scenario an engine process runs differs bit for bit from every later one,** and later ones do not
  depend on what ran before them. The job queue (`-ScenarioQueue`, `UScenarioBatchSubsystem`) therefore runs
  `Tools\batch_warmup.json` first in every process, and a single `scenario.ps1` run is a queue of one job; with
  that, 48 scenarios gave the same hashes in one process, in eight and in twelve.
- **A foot that has just landed stands on an edge.** The decision took the edge for the whole support and ordered
  a second step after a good one; while the foot settles (`balance.step_settle_s`) its whole sole counts.
- **Braking a stagger through the planted legs lifts the near foot** when it is strong (`balance.stagger_damping_s`
  0.2 against 0.15): the body rebounded off the new support and stepped again.
- **A body that decides it cannot be saved goes over like a plank.** Until the audit of November 2026 a capture
  point beyond one step's reach meant Falling at once: the muscles let go and the body tipped over its heels
  without having moved a foot, also after an 80 N*s push backward that two steps catch. It now always steps, and
  falls when the pelvis tilts or sinks (`thresholds.fall_*`).
- **Passing every check is not moving like a person.** The suite and the push matrix were green while the body
  stood with its arms held out, fell like a plank and stayed frozen in a lunge. Look at the motion
  (`Tools\film.ps1`: one sheet of pictures over time) before calling a behaviour done.
- **A falling body needs a pose of its own and legs that are let go in time.** Holding the stance, it fell
  straight; with a fall pose but its placed feet released over `poses.blend_s` (0.4 s), a body that fell after a
  step met the floor with straight legs. Placements are released over `poses.fall_blend_s`.
- **"Lying" must not wait for "motionless".** With the old rest thresholds (0.05 m/s for 0.5 s) the fall's pose
  kept the body curled up on its back, rocking, and it came to rest after 3.5 s; lying as soon as it is down and
  nearly still (0.3 m/s, 1 rad/s, 0.2 s) it rests after 1.7 s.
- **The collarbones are trunk, not arm.** In the arms' group, with arms soft enough to give way to a hit
  (40 N*m/rad), each collarbone sagged 10 degrees under its arm and laid the arm against the body.
  `FSandboxBodyTuning::GroupOfBone` puts them with the spine.
- **The muscles of a falling body do work** (10 to 30 J to fold the legs and put the arms out), so
  `max_energy_gain_j` of a fall is no longer near 0; the falling scenarios allow 40 J. Upright it is still 1 J.
- **A step that brings the feet back together after a lunge is not an asked step from a stance**
  (`UpdateRegroup`). Four things kept it from working, each found by reading what else acts on the body, none by
  tuning. The ankle strategy's reference was the middle of the feet: it brought the body back while the weight
  shift took it forward (5 cm of 19), so a shift is now part of that reference. The foot left behind held the
  pelvis 15 cm down for a reach it no longer needed: the body sat on the bent stance leg with its centre of mass
  over the heel; a leg lowers the pelvis only by the share of the weight it carries. Waiting for the body to
  stand over the stance foot took a goal far beyond it (the body went on past the foot): the foot lifts when the
  step will hold, the capture point at landing inside the support to come. And once the foot has landed the
  anchors glide from where the body is, so what was left of the shift counted twice and threw the body off the
  far side.
- **"Not upright" is not "falling to the floor".** A body hung up by one hand took the pose of a fall backward
  and held it for good (119 J of muscle work, a pendulum with its knees at its chest). A body that touches
  nothing of the world and does not come down is held up (`UpdateHeld`, `thresholds.held_*`) and hangs in its
  stance's pose; the state is still called Falling, or Lying once it is still.
- **A body on its toes still has its feet under it.** The support polygon is what touches the floor; with the
  heels 1.5 cm up it is the toes alone, and a capture point under the raised heels counted as outside the
  support: a body hit on the back of the head stepped back twice (the one open cell of the push matrix from part 4
  to the audit). Decisions (`DecisionMarginCm`) take the whole sole of every foot the floor touches.
- **A foot on its toes has a sole polygon of its toes only** (`FSandboxBodyProbe::SupportPolygon` takes what lies
  within `sole_tolerance_cm` of its lowest point). Its middle is 12 cm ahead of the whole sole's: where a foot
  will stand, or the place over a foot, is taken with a tolerance that takes in the whole sole.
- **A motion of a few centimetres does not show on a sheet of pictures.** The idle moves the hands by 4 to 7 cm
  and the head by 4; the film's tiles of a standing body with and without it look the same. Such a motion is
  measured (`body.bodies[].settled_travel_cm`, `metrics.settled_head_travel_cm`) and held by a check
  (`min_settled_head_travel_cm`); the look of it is for the owner in a session.
- **A body that moves by design needs limits that say so.** With the idle, `stand_npc` broke its limit on the
  fastest body after settling (0.05 m/s, written for a statue; the hands go at 0.08) and `pose_lean` its
  `min_pose_offset_deg` of 29.9 for an elbow asked for 30 (the idle turns it by up to 3). Jitter is still
  watched by the angular speed and the joint speed.
- **The idle clip is read from its keys, not from its compressed data** (`FSandboxIdleMotion::Load`): the editor
  builds the compressed data in the background, and a run must not depend on whether that has finished. The
  clip is Epic's content, fetched and not committed, and so is nothing derived from it.
- **The solver's determinism mode does not make a falling body repeat on a busy machine.** Chaos rebuilds the
  structure it finds colliding pairs with in a background task and takes it in on whichever step the task is
  done by; a pair, and with it a contact, then comes a step earlier or later. With 16 processes side by side
  3 to 18 runs of 100 of a scenario with a fall differed (always the same few other hashes), none in a single
  process, and no standing body ever did. `FScenarioStage::KeepSolverInStep` waits for the rebuild before every
  substep and lets none run during it. A run that differs "once" is a defect to measure, not to rerun: the same
  scenario 160 times side by side tells in a minute.
- **Moving a file is not a way to take a job from a shared queue.** Windows renames through a handle; two
  processes can each open the file before either renames, both renames succeed, and neither process can then
  read "its" file. About one job in a thousand was run by nobody. A job is taken by writing a marker while a
  lock file is held open for writing (`UScenarioBatchSubsystem::TakeJob`), and a job without a result now prints
  `QUEUE WARNING` and keeps the processes' logs under `Saved\Queue`.
- **Letting a clip run further ahead of the body leaves less room before the body counts as lost.** With the
  pelvis allowed 16 cm of height and 20 cm of floor behind the clip instead of 12, the stand went from 45 to 48 of
  50, and a 30 N*s push that the get-up had ridden out now put the pelvis 31 cm off, past `getup.lost_cm` at 30:
  the suite's `getup_push_weak` caught it. The two limits move together (lost is 40 now, and the 200 N*s blow still
  fells the body at 45).
- **A figure from one seed is luck.** With the clip waiting for a pelvis 12 cm off in any direction, the stand
  gave 48 of 50 on seed 1, and 45 and 45 on seeds 2 and 3: on the criterion's edge. A setting is chosen on three
  seeds (`Tools\getup-stand.ps1 -Seed`), and a single failed fall that changes with a setting is no finding.
- **A clip must not wait without end for what will not come.** A foot that lay across the other leg was 29 cm
  from where the clip's first key had it and off the floor; the clip waited for it through six attempts. The wait
  for a foot is bounded (`getup.foot_wait_s`); the pelvis's height is what a get-up really has to wait for.
- **The same clip from the same place fails the same way.** A body that gave a get-up up tried again by the same
  clip, five and six times. The next attempt takes another clip if one starts at all like the body lies.
- **A body knocked into a wall often does not fall.** Three of eight blows toward a wall 45 to 90 cm away ended
  with the body on its feet against it. A stand of falls draws such a trial again and says how many it drew.
- **A body appears in its reference pose, the arms out, and brings them down in half a second.** The fingertips
  are 57 cm to either side of its axis, at the height of a chair's back. Put 45 cm from a thing at its side, a
  finger appeared 0.12 cm inside the chair and 0.79 cm inside a wall, and no check said so. A thing beside a body
  is 65 cm away or more; `Tools\getup-stand.ps1` refuses a place where the arms would touch
  (`penetration.spawn_world_cm` of a run shows it: 0.034 is the soles on the floor).
- **`body.bodies[].final_location_cm` is a body's centre of mass from where the character was spawned, not a place
  in the world.** Read as world coordinates in a room, it put eleven bodies "standing inside the table". World
  positions over a run are in `trace.json` (bone origins, every 0.5 s).
- **A get-up that is given up moves the body.** Each failed attempt shifts it by half a metre; a body that fell at
  a wall 166 cm from the table stood, at the fourth attempt, 30 cm from the table. A fall "at a wall" stays one
  only by distance: the stand has those falls in the room's corner quarters and reports how near each came to
  the furniture.
- **A clip that stays where it was put makes the assist drag the body over the floor.** A body that turns into a
  clip's first pose, rolls over or comes onto hands and knees moves 20 to 35 cm, and the clip waited there for the
  pelvis to be pulled back: half the assist's force went into that, and with a weaker assist the clip waited for
  good. The clip's root follows the pelvis while the feet carry nothing (`getup.pelvis_follow_s`). The assist was
  believed to be lifting a body whose arms were too weak; the log line of the failed attempts said otherwise
  (pelvis 30 cm off over the floor, 4 cm in height).
- **An assist that is capped always pulls at its cap,** so its share of body weight measures the caps and not the
  need. Sweep the caps down against the stand and read what fails, not the share.
- **A scripted hand goes to a point of the world and slides off a body that moves.** Of the two hands of
  `getup_held` one ends on the floor; "two hands only slow a get-up" was measured on one hand from part 6 on, and
  a threshold set between "one hand" and "two hands" from those runs separated nothing. A hand's sustained push
  also reads above its spring's cap (up to 342 N for 300 N: its weight and the body's push against it). Read
  `effectors.list.<hand>.touched_impulse_ns` before believing which hand pressed on what; a press of a known
  size is a scripted force (`impulse` with `duration_s`).
- **A scenario that starts with a fall is written for where that fall puts the body, and every change to the
  body moves it.** With the stance's elbows 20 degrees straighter the blow of `getup_*` left the body on its back
  instead of half on its side: a scripted hand pressed the empty floor, a get-up took 7.3 s instead of 3.7 and
  ran out of its scenario's time, a pressed body needed two attempts instead of one. "300 N costs a get-up 0.2 s"
  had been that one fall; on 150 falls a third of the bodies stay down under it. What something does to a get-up
  is asked of the stand (`-PressN`, `-Tune`, three seeds), and a single scenario holds only what every fall
  shows.
- **No strength of the body draws a line between a light press and a heavy one.** Its legs lift 600 N as they
  lift 300 N, or neither, by the phase of the clip the press finds it in; stiffer legs spoil the get-up nobody
  hinders. What a press does is a rule on the force the body feels (`getup.hold_force_n`).
- **"The elbow cannot straighten" was written down and believed for two parts; the joint can.** The stance never
  asked. Before planning a change to the asset for something a joint "cannot" do, ask it for the pose
  (`-Tune "poses.stance.<joint>.swing1_deg=..."`) and look.
- **A clip steps a foot that the body is standing on.** A recording brings a foot forward while its weight is on
  the hands or the other leg; the simulated body's weight is where its own feet, hands and friction put it. With
  the clip's time waiting only for the pelvis, the body stood up leaning like a plank from a foot 45 cm behind where
  the clip had it; made to wait for the feet too, it waited for good, at 16 cm, for a foot that carried 525 N. A
  foot the body stands on is where it is: the clip's root follows the loaded feet (`getup.frame_follow_s`), and the
  clip waits only for a foot that is off the floor.
- **The assist of a get-up takes back a blow unless it is capped and the body may be lost.** With 500 N on the
  pelvis and `getup.lost_cm` at 45, a 200 N*s blow moved the body 40 cm and the assist had it back in half a second
  with 1.2 times its weight. Capped at 250 N (feet 50 N) and lost at 30 cm, the same blow fells it. Sweep the caps
  down to what the body still gets up with; the share is in `control.get_up`.
- **A scenario about a fall must keep its body down.** With the get-up on for every body, the fall scenarios and
  the knockdowns would measure "at rest for good" at the end of a get-up. A scenario's body gets up only when its
  file says `"get_up": true`; the session's does by default.
- **A clip made for another skeleton cannot be copied bone by bone, even with the same bone names.** The sample's
  mannequin and the MetaHuman body differ in their reference poses by 4 to 21 degrees a bone, partly in how the
  axes lie and partly in how the limbs are held (upper arm 11.6, forearm 12.8, thigh 4.0 degrees apart in
  direction). `FSandboxClipPose` carries over what each bone has turned from its own reference pose, in the space
  of the whole body, and aligns the segments of arms and legs; a run's `control.clips` has the figures.
- **A recorded get-up asks joints for more than the body's have.** Over the sample's four get-ups: the wrist 73
  degrees beyond its range, the foot 48, the shoulder's twist 43, the hip 29. Asked as recorded, the muscles
  pushed into the limits and the mean pose error was 7 to 12 degrees; with every target held to its joint's range
  (`FSandboxPoseRig::ClampToRange`) it is 3. The wrist is a defect of the model, not of the clip: the forearm's
  turn is in the wrist's twist, the two swings are limited in the forearm's frame, so a hand turned palm down has
  its bending back limited by the 25 degrees meant for the sideways bend.
- **A body whose pose is held from outside must not judge itself.** Carried through a get-up by its pelvis for a
  film, it took itself for fallen as soon as the pelvis was laid down, went limp in the Lying state's muscles,
  and the film showed a standing body with its head and trunk hanging. Nothing failed: a film drops the
  scenario's expectations. While a clip's pose is asked for (`SetClipPose`) the controller decides nothing about
  balance; look at the state log of a run made for looking.
- **A weld gives way to nothing.** A body carried by its pelvis through a get-up on the floor (`pin` with
  `follow_clip`, `raise_cm` 0) is pressed 10 cm into the floor and its joints stretch by 6 cm: its limbs are in
  the way and the weld does not care. And the first move, from standing to lying within `poses.blend_s`, swings
  the legs through the floor even at 60 cm (joints at 160 rad/s for a moment). The rig is for looking at poses in
  the air.
- **The sample's clips log load errors that are not failures.** Each names anim notifies of the sample that were
  not copied (`BP_AnimNotify_FoleyEvent_*`); the keys load. An automation test must not load one: it fails on any
  logged error.
- **A contact's friction is not the body's.** The engine combines the two surfaces of a contact, by the average
  unless one of them asks for a mode with a higher number (the smaller, the product, the larger), and the floor
  and the hands have the engine's 0.7 and 0.3: `surface.friction` 0.3 is 0.5 on the floor, and under the average
  no value of the body gives less than 0.35. A material's "override combine mode" flags are read by nothing. What
  the floor gives is measured (`Tools\surface-friction.ps1`), and a new value of a material reaches the solver
  only through `FChaosEngineInterface::UpdateMaterial` (`USandboxBodyController::ApplySurface`).
- **A passive body keeps the engine's default surface.** The surface values are the controller's; `drop_*` and
  `impulse_passive` slide and bounce on 0.7 and 0.3.
- **More grip is not more stability.** With the soles at 0.9 on the floor a blow of 200 N*s on a thigh tripped
  the body where it had stumbled and stood, and after a push sideways it froze on crossed legs, in Stagger, for
  the rest of the run; the push matrix called both "stepped" or "fell" as allowed and passed. And a body with 0.9
  against the floor gets up worse: turning into a clip's first pose and coming onto hands and knees it has to
  slide.
- **"In motion" by the mass-weighted speed lasts long after a body has stopped:** limbs go on settling above the
  threshold. Friction worked out of the time a dragged body was in motion read 0.55 where the acceleration gave
  0.69. Check a new measure on a case whose answer is known.
- **A step counted from outside is a foot that left the floor for 0.1 s and stood again 5 cm away, and a
  shuffle is just over that.** As a stepping foot lands, the other one hops 5.0 to 6.1 cm, and the push matrix
  read one recovery step as two: the cells that 32 to 48 solver iterations and most weaker assists "lose" in the
  30 N*s row are that, by the controller's own log (`control.recovery_steps` against `control.steps`). Before a
  failed cell is taken for behaviour, read both. (The push matrix counts a step from 10 cm since the owner's
  decision of 24 November; the run's own record still starts at 5 cm.)
- **Two questions asked of two different supports leave a gap between their answers.** "A step is needed" was
  asked of the whole soles with 4 cm allowed, "the body stands again" of the part of the soles that touches. A
  body on the edges of its feet, 2.4 cm outside the one and 6 cm outside the other, did neither, for good, and
  rested there because the stagger's upright hold on the pelvis (up to 150 N*m: 19 cm of an 80 kg body) carried
  it. An allowance for what a body in motion brings back does not hold for a body at rest; a body at rest with
  its centre of mass off its support is being held by the assist. When a state is never left, write down every
  condition of every way out of it and look for the values that satisfy none (`series.csv` has the three
  margins: `com_margin_cm`, `capture_margin_cm`, `decision_margin_cm`).
- **A run that ends in Stagger is not always stuck:** a step that brings the feet back together is made in
  Stagger, and one that lands a moment before the run's end leaves that state in the result.
- **A push matrix of 160 of 160 can stand on a threshold.** The blow of 30 N*s on the head from behind and to a
  side leaves the capture point just under the 4 cm that call for a step; any small change to the body puts it
  at 4.4 cm, the body steps, and the cell is judged by how its feet shuffle.
- **An engine process takes 1.75 GB.** Fifteen side by side (three stands at once) took 26 GB of 32 and the
  batch was stopped for want of memory. Nine is the most that run at a time (`Tools\getup-seeds.ps1`).
- **PowerShell hash keys do not tell case apart either.** `@{ s = 0; S = 0 }` does not parse, and a script that
  does not parse fails at once: run a new helper once in the foreground before a long batch in the background.
- **A run that only asks whether the body ends standing need not run to its end.** Most bodies of the stand are
  on their feet 8 s after the blow and were simulated for 40 s. `end_when_standing_s` ends a run once every event
  is over and the body has been Standing for that long without a break; checked against the full-length runs
  trial by trial before it became the default.
- **A `TUniquePtr` to a type that is only declared does not compile in a `UCLASS`** (C4150 from the generated
  code); `UScenarioBatchSubsystem` owns its log device through a plain pointer.
- **A count that starts again whenever there is progress does not bound a wait.** `getup.stall_s` (3 s) counts
  while the clip's time stands still; a body that hovers at the lag lets the clip move on by a frame now and
  then, and an attempt lasted 15.6 s, 13 of them in one pose. A wait is bounded by a total (`getup.wait_s`). When
  a limit "cannot" have been passed, look at when its counter is set back.
- **A gate's figure is measured at the gate.** "Under 300 N two bodies of three get up" was measured in step 1 of
  part 7 and carried to the gate; step 2 changed the body's friction and nobody ran the pressed stand again: it
  was one of three. A change to the body is followed by every measurement that a written claim rests on, not only
  by the suite.
- **A scripted force is felt as a press; a pin is not.** `impulse` with `duration_s` goes through what the body
  feels (`SustainedN`), so a get-up under it is "pressed" or "held down". To keep a body from rising without
  pressing on it, pin it (`getup_wait_bounded`).
- **A session and the scenario that replays it are two runs, not one.** A session's process runs no warm-up
  scenario, and its hands exist from its first frame and are made in another order relative to the body. A
  recording of the hands replayed as a scenario agreed with its session in every figure through the first contact
  and was a frame and 0.27 cm off after it. Hold a replay to its session by figures with limits
  (`Tools\record-replay.ps1`) and to another replay by hashes; "the same hashes as the session" was planned and
  could not be.
- **A recording's first frame belongs to one step of the body's life.** The pawn counts the frames the world
  stepped; the session knows on which of them its character was put into the world; the difference is
  `recording_start_s`. A reset during a recording makes a scenario that cannot end as the session did (the
  session says so in its log).
- **Editor Python needs `PythonScriptPlugin` enabled in the `.uproject`.** Without it `-ExecutePythonScript`
  is silently ignored and the editor just sits open.
- **MetaHuman needs the engine option "MetaHuman Creator Core Data"** (Epic Games Launcher, engine tile,
  Options; about 6 GB). Without it the log says "Optional Content folder not found", skin textures are not
  synthesised and the assembled character is incomplete.
- **MetaHuman assembly raises script errors that are not failures.** `build_meta_human` throws "Cannot break
  link ..." for Control Rig graphs in the engine's own face assets even when the assembly succeeds; the
  script judges the build by the assets it produced and reports the message as a warning.
- **A MetaHuman is two skeletal meshes** (body and face) plus a Blueprint that joins them. Loading only the
  body mesh gives a headless character.
- **MetaHuman scripting.** `Tools\metahuman.ps1` runs `Tools\python\metahuman_build.py` in the full editor
  (not a commandlet). The `rig` and `textures` steps call Epic's cloud; the first run opens a browser for an
  Epic sign-in, which a person must complete, and later runs reuse the stored sign-in. Never pass account
  credentials on the command line. The rig state is not exposed to Python; `can_build_meta_human()` is true
  only when the character is both rigged and textured.
