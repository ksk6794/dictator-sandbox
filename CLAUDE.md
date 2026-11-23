# DictatorSandbox

Unreal Engine 5.8 C++ prototype: one photoreal room, one active-ragdoll NPC the player can hit, push, grab
and drag, on desktop and PCVR. Requirements, architecture and milestones are in `docs/PRD.md`.
Current milestone: **M1 done** (MetaHuman by script, physics asset and room from JSON specs, baked lighting,
launch in the headset, 7.1 ms of GPU time per stereo frame). **M2, active ragdoll core: all five steps
are built and its six criteria are met; the owner's hand check of the desktop session is deferred and does
not hold up M3** (the deferred list is at the end of the M2 section of `docs/PRD.md`).
The NPC stands under full simulation for 10 minutes (muscles and balance assist through Physics Control);
every tuning value lives in `Config/Tuning`, is hot-reloaded and can be overridden from the console and
the command line; the body has the states Standing, Flinch, Falling and Lying, joint limits from anatomy
that mirror left to right, and passes the push matrix; the player has physical hands (effectors: bodies
with mass on force-capped springs) that the body feels through its own contact events, a desktop pawn with
test tools, and scenarios can script the hands; debug overlays show the controller's work in a session and
on scenario screenshots; slaps at 6 m/s and punches at 8 m/s on head, chest, thigh and forearm pass without
tunnelling. **M3, balance and recovery, is in progress: parts 1 to 3 of 7 are done** (the plan and the parts are in
the M3 section of `docs/PRD.md`). The body senses its own balance (standing feet, support polygon, capture
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
part 7. Part 3, standing balance: the capture point decides whether the body stands, needs a step (held by
the assist until part 4, reported as `step_needed_frames`) or falls; ankle and hip strategies are turns of the
leg targets; the assist's anchors follow the feet; a held push is leant into and makes the body flinch only as
it lands; quiet-standing assist share 0.0052 (bound 0.0093); the push matrix names each outcome (stood, held,
stepped, fell) and judges it by the owner's table (the 80 N*s row from part 4). `push_thigh_200_falls` carries
loosened limits (4 s, 4 J) until part 5. Next is part 4, Stagger and the recovery step.
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
| All scenarios | `powershell -ExecutionPolicy Bypass -File Tools\suite.ps1` (eight at a time; also holds the whole set to 2 cm of joint stretch and a 1.5 ms mean physics step, and prints the worst scenario of each; `-Parallel 1` for physics step times nothing disturbed) |
| Keep the suite's results, or see what changed since | `powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save <name>` before a change, `-Compare <name>` after it (or `suite.ps1 -SaveBaseline <name>` and `-Baseline <name>`): which scenarios have other hashes, and what moved in them |
| Scenarios over a set of tuning values | `powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name legs -Scenarios "stand_npc,hit_head_30_stands" -Vary "muscles.legs.stiffness_nm_per_rad=400,800;muscles.legs.damping_s=0.03,0.06"` (every combination, or `-OneAtATime`; `-Metrics` chooses the columns; table in `Saved\Sweeps\<name>`) |
| Push matrix (160 pushes, outcome by the owner's M3 table) | `powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1` (`-PushSeconds 0.1` for pushes instead of blows, `-Tune` to try values, `-Impulses "30" -Enforce "30"` for one row, `-Rules m2` for the old gate); against a baseline: `Tools\baseline.ps1 -CompareMatrix <name>` |
| One generated scenario file | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -File <path.json>` |
| Interactive session (a window, for a person) | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1` (`-Name <scenario>` opens that scenario's scene; keys are on the screen) |
| Self-test of the session, no window | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Smoke` (`-Smoke -Shot` renders it and saves `final.png`) |
| Run-to-run deviation | `powershell -ExecutionPolicy Bypass -File Tools\repeatability.ps1 -Name drop_passive -IncludeRendered` |
| Compare two runs | `powershell -ExecutionPolicy Bypass -File Tools\compare-runs.ps1 -A <dir> -B <dir>` |
| Regenerate assets from specs | `powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1` (`-Force` also rebuilds up-to-date maps) |
| Bake a map's lighting | `powershell -ExecutionPolicy Bypass -File Tools\bake-lighting.ps1 [-Quality Preview] [-Force]` |
| Create, rig and assemble the MetaHuman | `powershell -ExecutionPolicy Bypass -File Tools\metahuman.ps1` (cloud steps need an Epic sign-in; see below) |

Typical times on the reference machine: incremental build 4 to 35 s, full build 80 s, one scenario 8 s
(the 10-minute stand scenario 60 s), unit tests 15 s, the whole suite 110 s (40 scenarios, eight at a time;
7 minutes one after another), the push matrix 3.5 minutes (one row of 40: 1 minute), a sweep of 7 runs 17 s, the session's self-test
10 s, the whole bootstrap about 5 minutes.

The loop: edit, `build.ps1`, `test.ps1`, `suite.ps1`, read the JSON, repeat. A task is done when its
scenarios pass, not when the code compiles. For anything visual, run a scenario with `-Shot` and look at the PNG.

Before a change to the body or its controller, take a baseline (`suite.ps1 -SaveBaseline <name>`); after
it, `suite.ps1 -Baseline <name>` names every scenario whose hashes moved. Scenarios the change should not
touch must not be in that list, and the ones that are get explained in the part's report.

## Layout

| Path | Holds |
| --- | --- |
| `Source/SandboxCore` | Shared services: log category `LogSandbox`, JSON helpers, `FSandboxParamStore` (tuning files, overrides, hot reload, the `Sandbox.Tune*` console commands). Depends on no other Sandbox module. |
| `Source/SandboxBody` | The simulated body: `ASandboxRagdollActor`, `USandboxBodyController` (muscles, balance assist and the states Standing, Flinch, Falling, Lying; the sense of balance, `GetBalance` and `MeasureBalance`: standing feet, support polygon, capture point; the decision on it, `IsStepNeeded`; `FollowSupport` moves the assist's anchors with the feet; `UpdateStrategies` turns the leg targets (ankle, hip, lean into a held push); `FeelFrame` splits what is felt into hit and pressure; `GetControlView` hands its targets to overlays), `FSandboxBodyTuning` (the body's tuning values by name, read from the store, poses among them), `FSandboxBodyProbe` (measurements: the support polygon, the capture point, energy, penetration into the world and into itself), `USandboxAnimInstance` (the target pose the muscles hold: a base pose blended from one to the next, turns added on top such as the flinch's; a native instance with its own proxy, no Animation Blueprint), `FSandboxPoseRig` (joint angles of a pose to bone rotations, on the axes of the physics asset's joints). |
| `Source/SandboxInteraction` | The player side. `USandboxEffectorComponent` (a hand: a sphere with mass) held by `FSandboxWorldSpring` (spring with a force cap to a moving world point); `ASandboxEffectorRig` (the hands plus an `ISandboxTargetSource`: desktop, scripted, later VR); `FSandboxDragHandle`; `ASandboxPawn` (free camera, tools, time control, input made in C++), `ASandboxPlayerController`, `ASandboxHUD`, `ASandboxGameMode`; `FSandboxInteractionTuning`. |
| `Source/SandboxScenario` | `UScenarioRunnerSubsystem` (also measures the hands from outside: penetration, tunnelling, what a strike moved; and for the body, every frame: balance, energy, rest after a fall, penetration, written to `series.csv`), `FScenarioSpec`, `FScenarioStage` (builds a scenario's scene), `USandboxPlaySubsystem` (the interactive session, `-Play=`; the only place where pawn and character meet in code), `USandboxOverlaySubsystem` (debug overlays, `Sandbox.Overlay`; here because it needs both the character and the hands), automation tests. |
| `Source/SandboxEditor` | Editor-only `GenerateAssets` commandlet; `FPhysicsAssetGenerator` builds physics assets from specs, joints from their anatomy (`ComputeJointFrames`). |
| `Specs/` | Text specs for generated assets (physics assets, room maps); format in `Specs/README.md`. |
| `Scenarios/` | Scenario JSON files; format in `Scenarios/README.md`. |
| `Tools/` | The CLI scripts above. `common.ps1` holds shared helpers (`Invoke-ScenarioBatch` runs scenario processes side by side for the suite, the push matrix and sweeps). |
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
- **A body under control never sleeps** (`USandboxBodyController::Start` sets its bodies to never sleep). It
  has to sense its balance and answer a touch at once; see the pitfall about the solver's sleep below.
- **Time control never changes the physics step.** Pause, single step and slow motion decide on which
  frames the world steps; a step is always 1/90 s in 2 substeps. No time dilation.
- **Physics timing is fixed.** Synchronous physics, 90 Hz frame, 2 substeps (180 Hz). Scenarios force a
  fixed engine timestep. Do not read wall-clock time in simulation code. The solver's determinism mode is
  on (`p.Chaos.Solver.Deterministic=1` in `Config/DefaultEngine.ini`); every scenario run checks it.
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
  `FPhysicsAssetGenerator::ComputeJointFrames` does it. The two swings are limited together as an ellipse,
  so a shoulder cannot have 180 degrees in two planes.
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
