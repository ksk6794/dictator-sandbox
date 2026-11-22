# DictatorSandbox

Unreal Engine 5.8 C++ prototype: one photoreal room, one active-ragdoll NPC the player can hit, push, grab
and drag, on desktop and PCVR. Requirements, architecture and milestones are in `docs/PRD.md`.
Current milestone: **M1 done** (MetaHuman by script, physics asset and room from JSON specs, baked lighting,
launch in the headset, 7.1 ms of GPU time per stereo frame). **M2, active ragdoll core, is in progress.**
Steps 1 to 3 of 5 are done: the NPC stands under full simulation for 10 minutes (muscles and balance assist
through Physics Control); every tuning value lives in `Config/Tuning`, is hot-reloaded and can be overridden
from the console and the command line; the body has the states Standing, Flinch, Falling and Lying, joint
limits from anatomy that mirror left to right, and passes the push matrix. Still to do: desktop pawn with
effectors, overlays and strike scenarios.
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
| One scenario with a screenshot | add `-Shot` (renders offscreen, saves `final.png` next to the metrics) |
| One scenario in the headset | add `-VR` (headset on, SteamVR running; takes the scenario's duration in real time) |
| One scenario with other body tuning | add `-Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"`, or `-TuningDir <dir>` for another set of tuning files |
| All scenarios | `powershell -ExecutionPolicy Bypass -File Tools\suite.ps1` |
| Push matrix (128 pushes, the M2 fall rules) | `powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1` (`-PushSeconds 0.1` for pushes instead of blows, `-Tune` to try values) |
| One generated scenario file | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -File <path.json>` |
| Run-to-run deviation | `powershell -ExecutionPolicy Bypass -File Tools\repeatability.ps1 -Name drop_passive -IncludeRendered` |
| Compare two runs | `powershell -ExecutionPolicy Bypass -File Tools\compare-runs.ps1 -A <dir> -B <dir>` |
| Regenerate assets from specs | `powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1` (`-Force` also rebuilds up-to-date maps) |
| Bake a map's lighting | `powershell -ExecutionPolicy Bypass -File Tools\bake-lighting.ps1 [-Quality Preview] [-Force]` |
| Create, rig and assemble the MetaHuman | `powershell -ExecutionPolicy Bypass -File Tools\metahuman.ps1` (cloud steps need an Epic sign-in; see below) |

Typical times on the reference machine: incremental build 4 to 15 s, full build 80 s, one scenario 8 s
(the 10-minute stand scenario 60 s), unit tests 15 s, the whole suite 4 minutes, the push matrix 3 minutes.

The loop: edit, `build.ps1`, `test.ps1`, `suite.ps1`, read the JSON, repeat. A task is done when its
scenarios pass, not when the code compiles. For anything visual, run a scenario with `-Shot` and look at the PNG.

## Layout

| Path | Holds |
| --- | --- |
| `Source/SandboxCore` | Shared services: log category `LogSandbox`, JSON helpers, `FSandboxParamStore` (tuning files, overrides, hot reload, the `Sandbox.Tune*` console commands). Depends on no other Sandbox module. |
| `Source/SandboxBody` | The simulated body: `ASandboxRagdollActor`, `USandboxBodyController` (muscles, balance assist and the states Standing, Flinch, Falling, Lying), `FSandboxBodyTuning` (the body's tuning values by name, read from the store), `FSandboxBodyProbe` (measurements). |
| `Source/SandboxInteraction` | The player side: `ASandboxGameMode` now; effectors and pawn from M2. |
| `Source/SandboxScenario` | `UScenarioRunnerSubsystem`, `FScenarioSpec`, automation tests. |
| `Source/SandboxEditor` | Editor-only `GenerateAssets` commandlet; `FPhysicsAssetGenerator` builds physics assets from specs, joints from their anatomy (`ComputeJointFrames`). |
| `Specs/` | Text specs for generated assets (physics assets, room maps); format in `Specs/README.md`. |
| `Scenarios/` | Scenario JSON files; format in `Scenarios/README.md`. |
| `Tools/` | The CLI scripts above. `common.ps1` holds shared helpers. |
| `Config/` | Engine and game settings. Physics timing lives in `DefaultEngine.ini`. |
| `Config/Tuning/` | Every tunable number of the body, as JSON; names, units and ways to change them in its `README.md`. |
| `docs/` | `PRD.md` (plan), `testing.md` (repeatability policy and measurements). |

## Rules

- **Module dependencies.** `SandboxBody` and `SandboxInteraction` must not depend on each other; they meet
  only as rigid bodies in the physics world. Both may depend on `SandboxCore`. `SandboxScenario` may depend
  on all of them; nothing depends on it.
- **No Blueprint logic, no Animation Blueprints.** Gameplay, input and animation logic are C++.
- **Text is the source of truth.** Never hand-edit `.uasset` or `.umap` files. Generated assets come from
  `Specs/` through the `GenerateAssets` commandlet; imported art is imported by script.
- **Tuning values live in `Config/Tuning`, not in code.** A new tunable number gets a name in its module's
  list (`VisitValues` in `SandboxBodyTuning.cpp` for the body) and a value in the JSON file; no default in
  C++. A value missing from the files, or a name the code does not know, is an error by design.
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
