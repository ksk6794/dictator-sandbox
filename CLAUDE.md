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
tunnelling. Next is M3, balance and recovery.
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
| Debug overlays in the pictures | add `-Overlay all` to a `-Shot` run or to `play.ps1` (or layers: `com,support,joints,assist,contacts,state,hands`); in a session the V key, in any console `Sandbox.Overlay all\|off\|toggle\|<layers>` |
| One scenario in the headset | add `-VR` (headset on, SteamVR running; takes the scenario's duration in real time) |
| One scenario with other body tuning | add `-Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"`, or `-TuningDir <dir>` for another set of tuning files |
| All scenarios | `powershell -ExecutionPolicy Bypass -File Tools\suite.ps1` (also holds the whole set to 2 cm of joint stretch and a 1.5 ms mean physics step, and prints the worst scenario of each) |
| Push matrix (128 pushes, the M2 fall rules) | `powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1` (`-PushSeconds 0.1` for pushes instead of blows, `-Tune` to try values) |
| One generated scenario file | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -File <path.json>` |
| Interactive session (a window, for a person) | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1` (`-Name <scenario>` opens that scenario's scene; keys are on the screen) |
| Self-test of the session, no window | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Smoke` (`-Smoke -Shot` renders it and saves `final.png`) |
| Run-to-run deviation | `powershell -ExecutionPolicy Bypass -File Tools\repeatability.ps1 -Name drop_passive -IncludeRendered` |
| Compare two runs | `powershell -ExecutionPolicy Bypass -File Tools\compare-runs.ps1 -A <dir> -B <dir>` |
| Regenerate assets from specs | `powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1` (`-Force` also rebuilds up-to-date maps) |
| Bake a map's lighting | `powershell -ExecutionPolicy Bypass -File Tools\bake-lighting.ps1 [-Quality Preview] [-Force]` |
| Create, rig and assemble the MetaHuman | `powershell -ExecutionPolicy Bypass -File Tools\metahuman.ps1` (cloud steps need an Epic sign-in; see below) |

Typical times on the reference machine: incremental build 4 to 15 s, full build 80 s, one scenario 8 s
(the 10-minute stand scenario 60 s), unit tests 15 s, the whole suite 6.3 minutes (34 scenarios, one after
another; NFR-6 asks for 5 with the build), the push matrix 3 minutes, the session's self-test 10 s, the
whole bootstrap about 10 minutes.

The loop: edit, `build.ps1`, `test.ps1`, `suite.ps1`, read the JSON, repeat. A task is done when its
scenarios pass, not when the code compiles. For anything visual, run a scenario with `-Shot` and look at the PNG.

## Layout

| Path | Holds |
| --- | --- |
| `Source/SandboxCore` | Shared services: log category `LogSandbox`, JSON helpers, `FSandboxParamStore` (tuning files, overrides, hot reload, the `Sandbox.Tune*` console commands). Depends on no other Sandbox module. |
| `Source/SandboxBody` | The simulated body: `ASandboxRagdollActor`, `USandboxBodyController` (muscles, balance assist and the states Standing, Flinch, Falling, Lying; `GetControlView` hands its targets to overlays), `FSandboxBodyTuning` (the body's tuning values by name, read from the store), `FSandboxBodyProbe` (measurements, the support polygon). |
| `Source/SandboxInteraction` | The player side. `USandboxEffectorComponent` (a hand: a sphere with mass) held by `FSandboxWorldSpring` (spring with a force cap to a moving world point); `ASandboxEffectorRig` (the hands plus an `ISandboxTargetSource`: desktop, scripted, later VR); `FSandboxDragHandle`; `ASandboxPawn` (free camera, tools, time control, input made in C++), `ASandboxPlayerController`, `ASandboxHUD`, `ASandboxGameMode`; `FSandboxInteractionTuning`. |
| `Source/SandboxScenario` | `UScenarioRunnerSubsystem` (also measures the hands from outside: penetration, tunnelling, what a strike moved), `FScenarioSpec`, `FScenarioStage` (builds a scenario's scene), `USandboxPlaySubsystem` (the interactive session, `-Play=`; the only place where pawn and character meet in code), `USandboxOverlaySubsystem` (debug overlays, `Sandbox.Overlay`; here because it needs both the character and the hands), automation tests. |
| `Source/SandboxEditor` | Editor-only `GenerateAssets` commandlet; `FPhysicsAssetGenerator` builds physics assets from specs, joints from their anatomy (`ComputeJointFrames`). |
| `Specs/` | Text specs for generated assets (physics assets, room maps); format in `Specs/README.md`. |
| `Scenarios/` | Scenario JSON files; format in `Scenarios/README.md`. |
| `Tools/` | The CLI scripts above. `common.ps1` holds shared helpers. |
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
- **Time control never changes the physics step.** Pause, single step and slow motion decide on which
  frames the world steps; a step is always 1/90 s in 2 substeps. No time dilation.
- **Physics timing is fixed.** Synchronous physics, 90 Hz frame, 2 substeps (180 Hz). Scenarios force a
  fixed engine timestep. Do not read wall-clock time in simulation code.
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
- **A bash heredoc breaks on an apostrophe in its body when run through the agent's shell tool.** Write the
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
- **A sustained push by a hand makes the character flinch again every time a flinch ends** (0.35 s), until
  it falls: each contact frame adds to the felt impulse, as a long `impulse` push would. Known; M3 decides
  what a push should do.
- **Add a tuning name and its value in the same build.** A build that does not know a name refuses the
  file, so a new tuning file dropped in while an older build is running a suite fails every active scenario
  after it ("is not a tuning value").
- **Scripts target Windows PowerShell 5.1.** No `&&`, ternaries or null-coalescing.
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
