# Physics Sandbox PRD & Technical Plan

Nov 22, 2026 · @Sviat K

The prototype is feasible on Unreal Engine 5.8 with stock Chaos physics, provided three expectations are set now.

- **"Physically accurate" means physically plausible.** The body is a simulated ragdoll with real masses and joint limits that tracks animation through capped muscle drives. Staying upright needs a small non-physical assist, as it does in every shipped game of this kind. Biomechanical balance from first principles is a research problem and is out of scope.
- **Photoreal at 90 FPS in VR means baked lighting.** Lumen and Nanite in stereo remain experimental and unsupported by Epic. A static room loses nothing by baking, and the frame goes to the character.
- **AI-first means roughly nine-tenths CLI.** Building, asset generation, tuning and testing run headless. Account sign-ins, asset claiming, and judging feel in a headset stay with a person.

Three unknowns decide the schedule and are measured first: the MetaHuman's stereo rendering cost, Chaos joint stiffness under grab loads, and whether the physics asset and room can be generated from text.

## Key decisions

| Area | Choice | Why | Rejected |
| --- | --- | --- | --- |
| Engine | UE 5.8.4, Launcher build | Latest stable; Physics Control is production-ready in it; described as the last planned UE5 major | 5.9 Preview (lightly tested, final in 2027); source build (160 GB or more, not needed) |
| Physics | Chaos world simulation with `UPhysicsControlComponent` | Two-way collision with room, props and hands; Chaos Visual Debugger; Epic's maintained path | Rigid Body anim node (one-way world collision); Jolt, MuJoCo and Box3D plugins (a second physics world with no skeletal or VR story) |
| Body control | Joint drives, weak world-space assist, capture-point state machine | What shipped games converge on; buildable and tunable by a small team | Learned controllers (no training path inside UE, days of training); full procedural walking balance |
| Timing | Synchronous physics, fixed 90 Hz with 2 substeps | No interpolation latency between hands and picture | Async physics (kept as fallback) |
| Rendering | Deferred, baked lighting, TAA at 85% headset render scale; DLSS to be evaluated in M5 | Best fidelity per millisecond for a static room | Lumen in VR (desktop tier only); forward renderer (kept as fallback) |
| Character | MetaHuman, Optimized assembly, hair cards | Highest fidelity; scriptable assembly; free at this scale | Character Creator 5 (kept as fallback) |
| XR | Built-in OpenXR plugin, no vendor SDK | One code path for every PCVR headset | Meta XR plugin (not needed on PC); SteamVR plugin (removed from the engine) |
| Player contact | Force-capped dynamic effectors | Correct momentum transfer; same model in desktop, VR and tests | Kinematic hands (infinite mass) |
| Authoring | Text specs plus a generator commandlet; native animation instance | Agents edit text and regenerate; nothing depends on editor clicks | Blueprint and Animation Blueprint logic; third-party MCP servers |
| Testing | JSON scenarios, headless, fixed step, JSON metrics | Fast, repeatable, readable by agents | Functional tests placed in binary maps; Gauntlet (built for device farms) |

## Product requirements

The product is one room, one NPC and one player; it succeeds when the NPC's body reacts to contact the way a person's would, in both desktop and VR, and when an AI agent can prove that from the command line.

### Goals

- **Believable body.** Every reaction comes from simulation: contact forces, joint limits, muscle-like drives and balance. Canned hit animations are not the mechanism.
- **One simulation, two front ends.** Desktop and VR drive the same physical hands and the same NPC code. Only the source of the hand targets differs.
- **Agent-operable.** Build, asset generation, tuning, testing and packaging all run from CLI with machine-readable results.
- **Measurable.** Physical behaviour is covered by reproducible scenarios with numeric pass criteria, so tuning is a search rather than a judgement call.
- **Photoreal at 90 FPS.** High-fidelity character, materials and lighting inside an 11.1 ms stereo frame.

### Non-goals for the prototype

- Combat design, health, scoring, progression or NPC behaviour beyond standing, reacting and recovering.
- NPC locomotion beyond balance-recovery steps.
- Wounds, blood, dismemberment or bone breakage.
- Multiplayer, save games, menus beyond a debug panel.
- Standalone headset builds (Quest native) or consoles.

### Interaction verbs

| Verb | Desktop input | VR input | Expected physical outcome |
| --- | --- | --- | --- |
| Slap | Aim, tap light-strike key | Open hand swung into the body | Head or limb deflects, torso sways, NPC stays standing |
| Punch | Aim, click; hold to wind up | Fist swung into the body | Local recoil propagates to torso; stagger or step above a threshold |
| Kick | Aim, kick key | Scripted leg strike aimed by gaze or controller | Large impulse low on the body; leg buckles or NPC falls |
| Push | Hold push key with hand on body | Hands pressed against the body | Sustained force; lean, recovery step, then fall |
| Grab | Hold grab on a body part | Grip on a body part | Constraint at contact point; part follows hand up to a force cap |
| Drag | Move while grabbing | Move or pull while gripping | Body follows; standing NPC stumbles, fallen NPC slides with friction |
| Knock down | Any of the above at high energy | Same | Loss of balance, protective fall, rest, get-up |

### Functional requirements

| ID | Requirement | Priority |
| --- | --- | --- |
| FR-1 | NPC stands under full simulation, tracking an idle animation, with no drift or jitter for 10 minutes | Must |
| FR-2 | Contact anywhere on the body produces a reaction scaled by impulse that propagates through the joints | Must |
| FR-3 | Balance: small pushes absorbed by sway, medium pushes by a recovery step, large pushes cause a fall | Must |
| FR-4 | Falls show protective reflexes (arms brace, head tucks), no self-penetration, and come to rest | Must |
| FR-5 | NPC gets up from prone, supine or side rest poses and returns to idle | Must |
| FR-6 | Any body part can be grabbed with one or two hands, pulled, lifted within a force cap, and dragged | Must |
| FR-7 | NPC collides with walls, floor, furniture and loose props | Must |
| FR-8 | Desktop and VR share one pawn, one hand model and one NPC; the build runs flat with no headset attached | Must |
| FR-9 | Desktop test tools: free camera, pause, single-step, slow motion, NPC reset, calibrated impulse tool, drag handle | Must |
| FR-10 | VR: OpenXR tracked controllers drive physical hands; smooth locomotion and snap turn | Must |
| FR-11 | All physical parameters live in a text file, hot-reload at runtime and can be overridden from console or CLI | Must |
| FR-12 | Text-defined scenarios run headless from CLI and write JSON metrics plus an exit code | Must |
| FR-13 | Debug overlays: centre of mass, support polygon, joint drive targets, contacts, controller state | Must |
| FR-14 | Physics recording and screenshot or video capture can be triggered from CLI | Must |
| FR-15 | Impact audio and VR haptics scaled by impulse | Should |
| FR-16 | Soft-tissue secondary motion on face and torso under impact | Should |
| FR-17 | Skin flush at impact sites driven by a material mask | Could |

### Non-functional requirements

| ID | Requirement | Target |
| --- | --- | --- |
| NFR-1 | PCVR frame rate on the reference machine (RTX 5080, Ryzen 7 9800X3D, 32 GB) | 90 FPS, 99th-percentile frame time at or under 11.1 ms, no reprojection during the standard interaction scenario |
| NFR-2 | Desktop frame rate at 2560×1440 | 120 FPS or better |
| NFR-3 | Reproducibility | Same build, machine and scenario give metrics within stated tolerances on every run |
| NFR-4 | Blueprint dependency | No gameplay logic in Blueprint; binary assets are imported or generated from text |
| NFR-5 | Setup | One script takes a clean clone to a running build |
| NFR-6 | Agent loop time | Incremental compile plus headless scenario suite under 5 minutes |

## Hardest problems

Balance that looks alive, and joints that stay rigid when the player hauls on them, are the two problems most likely to decide whether the prototype convinces. The list is ranked by risk to that outcome.

| # | Problem | Why it is hard | Approach | Fallback |
| --- | --- | --- | --- | --- |
| 1 | Stagger and balance without a marionette look | Enough assist to stand reads as strings; too little and the NPC topples at a touch | Three balance levels; assist confined to pelvis, chest and head; assist share tracked as a metric and driven down | Animated stumble steps with higher assist; accept earlier falls |
| 2 | Joint stability under grab and drag loads | Chaos joints soften and stretch under load at low step rates; a capped-force hand pulling 80 kg is the worst case | 180 Hz simulation, mass ratios under 10, capped drives, non-linear joint solver, stretch metric as a gate | Async physics at a higher rate; release the grab when stretch crosses a limit |
| 3 | Tuning a large parameter space | Strength and damping interact across joints and states; the Half Sword developers call this their hardest problem | One parameter file, per-state groups, automated sweeps against the scenario suite | Fewer joint groups; fixed ratios between stiffness and damping |
| 4 | Get-up from arbitrary poses | Limbs tangled, body against a wall or over furniture; a blend from any pose can pop or clip | Prone and supine animations selected by pelvis orientation; roll to a clear pose first; clearance check; ramped strength | Longer blend with stronger assist; nudge the body into open floor before rising |
| 5 | Photoreal character in stereo at 90 FPS | No published stereo cost for a MetaHuman; strand hair and the face rig are expensive | Measure at Milestone 1 before the room is built; hair cards, lower face LOD, DLSS | Forward renderer with MSAA; Character Creator 5 character |
| 6 | Binary assets from text | Physics assets, maps and animation logic are normally made in the editor | Generator commandlet in an editor module; native animation instance; each route proven at Milestone 0 or 1 | Epic's MCP physics-asset toolset; a few minutes of human editor work per asset |
| 7 | Fast strikes | A fist moving 8 m/s travels 4 cm per 180 Hz step and can tunnel or spike impulses | Continuous collision on hands, feet and head; swept query; impulse clamp | Thicker collision shapes on limbs |
| 8 | Repeatable tests on a solver without a determinism guarantee | Parallel solving can reorder work between runs | Fixed timestep, fixed spawn order, measured deviation at Milestone 0 | Tolerance bands and repeated runs |
| 9 | Stereo bugs in the 5.8 deferred path | Open reports of right-eye errors with instanced stereo | Avoid the affected features; validate on a headset at Milestone 1 | Forward renderer; UE 5.7.4 |

## Architecture

The player side and the NPC side never call each other; they meet only as rigid bodies in the Chaos world. That keeps each module small and lets a test replace the player entirely.

```mermaid
flowchart TD
    D["<b>Desktop input</b><br/>mouse and keyboard verbs"]
    X["<b>OpenXR input</b><br/>tracked controller grip poses"]
    S["<b>Scenario runner</b><br/>scripted paths and impulses"]
    E["<b>Effectors: physical hands and kicking foot</b><br/>dynamic bodies pulled to their targets by force-capped drives;<br/>grab creates a constraint"]
    W["<b>Chaos physics world</b><br/>one simulation at 180 Hz for the room, props,<br/>effectors and all 16 NPC bodies"]
    D -->|target poses| E
    X -->|target poses| E
    S -->|target poses| E
    E -->|contact and grab forces| W
    W <-->|forces and body states| NPC
    subgraph NPC["NPC body"]
        P["<b>Target pose</b><br/>native C++ animation<br/>plus flinch offsets"]
        M["<b>Muscles</b><br/>Physics Control joint drives,<br/>force-capped"]
        A["<b>Balance assist</b><br/>world-space controls:<br/>pelvis, chest, head"]
        C["<b>Balance controller</b><br/>capture point and<br/>state machine"]
    end
    subgraph SHARED["Shared by every module"]
        PA["<b>Parameters</b><br/>JSON file, console overrides"]
        SC["<b>Scenarios and metrics</b><br/>headless runs, JSON results"]
        G["<b>Asset generator</b><br/>commandlet: text to assets"]
    end
    NPC --- SHARED
```

*Architecture: desktop, VR and tests drive the same effectors in one physics world.*

Read top down: three interchangeable sources produce target poses, effectors turn them into forces, and the NPC body answers through the same solver.

### Modules

| Module | Kind | Responsibility |
| --- | --- | --- |
| `SandboxCore` | Runtime | Parameter store with JSON loading, hot reload, console and command-line overrides; metric types; logging |
| `SandboxBody` | Runtime | NPC actor, Physics Control setup, muscles, balance assist, balance controller, state machine, reactions, native animation instance |
| `SandboxInteraction` | Runtime | Effector component, grab constraints, strike verbs, target-source interface; player pawn with desktop and OpenXR sources, cameras, locomotion |
| `SandboxScenario` | Runtime | Scenario loader and runner, metrics recorder, the interactive session that puts character and player into one world, session recorder, debug overlays and console commands |
| `SandboxEditor` | Editor only | `GenerateAssets` commandlet: physics asset, room map, art import |

### Rules that keep it simple

- `SandboxBody` and `SandboxInteraction` do not depend on each other. The body learns about hits from its own contact events; a grab is a constraint on whatever body the hand touched. The two test tools that need the character itself, the calibrated impulse and the reset, are requests the pawn raises and `SandboxScenario` carries out.
- Both depend only on `SandboxCore`. `SandboxScenario` depends on all three and nothing depends on it.
- One interface carries the desktop and VR split: a target source returns a pose and a grip value per effector each tick.
- No Blueprint classes. Actors, components, input and the game mode are C++; the map contains only static geometry and lighting.
- Engine plugins used: Physics Control, OpenXR, DLSS. No third-party gameplay plugins.

### Repository layout

```
Source/              C++ modules
Config/Tuning/       parameter JSON
Specs/               physics asset and room specs
Scenarios/           scenarios and metric baselines
Tools/               CLI scripts
Content/Imported/    art, tracked in Git LFS
Content/Generated/   generator output
```

## Active ragdoll design

The NPC is a fully simulated skeletal mesh in the Chaos world at all times. Joint drives make it track an animation pose, a weak world-space assist keeps it upright, and a small state machine changes drive strengths as balance is lost and regained.

### Control stack

1. **Target pose.** Idle, stumble, protective and get-up animations evaluated in native C++ produce the pose the body tries to reach.
2. **Muscles.** Parent-space spring-damper controls on every joint, created through `UPhysicsControlComponent`, pull each body toward the target pose. Each has a force cap.
3. **Balance assist.** World-space controls on pelvis, chest and head, plus foot-planting controls. This is the one non-physical force in the system and the main dial between "marionette" and "falls over".
4. **Balance controller.** Custom C++ computes centre of mass, its velocity, the support polygon from foot contacts and the capture point. It chooses sway, a recovery step or a fall.
5. **Reactions.** Impacts weaken muscles in the hit region for a fraction of a second and add a flinch offset to the target pose. Falling switches to a protective pose.

Physics Control is [marked production-ready in UE 5.8](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes?application_version=5.8) and its API is fully callable from C++. It only tracks poses; layers 4 and 5 and the state machine are ours to write. Epic's Game Animation Sample for 5.8 is reported to include a powered-ragdoll character built on the same component. It is the first reference to study, and M0 confirms what it contains.

All bodies stay dynamic in every state and the NPC has no character capsule. Epic staff report joint stretching when kinematic and simulated bodies are mixed on one mesh under async physics, which is our fallback timing mode. A single regime also keeps the states continuous.

### States

```mermaid
stateDiagram-v2
    GetUp: Get-up
    [*] --> Standing
    Standing --> Flinch: impulse above threshold
    Flinch --> Standing: timer ends
    Standing --> Stagger: capture point leaves support
    Stagger --> Standing: balance recovered
    Stagger --> Falling: pelvis error or step out of reach
    Falling --> Lying: at rest for 0.5 s
    Lying --> GetUp: after a delay, not held
    GetUp --> Standing: animation completes
    GetUp --> Falling: large disturbance during get-up
```

*NPC state machine: losing balance walks the NPC through six states and back to Standing.*

| State | Balance assist | Muscle strength | Target pose | Leaves when |
| --- | --- | --- | --- | --- |
| Standing | Normal; feet planted | High | Idle | Impulse above flinch threshold; capture point leaves the support polygon |
| Flinch | Normal | Hit region weakened for 0.2 to 0.5 s | Idle plus flinch offset | Timer ends |
| Stagger | Reduced | High in legs, normal elsewhere | Stumble step toward the capture point | Centre of mass back over support for 0.3 s; or pelvis error or capture point beyond step reach |
| Falling | Zero | Low stiffness, high damping | Protective: arms toward the fall, chin tucked | Body speed below threshold for 0.5 s |
| Lying | Zero | Very low | None | At rest, not held, after a delay |
| Get-up | Ramps from zero to normal | Ramps to high | Get-up animation matched to prone or supine | Animation completes; or a large disturbance returns it to Falling |

Being grabbed is not a state. A grab constraint is an external force like any other, and the balance controller responds to its effect.

As built in M2: Standing, Flinch, Falling and Lying exist; Stagger and Get-up are M3. Without the capture point, Standing and Flinch end in Falling when the pelvis leaves its place by more than a threshold (distance, height or tilt), and Lying is where the body stays. The values of every state and the thresholds between them are in `Config/Tuning/body.json`.

Balance is built in three levels so each milestone has a working character. Level 0 is the assist alone. Level 1 adds capture-point fall detection and animated stumble steps. Level 2, a stretch goal, places the stepping foot procedurally.

### Physics asset

Sixteen bodies and fifteen joints: pelvis, two spine segments, head, and per side upper arm, forearm, hand, thigh, shank and foot. Capsules everywhere, boxes for feet and chest, no fingers, no convex hulls. Collision is off between adjacent bodies and on between arms and torso and between the legs.

As built in M2: shapes come from the stock MetaHuman asset except the feet, which are boxes written out in the spec with a flat sole on the floor (the chest is still capsules). Joints are the generator's own, built from an anatomical description in the spec (limb direction, bending axis, a range per axis), with the right side as the mirror image of the left; the stock asset's joint positions, axes and motors all break a fully simulated body. The format is in `Specs/README.md`.

Masses follow de Leva's 1996 segment fractions, here for an 80 kg male. Every parent-to-child mass ratio stays under 10, which the joint solver needs.

| Body | Share of body mass (%) | Mass (kg) |
| --- | --- | --- |
| Pelvis | 11.17 | 8.9 |
| Lower spine | 16.33 | 13.1 |
| Chest | 15.96 | 12.8 |
| Head and neck | 6.94 | 5.6 |
| Upper arm, each | 2.71 | 2.2 |
| Forearm, each | 1.62 | 1.3 |
| Hand, each | 0.61 | 0.5 |
| Thigh, each | 14.16 | 11.3 |
| Shank, each | 4.33 | 3.5 |
| Foot, each | 1.37 | 1.1 |

Leg values were checked against [a paper reproducing de Leva's table](https://pmc.ncbi.nlm.nih.gov/articles/PMC9602786/table/healthcare-10-02054-t005). Trunk, head and arm values were not confirmed against the original and need a check before they are locked.

Joint limits start about 10% inside clinical range of motion ([WikEM table](<https://wikem.org/wiki/Joint_ROM_(Table)>)). Knees and elbows are hinges. As built in M2 the table is in force, except the shoulder, which the engine's single limit cone can only approximate, and the knee, which has 2 degrees of slack past straight; the choices made where the table leaves room are listed in `Specs/README.md`.

| Joint | Starting limits (degrees) |
| --- | --- |
| Knee | Flexion 0 to 150 |
| Elbow | Flexion 0 to 140 |
| Hip | Flexion 100, extension 30, abduction 40, adduction 20, rotation 40 in and 50 out |
| Shoulder | Flexion 180, extension 50, abduction 180, rotation 90 each way |
| Ankle | Plantar flexion 20, dorsiflexion 30 |
| Neck | Flexion 60, extension 75, lateral 45, rotation 80 |
| Spine, total | Flexion 45, extension 25, lateral 25, rotation 30 |
| Wrist | Flexion 60, extension 60 |

### Solver and timing

- **Synchronous physics, fixed 90 Hz step, two substeps.** The simulation runs at 180 Hz with no interpolation latency between hands and render. Async physics adds about two fixed steps of delay and leaves game-thread control targets stale.
- **Fixed engine timestep in tests.** Headless runs force a constant frame delta so results do not depend on wall-clock time.
- **Joint solver.** The default linear solver, with 64 position iterations on the NPC's own bodies (decided in M2: at the engine default of 8 a standing body does not converge and its feet creep; the cost is 0.40 ms per frame against 0.26). Switch the NPC's joints to the non-linear solver (a per-joint setting of the physics asset) if limbs stretch under load; Epic documents it as more stable and more expensive.
- **Stability rules.** Soft limits, capped drive forces, projection off for bodies in contact, inertia conditioning on, continuous collision only on hands, feet and head.
- **Fallback.** If frame drops destabilise the body, move to async physics at 180 Hz and run control logic in simulation callbacks.

### Tunable parameters

Every number above is a parameter in one text file, grouped as follows.

| Group | Parameters |
| --- | --- |
| Muscles | Stiffness in N·m/rad, damping time and torque cap per joint group (spine, neck, arms, legs), per state |
| Balance assist | Horizontal and vertical stiffness in N/m, angular stiffness in N·m/rad and damping time for pelvis, chest, head and feet, per state |
| Thresholds | Flinch impulse, stagger and fall conditions, rest speed and time |
| Reactions | Weakening factor, spread to neighbouring chains, recovery time |
| Get-up | Delay, ramp duration, pose-match tolerance |
| Body | Total mass, segment fractions, joint limits, friction, restitution |
| Solver | Substeps, iteration counts, joint solver type |
| Effectors | Mass, spring, damper, force cap, strike speeds, grab break force |

As built in M2: the files are the JSON files of `Config/Tuning`: `body.json` (the muscles, the balance assist, the solver counts, thresholds, reactions and the names of the bones the assist holds), `effectors.json` (the player's hands) and `tools.json` (the desktop tools). A value's name is its path in the file, `muscles.legs.stiffness_nm_per_rad`, and the same name is used in overrides and in result files. Per-state values go under a `states` section (`states.falling.muscles.legs.stiffness_nm_per_rad`) and replace the base value while the body is in that state. The code holds no defaults: a missing value or an unknown name is an error. `Config/Tuning/README.md` is the reference.

## Interaction: one effector model for desktop and VR

The player touches the NPC only through effectors: dynamic rigid bodies pulled toward a target pose by a force-capped spring. Desktop, VR and automated tests differ only in where the target pose comes from.

### The effector

- Each hand, and one foot for kicks, is a dynamic body with real mass. A kinematic hand behaves as infinite mass and launches whatever it touches, so it is ruled out.
- A spring-damper drive pulls the body to its target. The force cap is the player's strength; spring and damper set responsiveness.
- The visible hand follows the physics body, not the controller. The gap between them is what conveys weight and resistance.
- Grab creates a constraint between the effector and the touched NPC body at the contact point. Two hands create two constraints. The force cap is what makes an 80 kg body heavy to lift and drag.
- Fast strikes use continuous collision detection plus a swept query, so a fist cannot tunnel through a forearm between steps.

### Target sources

| Source | Mode | How the target pose is produced |
| --- | --- | --- |
| OpenXR grip poses | VR | Tracked controller pose each frame |
| Virtual hands | Desktop | Hands rest in front of the camera; the aim ray picks a reach point; each verb plays a parametric trajectory toward it (wind-up, peak speed, follow-through) |
| Scenario script | Headless tests | Trajectories read from the scenario file, or calibrated impulses applied directly to a named body |

VR has no foot tracking, so the kick is a scripted leg effector in both modes, aimed by gaze or controller direction. Strike speeds are parameters; starting values are 6 m/s for a slap, 8 m/s for a punch and 10 m/s for a kick, to be tuned.

As built in M2: two hand effectors, each a 1.5 kg sphere on a spring with a 300 N cap, and two of the three sources. The desktop source holds the hands below the line of sight and sends one straight ahead while its mouse button is held; the scripted source plays the keys of a scenario's `effector` events, which is how the strike scenarios deliver a slap at 6 m/s and a punch at 8 m/s. Continuous collision detection is on for the hands and they do not tunnel up to 28 m/s; the swept query is the test's own measurement of that, not part of the hand. Strike verbs, the kick, grabbing and the OpenXR source are M4. The values are in `Config/Tuning/effectors.json`.

### Desktop test conveniences

- **Impulse tool.** Click a body to apply a chosen impulse in N·s along the aim ray. It bypasses the hands, so results are calibrated and repeatable.
- **Drag handle.** A spring from the cursor to the clicked body, for pulling limbs and posing the NPC.
- **Time control.** Pause, single-step, slow motion.
- **Reset.** Return the NPC to standing at the spawn point, or to any saved pose.
- **Record and replay.** A live session's effector targets can be saved as a scenario file. A bug found by hand becomes a regression test.
- **Cameras.** First-person, free-fly and orbit around the NPC.

As built in M2: the impulse tool, the drag handle, pause, single step, slow motion, reset to the spawn pose and a free-fly camera that can leave the hands behind, in `Tools\play.ps1`. Slow motion and single step leave frames out and never shorten the physics step. Record and replay, saved poses and the orbit camera are not built yet.

### VR specifics

- Haptic amplitude follows contact impulse. There is no hit-stop; freezing the view in VR is uncomfortable.
- The player body is a capsule with smooth stick locomotion and snap turn. It pushes the NPC with a capped force instead of passing through.
- Hand tracking without controllers is out of scope for the prototype.

## Rendering and performance

Bake the room's lighting and spend the frame on the character: deferred renderer, baked global illumination, one shadowed movable light on the NPC, and DLSS. Lumen stays a desktop-only quality tier.

### Renderer options

| Option | Status in VR | Role |
| --- | --- | --- |
| Deferred + baked GI + reflection captures + DLSS/DLAA | Supported path; zero runtime GI cost; full skin, eye and decal shading | Baseline for VR and desktop |
| Forward + MSAA + baked GI | VR Template default and the cheapest; loses screen-space reflections, contact shadows and Nanite | Tested fallback if deferred stereo bugs block us |
| Deferred + Lumen + Nanite | Epic documents it as [experimental and not officially supported in XR](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-and-lumen-for-xr-in-unreal-engine); community reports range from 40 to 90 FPS on older engines and cards, none on this hardware | Desktop-only scalability tier |

A static room gains nothing from dynamic GI that a bake does not give at higher quality. The only dynamic object that matters is the NPC, lit by a Volumetric Lightmap plus one movable shadow-casting key light.

### Configuration

- DX12, deferred shading, instanced stereo, built-in OpenXR plugin.
- NVIDIA DLSS plugin 8.7.2, which [supports UE 5.5 to 5.8](https://forums.developer.nvidia.com/t/nvidia-dlss-unreal-engine-plugin-archive/377564). DLAA where headroom allows, DLSS Quality otherwise.
- Lighting baked with CPU Lightmass by default. [GPU Lightmass is still Beta](https://dev.epicgames.com/documentation/unreal-engine/gpu-lightmass-global-illumination-in-unreal-engine) with crash reports on 5.6 to 5.8, so it is used only if it proves stable.
- Kept off in VR because of open stereo bugs or unsupported combinations in 5.8.x: MegaLights, volumetric fog, TSR below 100% screen percentage, DLSS Frame Generation, and VRS foveation ([not compatible with DLSS](https://dev.epicgames.com/documentation/en-us/unreal-engine/xr-performance-features-in-unreal-engine)).
- Runtime reprojection (ASW, SteamVR motion smoothing) is a safety net, not a design target. It smears fast hand motion, which is the core of this product.

### Character

- **Source: MetaHuman, UE Optimized assembly.** Free under the UE EULA below $1M revenue, and Creator has a [Python API for create, assemble and export](https://dev.epicgames.com/documentation/metahuman/metahuman-creator-python-scripting-in-unreal-engine). Auto-rig and texture download need one interactive Epic sign-in.
- **VR settings:** hair cards instead of strands, face LOD 1 or 2. Strand hair in stereo is assumed unaffordable until measured.
- **Physics asset:** built on primary bones only. Twist and corrective joints stay animation-driven; adding or renaming joints breaks Creator compatibility.
- **Alternative: Character Creator 5.** Freer skeleton and mesh editing and a simpler custom physics asset, at about $299 and with a weaker face rig. It is the fallback if the MetaHuman skeleton fights the physics asset.
- **Soft tissue:** spring-driven jiggle bones on cheeks and torso, impact-triggered morph targets, and a material mask for skin flush. Each is sub-millisecond. Chaos Flesh and ML Deformer are out of scope for the prototype.

### Room

- One interior of about 5 m by 4 m, static shell and furniture, plus 5 to 10 simulated props (chair, table, bottles).
- [Megascans is free on Fab again](https://80.lv/articles/entire-quixel-megascans-library-now-available-for-free/) as of November 2026. Fab has no public API, so a human claims the chosen assets once.
- Scriptable CC0 sources fill the rest: the [Poly Haven API](https://github.com/Poly-Haven/Public-API) and [ambientCG API](https://docs.ambientcg.com/api/v3/).

### Frame budget

These are planning estimates, not measurements. Milestone 1 replaces them with numbers from the reference machine.

| Thread | Budget | Main items |
| --- | --- | --- |
| Game | 4 ms | Physics 1 to 1.5 ms; animation and face rig 1 to 2 ms |
| Render | 4 ms | Draw submission for two eyes |
| GPU | 8.5 to 9 ms | Base pass and shadows about 3; character about 2; lighting and reflections about 1; post and DLSS about 2 |

The remaining 2 ms of GPU time is headroom for the compositor and, on Quest over Link, video encoding. Quest 3 stereo is about 9 megapixels at 90 Hz, close to the pixel rate of 4K at 100 FPS.

## AI-agent development workflow

Agents work through a small set of CLI scripts that build, generate assets, run headless scenarios and return JSON. Text files are the source of truth, binary assets are build outputs, and a human is needed only for a short list of mostly one-time steps.

### Text as the source of truth

| What | Authored as | Becomes engine data by |
| --- | --- | --- |
| Gameplay, physics control, tools | C++ in `Source/` | `Build.bat` |
| Physical parameters | JSON in `Config/Tuning/` | Loaded at runtime, hot-reloaded, overridable from the console (`Sandbox.Tune`) and the command line (`-Tune`) |
| Physics asset: bodies, shapes, masses, limits | JSON spec | Editor-module commandlet using `FPhysicsAssetUtils` and constraint templates |
| Room layout and lighting | JSON spec | Commandlet builds the map; lighting bake commandlet |
| Animation logic | Native `UAnimInstance` with a custom proxy | Compiled; no Animation Blueprint |
| Input mappings | C++ creating input actions at startup | Compiled; no input assets |
| Test scenarios | JSON in `Scenarios/` | Read by the scenario runner |
| Imported art: meshes, textures, animations, MetaHuman | Source files in Git LFS | Import script through editor Python |

Animation Blueprint graphs are the one asset type with no scripting route: [Python can create the asset but cannot populate its graph](https://forums.unrealengine.com/t/creating-animation-blueprints-control-rigs-using-python-scripting/136188). The native animation instance removes the need.

### Command set

Each script wraps one engine command, prints machine-readable output and returns a meaningful exit code.

| Script | Wraps | Returns |
| --- | --- | --- |
| `build` | `Build.bat <Project>Editor Win64 Development -WaitMutex` | Compiler errors in `file(line): error` form |
| `gen-assets` | `UnrealEditor-Cmd.exe -run=GenerateAssets` | `Content/Generated/` plus a result JSON |
| `test` | `-ExecCmds="Automation RunTest <filter>;Quit" -nullrhi -unattended` | Test report JSON |
| `scenario` | `-game -nullrhi` with a fixed timestep and a scenario file | Metrics JSON, pass or fail |
| `sweep` | Several `scenario` processes in parallel with parameter overrides | One results table |
| `shot` | `-game -RenderOffscreen` with timed screenshots | PNG files the agent reads |
| `perf` | `-game -emulatestereo -csvprofile` | Frame-time percentiles |
| `package` | `RunUAT.bat BuildCookRun` | Packaged Win64 build |

### The loop

1. Edit C++, a parameter file or a spec.
2. Run `build` if code changed; fix errors until it is clean.
3. Run `gen-assets` if a spec changed.
4. Run the scenario suite headless.
5. Compare metrics with the thresholds and with the committed baseline.
6. For visual questions, run `shot` and inspect the images.
7. Commit when green, with updated baselines.

### Guardrails for agents

- A `CLAUDE.md` and `AGENTS.md` at the repo root hold the commands, the module map and the rules. `Intermediate/`, `Binaries/` and `Saved/` are excluded from context.
- No Live Coding. It [cannot add new reflected functions](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8); the agent closes the editor, builds, and relaunches.
- Every script writes a result file. Editor Python can exit with success after an error, so exit codes alone are not trusted.
- A task is done when its scenarios pass, not when the agent says so.
- Epic's official MCP server is Experimental in 5.8. It is used for inspection when convenient and nothing depends on it. Third-party MCP servers are not used.

### Toolchain

- **Engine:** UE 5.8.4 installed through the Epic Games Launcher, about 60 GB. A source build needs 160 GB or more and is not required.
- **Compiler:** Visual Studio 2026 is already on this machine and is [Epic's recommended version for 5.8](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine). The MSVC 14.50 toolset must be added and pinned: the default 14.51 toolset [breaks 5.8 source builds](https://forums.unrealengine.com/t/ue-5-8-fails-to-compile-with-visual-studio-2026-hash-map-missing-source-fix/2732357).
- **Other:** Windows SDK 10.0.22621, the version the installed engine's `Windows_SDK.json` prefers (newer SDKs are accepted); the engine bundles its own .NET.
- **Version control:** Git with LFS for `.uasset`, `.umap` and source art. `Content/Generated/` is ignored if regeneration proves fast and deterministic.

### What still needs a human

| Step | How often |
| --- | --- |
| Epic account sign-in, EULA and engine install in the Launcher | Once |
| Administrator approval for Visual Studio components | Once |
| MetaHuman cloud sign-in for auto-rig and texture download | Once per character |
| Claiming Megascans and other Fab assets | Once per asset set |
| Meta XR Simulator install and sign-in | Once |
| Headset sessions to judge feel, weight and comfort | Every milestone gate |
| Sign-off on visual quality and on whether motion looks natural | Every milestone gate |

## Physics testing and diagnostics

Every physical behaviour has a scenario: a JSON file that places the NPC, applies scripted stimuli at fixed times, simulates at a fixed step with no rendering, and writes metrics. Tuning then becomes a measured search an agent can run unattended.

### Scenario format

```json
{
  "name": "push_chest_front_80",
  "duration_s": 6.0,
  "npc": { "pose": "standing" },
  "events": [
    { "t": 1.0, "type": "impulse", "body": "chest",
      "impulse_ns": 80, "direction": [1, 0, 0] }
  ],
  "expect": {
    "end_state": "Standing",
    "fell": false,
    "max_joint_stretch_cm": 2.0
  }
}
```

Event types are a direct impulse on a named body, an effector trajectory (strike or push), a grab with a hand path, and a parameter override. Recorded live sessions replay through the same format.

As built so far: `impulse` (a single blow, or a push when it has a duration), `effector` (a trajectory of one of the player's hands: keys with a time and a place, straight lines between them), `pin`, `console` (any console command at a simulated time; a parameter override is `Sandbox.Tune ...`) and `edit_tuning_file` (an edit of the run's own copy of the tuning files, to test hot reload). Expectations include the end state and the whole state sequence. The format in force is in `Scenarios/README.md`.

### Standard suite

Thresholds are starting targets to be tightened as tuning matures.

| Family | Stimulus | Passes when |
| --- | --- | --- |
| Stand | No input for 10 simulated minutes | Pelvis drift under 5 cm, no state change, no visible jitter at rest |
| Push matrix | Impulses of 10, 30, 80 and 200 N·s at head, chest and thigh from 8 directions | Outcome class (stand, step, fall) matches the table; left and right results are symmetric |
| Strikes | Slap, punch and kick trajectories at head, torso and leg | No tunnelling; peak joint angular velocity under 30 rad/s; target region visibly displaced |
| Grab and drag | Lift an arm; pull a standing NPC; drag a fallen NPC 2 m with one and two hands | Joint stretch under 3 cm; hand force stays at its cap; no blow-up |
| Fall and rest | Knockdowns from 8 directions | Comes to rest within 3 s; kinetic energy never rises without input; penetration under 2 cm |
| Get-up | 50 randomised falls, including against a wall and furniture | Returns to Standing in 90% of runs at Milestone 3, 95% at Milestone 5 |
| Soak | 10 minutes of random stimuli | No NaN, no body outside the room, no stretch above threshold |
| Repeatability | Any scenario run twice | Identical trajectory hash, or deviation inside a stated tolerance |
| Performance | Scripted interaction with stereo rendering | 99th-percentile frame time at or under 11.1 ms; physics step time within budget |

For scale, an 80 kg person walking at 1.25 m/s carries 100 N·s of momentum, and a 200 N·s impulse changes the body's speed by 2.5 m/s.

### Metrics recorded on every run

- State timeline, time upright, fall time and direction.
- Centre-of-mass path and the capture-point margin to the support polygon.
- **Assist share:** the fraction of supporting force that comes from the world-space assist. It is the numeric proxy for the marionette look and should fall as balance improves.
- Peak joint angular velocity, maximum joint stretch, joint-limit violation.
- Maximum penetration depth and tunnelling count.
- Kinetic-energy trace and time to rest.
- Physics step time, average and worst.
- A hash of all body transforms at fixed checkpoints.

As built in M2: the state timeline, the fall, centre-of-mass drift, the assist load, peak joint angular velocity, joint stretch, joint-limit violation, physics step time and the hashes are recorded on every run; penetration depth and the tunnelling count are measured for the player's hands (by the runner, with scene queries after every frame); the capture-point margin, the kinetic-energy trace and penetration between the character and the world come with M3.

### Diagnostic tools

| Tool | Triggered by | Used for |
| --- | --- | --- |
| Chaos Visual Debugger | [`p.Chaos.StartVDRecording` and `p.Chaos.StopVDRecording`](https://dev.epicgames.com/documentation/en-us/unreal-engine/recording-to-file?application_version=5.8) | Frame-by-frame replay of particles, contacts and joints; a human opens the recording in the editor |
| Engine debug draw | `p.Chaos.DebugDraw.Enabled 1`, `p.Chaos.Solver.DebugDrawJoints` | Joint frames, shapes and sleep state in the live view |
| Project overlay | Console command | Centre of mass, support polygon, capture point, state, drive targets against actual pose |
| Offscreen screenshots | `shot` script | Images an agent can inspect at marked moments of a scenario |
| CSV profiler and Unreal Insights | `-csvprofile`, `-trace=cpu,gpu,frame` | Frame-time breakdown per thread |
| Meta XR Simulator | [`metavr` CLI](https://developers.meta.com/horizon/documentation/unreal/xrsim-getting-started/) | Smoke test of the OpenXR path with no headset |

As built in M2: the project overlay is `Sandbox.Overlay all|off|toggle|<layers>` with the layers `com`, `support`, `joints`, `assist`, `contacts`, `state` and `hands` (the capture point joins them in M3); screenshots are `Tools\scenario.ps1 -Shot`, with `-ShotAt` for moments of the run and `-Overlay` to draw the overlay into them. The layers are described in `Scenarios/README.md`.

### Parameter sweeps

`sweep` launches several headless runs in parallel, each with a parameter override file, and gathers their metrics into one table. An agent uses it for coordinate searches over muscle strength, damping and assist, and keeps the best set as the new baseline.

### Limits of automation

- **Determinism is not guaranteed.** Epic documents none for Chaos, and solver parallelism grew in 5.6 to 5.8. Milestone 0 measures run-to-run deviation; if runs are not identical, tests use tolerance bands and repeated runs.
- **Headless animation needs forcing.** With no renderer nothing counts as recently rendered, so the NPC mesh is set to always tick its pose. One comparison against an offscreen-rendered run confirms the two agree.
- **Metrics catch instability, not charm.** Whether motion looks natural is judged by a human from rendered clips at each gate.

## Milestones and acceptance criteria

Six milestones, each closed by a gate an agent can check from CLI and, from M1 on, a short human review in the headset. The first two exist to retire the three unknowns before the expensive work begins.

```mermaid
flowchart LR
    M0["<b>M0 Toolchain and agent loop</b><br/>Engine, project skeleton, CLI scripts,<br/>passive ragdoll test<br/><i>Gate: clean clone to green tests<br/>with one script</i>"]
    M1["<b>M1 Feasibility spikes</b><br/>MetaHuman, generated physics asset,<br/>baked room, stereo cost<br/><i>Gate: stereo frame within budget;<br/>assets built from text</i>"]
    M2["<b>M2 Active ragdoll core</b><br/>Muscles, assist, flinch, fall,<br/>desktop effectors, overlays<br/><i>Gate: stands 10 min; push matrix passes;<br/>no joint stretch</i>"]
    M3["<b>M3 Balance and recovery</b><br/>Capture point, stumble steps,<br/>protective fall, get-up<br/><i>Gate: get-up succeeds in 90%<br/>of 50 random falls</i>"]
    M4["<b>M4 Full interaction and VR</b><br/>All verbs, two-hand grab, VR hands,<br/>haptics, replay<br/><i>Gate: verb scenarios pass;<br/>headset review signed off</i>"]
    M5["<b>M5 Photoreal at 90 FPS</b><br/>Final room and bake, soft tissue,<br/>audio, packaging<br/><i>Gate: 90 FPS at p99 on headset;<br/>10 min soak clean</i>"]
    M0 --> M1 --> M2 --> M3 --> M4 --> M5
    style M1 stroke-width:3px
```

*Roadmap: feasibility is settled at M1, before the ragdoll work starts.*

A rough estimate is 13 to 16 weeks with agents implementing and one person reviewing at gates. M3 carries the most uncertainty.

### M0: Toolchain and agent loop (about 1 week)

Install UE 5.8.4 and the MSVC 14.50 toolset. Create the C++ project with its five modules, the CLI scripts and `CLAUDE.md`. Drop the stock mannequin as a passive ragdoll through scenario runner v0.

- [x] One script takes a clean clone to a passing test run
- [x] A drop-test scenario runs headless at a fixed step and writes metrics JSON
- [x] Run-to-run deviation is measured and the tolerance policy is written down
- [x] Headless and offscreen-rendered runs give matching bone transforms
- [x] An agent completes an edit, build and test cycle without the editor GUI

**M0 completed on 22 November 2026.** A clean-state bootstrap takes 98 s. Three headless runs and one rendered run of each scenario gave identical per-frame hashes. A headless scenario simulates about 10 times faster than real time. Details are in `docs/testing.md`. One deviation from the plan: the passive ragdoll is the stock UE5 mannequin copied from the engine's templates, because the engine's tutorial character explodes when fully simulated.

### M1: Feasibility spikes (about 2 weeks)

Assemble and import the MetaHuman. Generate the physics asset from its JSON spec. Generate and bake a graybox room from CLI. Launch through OpenXR and measure stereo cost. Hang the ragdoll from one hand constraint to stress the joints.

- [x] Character in the baked test room renders in stereo within 9 ms of GPU time at headset resolution
- [x] Regenerated physics asset matches the spec's body count, masses and limits, checked by a test
- [x] The lighting bake completes from the command line
- [x] The same build launches in VR with `-vr` and flat with `-nohmd`
- [x] Joint stretch under an 80 kg hanging load stays under 3 cm
- [x] Renderer path, character source and solver settings are confirmed or switched to their fallbacks

**M1 completed on 22 November 2026.** The MetaHuman is created by script; the physics asset (16 bodies, 80 kg) and the test room are generated from JSON specs; lighting bakes from the command line; hanging the body from one hand stretches no joint by more than 0.49 cm. In the PlayStation VR2 the test scene takes 7.1 ms of GPU time (7.8 ms at the 99th percentile) at 90 FPS with TAA and an 85% render scale; at SteamVR's full target it takes 10.6 ms. The owner confirmed the choices: deferred renderer with baked lighting and TAA, MetaHuman, synchronous physics at 90 Hz with two substeps. DLSS evaluation is deferred to M5. Deviations: collision shapes and joint frames come from the stock MetaHuman asset as a template, joint limits are still the stock values, and flat launches switch the OpenXR plugin off because `-nohmd` alone costs 26 s at start-up. Measurements are in `docs/testing.md`. Two defects of that template were found and fixed in M2, and the hanging-load figure changed with them; see the M2 section.

### M2: Active ragdoll core (about 3 weeks)

Muscles, balance assist at level 0, and the Standing, Flinch, Falling and Lying states. Parameter file with hot reload. Desktop pawn with effectors, impulse tool, drag handle and overlays.

- [x] Stand scenario passes: 10 minutes, drift under 5 cm
- [x] Push matrix: 10 N·s never causes a fall, 200 N·s always does, left and right agree
- [x] Strike scenarios show no tunnelling and no joint above 30 rad/s
- [x] No scenario shows joint stretch above 2 cm
- [x] A parameter edit takes effect in a running session
- [x] Physics step averages 1.5 ms or less

**All five steps of M2 are built and all six criteria are met. The owner's own review (the desktop session by hand and three smaller items) was deferred by the owner on 23 November 2026 and does not hold up M3; the list is at the end of this section. Step 1, "the character stands", was finished on 22 November 2026.** The NPC stands under full simulation for 10 simulated minutes: pelvis drift 0.76 cm, joint stretch under 0.01 cm, solver time 0.39 ms per frame, identical results in repeated headless and rendered runs. The target pose is the reference pose (an A-pose); there is no idle animation yet. Measurements are in `docs/testing.md`.

Decisions taken in step 1:

- **Muscles are force-mode springs in physical units** (N·m/rad), not Physics Control's default acceleration mode. That mode scales each drive by the inertia of the two jointed bodies alone, which leaves the load-bearing joints far too weak. Starting values: legs 800, spine 600, arms 150, neck 80 N·m/rad.
- **Balance level 0 turned out lighter than planned.** With muscles of that strength the legs carry the body; the assist's commanded force is 4.6% of body weight and its main job is to stop the feet creeping across the floor. These figures are the baseline for the M3 assist-share criterion.
- **64 solver position iterations on the NPC's bodies** (see Solver and timing).
- **Feet are flat boxes** written out in the physics asset spec.
- **Tuning values have names** (`muscles.legs.stiffness_nm_per_rad`, `assist.pelvis.horizontal_n_per_m`, `solver.position_iterations`) and can be overridden per run from the command line. The parameter file and hot reload came in step 2, below.

Corrections to M1 found in step 1. The stock MetaHuman physics asset, used as the template, has joint anchors 1.5 to 3.6 cm apart on all 15 joints (M1 recorded only the largest) and an orientation motor on every joint. A falling body hid both; a standing one could not start without its skeleton shortening by 12 cm and its elbows and knees flexing. The generator now fixes both. Without the motors the passive body is looser: joint stretch under the hanging load is 1.52 cm, not the 0.49 cm recorded at M1 (the limit of 3 cm is still met).

Not done yet in step 1, against the plan: muscle torque caps exist as parameters but are left at "no cap" until the fall states need them; joint limits are still the stock symmetric values, and the stock joint axes differ between the left and right side, which the push matrix's left-right criterion will need fixed first.

**Step 2 of 5, "the parameter file", was finished on 22 November 2026.** Every tuning value of the body is read from `Config/Tuning/body.json`; the code has no defaults left. A running session takes over an edited file within 30 frames and an override from the console or the command line on the next tick. Three scenarios prove it headless: muscles switched off from the console in the middle of a run (`tune_live_override`), the same through an edit of the file (`tune_live_file`), and a change of the solver counts (`tune_live_solver`). The stand scenarios give the same hashes as before the change, to the bit. Measurements are in `docs/testing.md`.

Decisions taken in step 2:

- **The store is generic and lives in `SandboxCore`; modules declare what they understand.** `FSandboxParamStore` knows files, names, overrides and revisions, not bodies. `SandboxBody` declares its names, its states and its range checks to it, and the store refuses any file or override that fails them. Every change is applied as a whole or not at all; a bad edit leaves the previous values in force.
- **Hot reload is a poll, not a file-system watcher.** Every 30 frames the store compares the files' size and modification time with what it loaded. It counts frames and never reads the clock, so a run whose files do not change is not affected. In a scenario the runner polls on fixed frames of the run.
- **Overrides are console commands, not one console variable per value.** `Sandbox.Tune name=value,...`, `Sandbox.TuneReset`, `Sandbox.TuneList`, `Sandbox.TuneReload`; on the command line `-Tune=` and `-TuningDir=`. Overrides lie on top of the files and survive a reload.
- **Per-state values are part of the format now,** ahead of the states themselves: the store resolves `states.<state>.<name>` over `<name>`, and the body's checks cover every state that sets a value. The body reads only the base values until step 3.
- **The rig's bone names moved to the file too.** They are read when the body starts and cannot change in a running body.
- **Falling has a metric:** the pelvis below half its starting height. Scenarios can expect `fell`, a minimum fall time and an exact number of tuning changes.

Found in step 2: Chaos does not apply new per-body solver iteration counts to a body whose simulation island is not changing, so the first live change of `solver.position_iterations` was accepted and did nothing. The controller now adds a constraint that restricts nothing for one frame, which makes the island take the new counts.

Not done in step 2: the torque caps are still at "no cap" and the N·m/rad conversion is still uncalibrated, as after step 1; there is no `sweep` script yet (`-Tune` and `-TuningDir` are what it will be built on); the tuning directory is not yet staged for a packaged build, which M5 needs.

**Step 3 of 5, "states and the push matrix", was finished on 22 November 2026.** The body has the states Standing, Flinch, Falling and Lying, joint limits that follow the table above and are mirror images left and right, and it passes the push matrix: 128 pushes (10, 30, 80 and 200 N·s on head, chest and both thighs from 8 directions), of which no 10 N·s push causes a fall, every 200 N·s push does, and every mirror-image pair ends the same way. The largest joint stretch in the matrix is 0.31 cm. Measurements are in `docs/testing.md`.

Decisions taken in step 3:

- **Joints are described by anatomy, not copied from the stock asset.** The spec gives each joint the direction of its limb, the axis it bends about and a range per axis that need not be symmetric (a knee bends one way only); the generator builds the engine's frames from that. The right side is generated as the mirror image of the left, so the two cannot differ. The stock asset's own axes were not mirror images, and the reference pose sat off-centre in its limits.
- **A fall is decided by the pelvis.** Balance level 0 cannot win balance back, so the body is let go once its pelvis is more than 15 cm from where it stands (about the edge of the feet's support), 20 cm below its standing height, or tilted by more than 35 degrees. All three are tuning values. The capture point replaces this in M3.
- **A hit reaches the body through its own entry point,** `USandboxBodyController::ApplyImpulse`, which applies the impulse and lets the character feel it. Impulses within 0.15 s of each other are one hit. Hits by other bodies will arrive through contact events once there are effectors (step 4).
- **A flinch weakens the muscles of the hit region** (spine, neck, arms or legs) to 30% and brings them back in a straight line over 0.35 s.
- **A fallen body is at rest when it is slow as a whole** (mass-weighted speed), not when its fastest body is: a trembling hand must not keep it from lying.
- **The push matrix is a tool of its own,** `Tools\push-matrix.ps1`: it generates the 128 scenarios, runs them eight at a time (about 3 minutes) and checks the rules. Four representative hits and pushes are also in the ordinary suite. `Tools\bootstrap.ps1` runs both.
- **Each impulse of the matrix is a single blow by default,** as the plan words it. Delivered as a push over 0.1 s the rules hold as well; both tables are recorded.

Found in step 3: a passive body runs on the engine's 8 solver iterations, the NPC on its own 64, and the difference decides the stretch under load: hanging from one hand, the wrist stretches by 2.9 cm at 8 iterations and 0.1 cm as the NPC proper. The hanging scenario now runs the NPC under active control, as it would be when grabbed.

Deviations and what is not done in step 3: the shoulder is an approximation, because the engine limits the two swings together as one cone (it reaches straight up and hangs at the side, but reaches back by about 20 degrees instead of 50), accepted for M2 and to be looked at again in M4; the knee's range is -2 to 150 degrees, not 0 to 150, because a standing knee that sits exactly on its limit makes the feet rock almost twice as fast; the flinch has no pose offset yet, only the weakening (the offset comes in M3, with the protective pose); the fall has no protective pose (M3); outcomes for 30 and 80 N·s are recorded but not judged, since the outcome table is an M3 criterion; the torque caps are still at "no cap" and the N·m/rad conversion is still uncalibrated.

**Step 4 of 5, "desktop pawn with effectors and tools", was finished on 22 November 2026.** The player has two physical hands: 1.5 kg spheres, each pulled toward its target pose by a spring that can apply 300 N at most. The character feels them through its own contact events and flinches by the rules it already had: a hand pushed into the chest makes it flinch at 6.0 N·s and stand, a hand at 8 m/s on the head delivers 14 N·s, a push that is held makes it fall, and a hand that passes in front of it without touching leaves it as it was. A hand held against the floor presses with 300.0 N, which confirms the cap and the units of the contact events in one measurement. Scenarios script the hands with a new `effector` event; six such scenarios are in the suite and give identical hashes in repeated headless and rendered runs. The desktop pawn (free camera, the two hands, impulse tool, drag handle, pause, single step, slow motion, reset) runs in an interactive session, `Tools\play.ps1`, which holds 90 frames per second on the simulation's fixed step. Its tools are also exercised without a person by `Tools\play.ps1 -Smoke` (13 checks). Earlier scenarios give the same hashes as before the step, to the bit. Measurements are in `docs/testing.md`.

Decisions taken in step 4:

- **The character learns of a hit from its own contact events,** as planned. `USandboxBodyController` listens to its mesh's contacts and feeds the impulse of each into the same accounting an `impulse` event uses. It counts only contacts with other simulated bodies: the floor under the feet alone reports 8.7 N·s per frame, above the flinch threshold. It knows nothing of hands; a thrown prop will be felt the same way.
- **The impulse tool is aimed by the pawn and carried out by the session.** `SandboxInteraction` may not call the character, and an impulse applied to the rigid body alone would not be felt, so it would not match the push matrix. The pawn announces "this impulse on what the crosshair hit"; `USandboxPlaySubsystem` in `SandboxScenario`, which depends on both sides, applies it through `USandboxBodyController::ApplyImpulse`, the entry the scenarios use. The reset of the character goes the same way. The drag handle needs none of this: it is a spring on whatever body was clicked, and lives in `SandboxInteraction`.
- **An interactive session is a scenario opened by hand.** `Tools\play.ps1 -Name <scenario>` takes the map, the body and the camera from the scenario file, so any scenario can be opened and prodded. The code that builds the scene is shared with the runner (`FScenarioStage`).
- **A scenario run has no pawn.** The runner swaps the project's pawn for a spectator without a body, so that hands exist in a scenario only when its file asks for them.
- **One spring for hands and drag handle,** `FSandboxWorldSpring`: a joint to the world with a position drive, solved implicitly together with the contacts, in force mode and physical units. The engine caps such a drive axis by axis; the caps are set every frame along the direction of the force asked for, so the hand is as strong along a diagonal as along an axis (measured: the same peak speed, 11.80 m/s, in both).
- **The target source answers "where should this hand be at the start of the coming step", with a velocity.** Three sources fit behind it: the desktop one (hands rest below the line of sight and reach straight ahead while a button is held), the scripted one (keys of a scenario file, straight lines at constant speed), and the tracked controllers in M4.
- **Hands have no gravity and never sleep.** The arm that would carry a hand is not simulated, so its weight is not charged to the force cap. A hand at rest fell asleep and ignored its spring until it was woken; the spring now wakes what it pulls.
- **Time control leaves the physics step alone.** Paused, the world stands; in slow motion it makes its full step on one frame in four; a single step is one frame. The engine's time dilation would shorten the substeps and show something full speed never computes.
- **The session holds its own frame rate.** With a fixed timestep the engine runs as fast as it can, so the session waits each frame out against the clock. That is the one place that reads the clock; nothing in the simulation does.
- **Tool numbers are tuning values too:** `Config/Tuning/effectors.json` (mass, radius, spring, damper, force cap, the same for orientation, snap distance) and `tools.json` (impulse and its wheel step, the drag spring, where the hands rest and reach, camera speed, slow-motion divisor). All are hot-reloaded; a live change of the force cap and of the mass was checked to reach the solver.

Found in step 4: the peak force of a contact is not limited by the spring's cap, because a moving hand that is stopped gives up its momentum as well (14 N·s in one frame reads as 1270 N); the cap is checked with a hand held still. A push that is held makes the character flinch again each time a flinch ends, every 0.35 s, until it falls: the same accounting as for a long `impulse` push, left for M3 to decide.

Deviations and what is not done in step 4: the hands are spheres, with no hand mesh and no fingers (the visible hand comes with VR in M4); the desktop hands only rest and reach, the strike verbs with wind-up are M4, as are grabbing with a hand, the kicking foot and recording a session; keys, mouse and the feel of the hands are checked by a person, the list is in `docs/testing.md`; the peak joint speed in the hand scenarios (7.6 rad/s at most) is recorded but strike speeds are step 5; the torque caps of the muscles are still at "no cap".

**Step 5 of 5, "overlays and strike scenarios", was finished on 22 November 2026.** Debug overlays show the centre of mass, the support polygon, every joint's drive target against the actual pose, the balance-assist targets, contacts, the controller's state and the hands; they are switched by the console command `Sandbox.Overlay`, work in an interactive session (the V key) and appear on scenario screenshots, which can now be taken at chosen moments of a run. Seven strike scenarios send a hand through the head, the chest, a thigh and a forearm of the standing character at the slap's 6 m/s and the punch's 8 m/s: the hand arrives at 6.25 and 8.87 m/s, nothing tunnels, the deepest penetration found is 0.01 cm, the fastest joint turns at 10.1 rad/s, and the struck body moves 1.4 cm (a thigh) to 22 cm (the forearm) while the character flinches and stays on its feet. The whole suite of 34 scenarios stays within 0.86 cm of joint stretch and a 0.45 ms mean physics step. All 27 earlier scenarios give the same hashes as before the step, and a run gives the same hashes with overlays on, headless and rendered. Measurements are in `docs/testing.md`.

Decisions taken in step 5:

- **Overlays live in `SandboxScenario` and only look.** They need the character and the hands, and that is the one module allowed to see both. `USandboxOverlaySubsystem` finds them in the world each frame, reads them through the accessors result files use plus one new read-only view of the controller's targets, and listens to their contact events. It writes nothing to a body. Without a renderer it stays off, does not tick and listens to nothing.
- **Joint drive targets come from Physics Control's own cached target pose,** not from a second guess at it: what is drawn is where the drives really pull.
- **The support polygon is the hull of the soles of the feet the floor pushed on in the last step.** The geometry is in `FSandboxBodyProbe`, with the centre of mass's margin to it, ready for the balance controller of M3; which feet stand is still worked out by the overlay from contact events, and by nobody in a run without one.
- **Tunnelling and penetration are measured by the runner from outside, not read from the solver.** After every frame it asks the scene how deep each hand's sphere is inside the character's bodies and the static world, and whether the hand's centre crossed a surface since the frame before. A sphere's centre cannot get closer to a surface than its radius, so a crossing is a hand that went through. Every run with hands fails on one such frame. The metric was shown to fail: without continuous collision detection a hand at 25 m/s ends 6.5 cm inside the chest.
- **Continuous collision detection stays on the hands and nowhere else.** At strike speeds a hand does not tunnel even without it; with it nothing tunnels up to 28 m/s. The plan's swept query exists as the measurement above; an impulse clamp was not needed, since no strike makes a joint turn faster than a third of the limit.
- **A strike scenario states its speed and has to arrive with it,** checked by a new expectation on the hand's speed at first contact, and has to move what it hits: 5 cm for the head, 3 cm for the chest, 1 cm for a thigh. These thresholds for "visibly displaced" were chosen here from what was measured.
- **The limits that hold for "any scenario" are checked by the suite itself:** `Tools\suite.ps1` fails if any simulated body's joints stretch by more than 2 cm or any scenario's physics step averages more than 1.5 ms, and names the worst scenario of each.

Found in step 5: since step 4 the character had been told of a contact only when the other body asked for contact events itself. The engine wants two flags set on a skeletal mesh before its bodies ask, and its own setter sets one. The hands ask, so every hand scenario was right; the floor does not, and a prop would not have. The controller sets both flags now, each foot reports half the character's weight against the floor, and nothing else changed (same hashes, same solver time). Continuous collision detection engages only above 6.5 m/s for a hand of this size, so the slaps run without it. A reset in the interactive session logged 30 warnings of Physics Control that no script read: the new character's controller was ticked before its mesh had a pose; it now waits for the first pose, and the session's self-test (14 checks) fails on such warnings.

Deviations and what is not done in step 5: overlay text is not drawn in a headset, only the lines; the support polygon and the foot contacts are shown but not yet used by the controller (M3); penetration is measured for the hands only, not between the character's own bodies or against the floor (the fall-and-rest family of M3); the kick, the strike verbs with a wind-up and strikes against a lying or falling character are M4; the suite takes 6.3 minutes one scenario after another, over the 5 minutes NFR-6 allows for build and suite together (the scenarios are independent and run in 91 s eight at a time, but the suite script was not made parallel in this step); the torque caps of the muscles are still at "no cap", the softness of the joint limits is untuned and the N·m/rad conversion is uncalibrated, as before.

**The M2 gate, 23 November 2026.** The six criteria, as measured on the final build:

| Criterion | Measured | Where |
| --- | --- | --- |
| Stand: 10 minutes, drift under 5 cm | 0.95 cm | `stand_npc_10min` |
| Push matrix: 10 N·s never a fall, 200 N·s always, left and right agree | 0 of 32 fall, 32 of 32 fall, 56 mirror pairs agree | `Tools\push-matrix.ps1` |
| Strikes: no tunnelling, no joint above 30 rad/s | 0 tunnelled frames, penetration 0.01 cm, fastest joint 10.1 rad/s | the seven `strike_*` scenarios |
| No scenario with joint stretch above 2 cm | 0.86 cm at most | `Tools\suite.ps1`, 29 scenarios with a simulated body |
| A parameter edit takes effect in a running session | on the next tick (console) or within 30 frames (file) | `tune_live_*` |
| Physics step averages 1.5 ms or less | 0.45 ms at most | `Tools\suite.ps1`, 34 scenarios |

Decided by the owner at the gate:

- **The 30 rad/s limit applies to strikes by a hand,** as the criterion words it. Calibrated impulses and falls record the fastest joint and are not judged by it: 80 N·s delivered within one frame is 7200 N, more than a hand can give, and the joints it whips to 31 rad/s (40 at 200 N·s, up to 52 in a passive body dropped on the floor) are answering a stimulus the product does not have.
- **A body that is not simulated has no joint stretch.** The portrait scenarios keep the stock MetaHuman asset, whose anchors are authored 3.6 cm apart; nothing solves those joints, so the figure is reported as the asset's offset and is not part of the criterion.

Deferred by the owner on 23 November 2026, to be gone through later; M3 starts without them:

- [ ] Try the desktop session by hand (`Tools\play.ps1 -Overlay all`): the keys and the mouse do what the screen says; the camera turns the right way up and down, which no run has checked; the hands feel weighted and do not launch the character; the drag handle is usable; slow motion is readable; a tuning file saved while the window is open changes the hands (`Config/Tuning/effectors.json`, another `max_force_n`); the V key switches the overlays, and they can be read.
- [ ] Look at a strike with the overlays: `Tools\scenario.ps1 -Name strike_punch_torso -Shot -ShotAt "3.07,3.09,3.25" -Overlay all`, then the pictures in `Saved\Scenarios\strike_punch_torso`.
- [ ] Accept or change the thresholds for "the struck region is visibly displaced": 5 cm for the head, 3 cm for the chest, 1 cm for a thigh (measured 7.9 to 13.2, 4.9 to 6.9, 1.4 to 2.0).
- [ ] Say whether the suite should be made parallel before M3: it takes 6.3 minutes one scenario after another, over NFR-6's 5 minutes, and 91 s eight at a time.

### M3: Balance and recovery (3 to 4 weeks)

Capture-point controller, Stagger with stumble steps, protective fall pose, Get-up from prone and supine, assist-share metric, parameter sweeps.

- [ ] 30 N·s pushes are absorbed without a fall; 80 N·s pushes produce a recovery step or a fall as the outcome table states
- [ ] Get-up succeeds in 90% of 50 randomised falls
- [ ] Standing assist share is at most half its M2 value
- [ ] Every fall comes to rest within 3 s with no energy gain
- [ ] Human review of rendered clips: it reads as a person losing balance, not a puppet

### M4: Full interaction and VR (2 to 3 weeks)

All seven verbs on desktop, one- and two-handed grab and drag, the kick effector, OpenXR hands, locomotion and haptics, session record and replay.

- [ ] Every verb has a passing scenario
- [ ] Grab and drag scenarios meet the stretch and force-cap criteria
- [ ] One binary runs flat and in VR, with VR-specific code confined to the target source
- [ ] A recorded desktop session replays to the same outcome
- [ ] The OpenXR path passes a smoke test in Meta XR Simulator
- [ ] Headset review: hands feel weighted, nothing launches, no discomfort

### M5: Photoreal at 90 FPS (2 to 3 weeks)

Final room art and lighting bake, character shading, soft-tissue motion, impact audio, performance tuning and packaging.

- [ ] 99th-percentile frame time at or under 11.1 ms in the headset during the standard interaction scenario
- [ ] Desktop holds 120 FPS at 2560×1440
- [ ] 10-minute soak is clean and get-up succeeds in 95% of runs
- [ ] The packaged Win64 build runs outside the development environment
- [ ] Visual sign-off on room, character and motion

## Risks and open questions

The technical risks sit in the Hardest problems table. What remains are assumptions the research could not confirm and decisions that are yours.

### Assumptions to verify

| Assumption | Why it is uncertain | Verified at |
| --- | --- | --- |
| The MetaHuman licence permits this use | The EULA text could not be retrieved; no violence clause is known, and the licence grants no likeness rights | Confirmed: the owner read the licence on 22 November 2026 and accepts it |
| Unreal Build Tool finds Visual Studio 2026 without editor settings | One 5.8.1 user needed to set a preference in the editor first | M0 |
| Fixed-timestep flags (`-UseFixedTimeStep`, `-FPS`, `-deterministic`) behave as expected in 5.8 | Long-standing flags, not re-confirmed against current docs | M0 |
| Skeletal animation and physics evaluate fully under `-nullrhi` | Extrapolated from dedicated-server behaviour | M0 |
| Lighting can be baked by commandlet | Lightmass regressions are reported on 5.6 to 5.8 | M1 |
| A MetaHuman in stereo fits a 2 ms GPU share | No published measurement exists | M1 |
| Generator code can set every constraint limit and profile | Engine APIs exist, but coverage for this spec is untested | M1 |
| Segment mass fractions for trunk, head and arms | Not checked against the original paper | M1 |

### Decisions for you

- [x] **Does the NPC depict a real person?** A recognisable likeness raises likeness-rights and store-policy questions and changes the character pipeline. The plan assumes a fictional character.

  Decided 22 November 2026: a fictional character, for now. Revisit before any change toward a real likeness.

- [x] **Which headset is the reference?** Quest 3 over Link or Virtual Desktop adds video-encoding load; a native PCVR headset does not. It sets the resolution in every performance gate.

  Decided 22 November 2026: PlayStation VR2 through the official PC adapter. It runs on SteamVR, which is installed and is the active OpenXR runtime on the development machine. The link is DisplayPort, so there is no video-encoding load. The panels are 2000×2040 per eye at 90 or 120 Hz; SteamVR's actual render target is larger than the panel and is read from SteamVR at the M1 gate.

- [ ] **Private prototype or public release?** Release brings content-rating and licence checks that a private build can skip.
- [ ] **Budget for paid content.** Character Creator 5 is about $299; animation and interior packs vary. The plan works with free sources but paid packs save time.
- [ ] **Agent permissions.** May agents install software, run multi-hour sweeps unattended and commit directly, or should each be approved?
- [ ] **NPC build.** The spec defaults to an 80 kg adult male; mass and proportions change every tuning value.

## Sources

Research was done on 22 November 2026. Forum threads are anecdotal and are marked as such where the plan relies on them.

**Engine and physics**

- [Unreal Engine 5.8 release notes](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-5-8-release-notes?application_version=5.8) and [release thread](https://forums.unrealengine.com/t/unreal-engine-5-8-released/2729274); [5.8.4 hotfix](https://forums.unrealengine.com/t/5-8-4-hotfix-released/2842775); [5.9 Preview thread](https://forums.unrealengine.com/t/2852840)
- [UPhysicsControlComponent API](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Plugins/PhysicsControl/UPhysicsControlComponent?application_version=5.8)
- [Physics constraint reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-constraint-reference-in-unreal-engine?application_version=5.8) and [physics project settings](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-settings-in-the-unreal-engine-project-settings?application_version=5.8)
- [Chaos Visual Debugger: recording to file](https://dev.epicgames.com/documentation/en-us/unreal-engine/recording-to-file?application_version=5.8)
- [Game Animation Sample updated for 5.8](https://www.unrealengine.com/tech-blog/download-the-latest-game-animation-sample-project-now-updated-for-ue-5-8)
- Forum: [async physics and Physics Control](https://forums.unrealengine.com/t/async-physics-and-physics-control/2838779); [Chaos determinism](https://forums.unrealengine.com/t/chaos-physics-determinism/151350)
- Alternatives reviewed: [UnrealJolt](https://github.com/Yadhu-S/UnrealJolt), [URLab (MuJoCo)](https://github.com/URLab-Sim/UnrealRoboticsLab)

**Active ragdoll practice**

- [Physical animation in Jedi: Fallen Order](https://gamedeveloper.com/programming/leveraging-physical-animation-to-sell-force-powers-in-i-jedi-fallen-order-i-)
- [Landfall on physics in TABS](https://gamedeveloper.com/design/how-landfall-games-finds-the-fun-in-physics-engines)
- [Boneworks physics design](https://gamedeveloper.com/design/how-classical-game-mechanics-and-physics-converge-in-vr-hit-i-boneworks-i-)
- [Half Sword developer interview](https://store.epicgames.com/news/how-half-sword-makes-bloody-brutal-medieval-combat-feel-real)
- [Epic: physics-driven animation](https://dev.epicgames.com/documentation/unreal-engine/physics-driven-animation-in-unreal-engine)
- [Capture point, Pratt et al. 2006](https://www.cs.utexas.edu/~shivaram/readings/b2hd-PrattCDG2006.html); [SIMBICON](https://www.cs.ubc.ca/~van/papers/Simbicon.htm); [DReCon](https://montreal.ubisoft.com/en/drecon-data-driven-responsive-control-of-physics-based-characters/)
- [HurricaneVR: tuning physical hand strength](https://cloudwalker2020.github.io/HurricaneVR-Docs/manual/rig/pdstrength.html); [VRPuppetHands](https://github.com/szanuj/VRPuppetHands)
- [Segment masses after de Leva](https://pmc.ncbi.nlm.nih.gov/articles/PMC9602786/table/healthcare-10-02054-t005); [joint range of motion](<https://wikem.org/wiki/Joint_ROM_(Table)>)

**Rendering and VR**

- [Nanite and Lumen for XR](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-and-lumen-for-xr-in-unreal-engine); [features by rendering path](https://dev.epicgames.com/documentation/en-us/unreal-engine/supported-features-by-rendering-path-for-desktop-with-unreal-engine); [XR performance features](https://dev.epicgames.com/documentation/en-us/unreal-engine/xr-performance-features-in-unreal-engine)
- [GPU Lightmass](https://dev.epicgames.com/documentation/unreal-engine/gpu-lightmass-global-illumination-in-unreal-engine)
- [NVIDIA DLSS plugin archive](https://forums.developer.nvidia.com/t/nvidia-dlss-unreal-engine-plugin-archive/377564)
- Forum: [instanced stereo in 5.8](https://forums.unrealengine.com/t/instanced-stereo-deferred-rendering-broken-in-5-8/2736359); [Lightmass issues in 5.7](https://forums.unrealengine.com/t/5-7-lightmass-issues-crashes-and-bugs/2694991)
- [UE 5.8 performance highlights, Tom Looman](https://tomlooman.com/unreal-engine-5-8-performance-highlights/)
- [Command-line arguments reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-command-line-arguments-reference)
- [Meta XR Simulator](https://developers.meta.com/horizon/documentation/unreal/xrsim-getting-started/)
- [MetaHuman licence](https://www.metahuman.com/license); [MetaHuman Creator Python scripting](https://dev.epicgames.com/documentation/metahuman/metahuman-creator-python-scripting-in-unreal-engine)
- [Megascans free on Fab](https://80.lv/articles/entire-quixel-megascans-library-now-available-for-free/); [Poly Haven API](https://github.com/Poly-Haven/Public-API); [ambientCG API](https://docs.ambientcg.com/api/v3/)

**Toolchain and automation**

- [Visual Studio setup for Unreal](https://dev.epicgames.com/documentation/unreal-engine/setting-up-visual-studio-development-environment-for-cplusplus-projects-in-unreal-engine); forum: [MSVC 14.51 and 5.8](https://forums.unrealengine.com/t/ue-5-8-fails-to-compile-with-visual-studio-2026-hash-map-missing-source-fix/2732357), [VS 2026 detection](https://forums.unrealengine.com/t/5-8-1-project-fails-to-build-with-visual-studio-2026/2743331)
- [Editor Python scripting](https://dev.epicgames.com/documentation/en-us/unreal-engine/scripting-the-unreal-editor-using-python); [running automation tests](https://dev.epicgames.com/documentation/en-us/unreal-engine/run-automation-tests-in-unreal-engine); [CQTest](https://dev.epicgames.com/documentation/unreal-engine/cqtest-test-framework-for-unreal-engine)
- [Unreal MCP in the editor](https://dev.epicgames.com/documentation/unreal-engine/unreal-mcp-in-unreal-editor?application_version=5.8)
- Forum: [editing physics assets via Python](https://forums.unrealengine.com/t/editing-physics-assets-via-python/142027); [Animation Blueprints via Python](https://forums.unrealengine.com/t/creating-animation-blueprints-control-rigs-using-python-scripting/136188)
- [Using Claude Code with Unreal Engine](https://store.algosyntax.com/tutorials/unreal-engine/how-to-use-claude-code-with-unreal-engine/)
