# DictatorSandbox

Unreal Engine 5.8 C++ prototype: one photoreal room, one active-ragdoll NPC the player can hit, push, grab
and drag, on desktop and PCVR. **The goal** (the owner, 26 November 2026): the ragdoll looks and moves naturally,
as a living person, and can be interacted with at every moment; both halves bind every part.

**Where things are:** requirements, milestones and the current status in `docs/PRD.md` ("Status", at the top);
the order of work in `docs/active-ragdoll.md`, section 8 (the second architecture: authority, parts R1 to R9, and
where each part of the natural-body track went; R10, for the owner's FR-44 to FR-55 of 27 November, placed there after R3 by decision
40, and decisions 41 to 48 taken the same day); criteria C1 to C26 and C28 to C40 with their human figures in `docs/natural-body.md` (C27 in
`docs/active-ragdoll.md`); measurements in `docs/testing.md`. What each milestone
built, with its figures, is in its section of `docs/PRD.md`. What has already bitten is in `docs/pitfalls.md`, by
area: read the areas a part touches before its plan (the entries every part needs are at the end of this file).

**In short (28 November 2026):** M1 to M3 done (the owner's hand checks deferred); M4 in progress, its rest after
R3. 19 bodies, muscles through Physics Control, capture-point balance with steps, the led get-up (the only one since R2's rest). The
player's movement in VR built (the right stick, a snap turn on the left, the player's body as a capsule of 10 kg; decisions 24
to 28; the owner's look in the headset to come). Baseline `start` (149 scenarios): the suite is 149 of 149; R2's hold by one hand,
its hit by impulse and its start from the lying pose are on since 27 November (`getup.hold_by_body` 1, `getup.hit_by_impulse` 1,
`getup.start_blend_s` 0.3; decisions 32, 35 and 36 confirmed after the videos): `grab_getup_held` passes without its `known_failure` mark, and the hit's two copies went into their scenarios. R2's step 0, the clean-up of dead switches, done (every hash kept). In the night of 26 November, the owner away: step 2
(decision 17: a get-up that was not led fails a run, `get_up_led`; `control.authority` by state, body and direction) and step 3
(the hold by one hand, decisions 13 and 14, behind `getup.hold_by_body` at 0, for the owner's look) and step 4 (a hit takes
authority by its impulse, decision 20, behind `getup.hit_by_impulse` at 0) and step 6 (the start blended from the lying pose,
decision 22, behind `getup.start_blend_s` at 0), every hash of `walk` kept; 141 scenarios (two for decision 20, seven of a push,
a blow and a hold in the get-up's phases), 54 unit tests; R3's first item, the fall's way and time, built as a measure only
(`control.fall.sense`); step 7, get-ups from eight ways of lying (`getup_laid_*`, laid by `body.laid_deg`), with a defect put right:
the support frame averaged the feet's turns across 180 degrees (baseline `laid`, 151 scenarios with two beside the table, 150 of 151);
R3's second, the fall's arms blended by its way, behind `fall.blend` at 0 (kept at 0 by the owner, to be worked on: decision 37).
Decisions 30, 31, 33 and 34 taken for the owner in the night, to confirm; 38: the side clips and C27 go with R2's matcher. R2's step 8
(27 November, decision 39): the motion library and the matcher (`FSandboxMotionLibrary`, `FSandboxMotionMatcher`, clips read as their
mirror images), behind `getup.match` at 0 for the owner's look, every hash kept; 154 scenarios (five `getup_match_*` switch it on
themselves), 59 unit tests. **R2's rest, 27 November** (`docs/recon/r2_rest_plan.md`, decisions 49 to 53): the matcher on; a body on its
side rolls by its own muscles first (`getup.roll`); a price on a late start (`match_late_cm_per_s`); the clip fitted to the surface and
the floor (`getup.fit_*`); a two-handed drag gives an attempt up (`getup.drag`); a held arm yields or is pulled in (`getup.arm_answer`);
the fingers on. All switched on after the videos (52); R2's gate met on three seeds. Then a scripted hand that follows a body
(`follow_bone_s`) for four scenarios of drags and holds in the later phases, and the clip-following path taken out (`getup.carry`,
`getup.match`, `getup.clip_*` and that path's values; a get-up that cannot be led is given up and the body stays down), every hash
kept; baseline `r2final`, 168 scenarios, 61 unit tests. **R3, 27 to 28 November, the owner away** (decision 54: R3 now, stage 2 of
the faster checks after it; `docs/recon/r3_plan.md`): every item behind its own switch at 0, every hash of `r3base` kept, 174 scenarios
(six `fall_catch_*`, `fall_phase_catch_*` switch R3 on themselves), 62 unit tests: the arms' tone on the time to impact (`fall.arm_curve`),
the catch in the trunk's frame (`fall.catch`), the head kept off the floor (`fall.head`), the palms to the floor (`fall.palms`); the curl,
the sit and the turn built and not proposed (`fall.curl`, `fall.sit`, `fall.turn`). With the four on (variant A), on the scorecard the
head strikes in 0% of the falls by blows (63%) and 9% by hand (55%), C14 by hand 83% (83%); on 96 more falls 29% and 16% (29%, 44%), C14
by hand 44% (61%); C16 by hand 5.38 m/s: the forward shove by hand; the get-up's figures as before. Nine causes of an arm's target
jumping found on 120 falls and put right, the last the catching elbow's hinge (its anatomical axis, the catch's alone). Decisions 55 to
57 taken for the owner, 58 the owner's (A, B with `fall.blend`, or not yet); videos on
https://claude.ai/artifact/Qj8WVh7f3SWzgDeHuWavSZ; `fall.aim` (N6) stays until the owner's word. Stage 2 of the faster checks begun the
same night with its SP-6 (`docs/fast-checks.md`, section 12): a queue's process keeps the NPC's meshes and physics asset after its
warm-up (`Tools\batch_keep.json`), a job's world in 0.04 s instead of 0.38, every tool's 1677 runs with the same hashes; and its SP-4,
the pool, started by hand (`Tools\pool.ps1 -Start`; one scenario 2.6 s, the suite 48 s, every run's hashes as without it; a build
stops it and does not start it again, decision 13). **R10 begun, 28 November 05:15 to 06:30, the owner away** (`docs/recon/r10_plan.md`;
`docs/testing.md`, "R10"; every step behind its own switch at 0, every hash kept: the suite's 174 of `r10base`, the matrix's 160, the
stand's 150, `record-replay -Perception`; 206 scenarios, baseline `r10end`, 65 unit tests): step 1, the player told to the body as a
viewer (a scenario's `viewer` event, the session's head and capsule, the head kept in recordings, a hand's grip; `control.behaviour.viewer_*`);
step 4, fear (`behaviour.fear`); step 5, staying down covered (`poses.protect_head`) while afraid with the player near, a rise turned
back by its clip run backward (`behaviour.stay_down`; C33 met on the stand of 150 with a viewer, `getup-stand.ps1 -Near 70`, three
seeds); step 2, chance with a seed (`FSandboxChance`, a scenario's `"seed"`; nothing draws yet); step 6,
the struck place covered after a hit while lying or rising (`behaviour.protect`; head, trunk, groin, leg; C34 not met for a leg struck
kneeling); step 8 in part, the commands `get_up` and `stop` (`behaviour.commands`; the console's `Sandbox.Command` and the desktop's keys U and J,
kept in recordings). Decisions 59 to 66 taken for the owner, 67 the owner's (which go on, after the videos
`Saved\Films\r10_staydown_*`, `r10_protect_hit`). **28 November, the owner's word:** 55 to 57, 59 to 66, 68 and the faster checks'
12 and 13 confirmed; **R10 switched on by decision 67** (`behaviour.fear`, `stay_down`, `protect`, `commands` 1 in the files; a
scenario that wants one off switches it off itself; baseline `r10on`, 207 scenarios; the matrix, the knockdowns and the stand keep
every hash; `docs/testing.md`, "R10 switched on"). Next (the second window of 28 November, `docs/recon/night2_plan.md`): R3's
decision 58 under the owner's delegation, the controllers' buttons for the commands and the page for the headset session of 29
November, R10's step 3 (the arbiter), `give_hand`, step 7. DLSS is deferred to M5.

Everything is driven from the command line. Do not rely on the Unreal Editor GUI.

## Commands

Run from the repo root in PowerShell. Each script prints a one-line verdict first (`BUILD OK`,
`SCENARIO FAIL: ...`), returns a meaningful exit code and writes a JSON result under `Saved\`.

| Task | Command |
| --- | --- |
| Clean clone to verified build | `powershell -ExecutionPolicy Bypass -File Tools\bootstrap.ps1` |
| Compile | `powershell -ExecutionPolicy Bypass -File Tools\build.ps1` (`-Rebuild` a full build; every action with its own time in `Saved\Tools\build.json`) |
| Unit tests | `powershell -ExecutionPolicy Bypass -File Tools\test.ps1` |
| One scenario, headless | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -Name drop_passive` |
| One scenario with a screenshot | add `-Shot` (renders offscreen, saves `final.png` next to the metrics); `-ShotAt "3.07,3.25"` adds pictures at those simulated times (`shot_3.070.png`) |
| Debug overlays in the pictures | add `-Overlay all` to a `-Shot` run or to `play.ps1` (or layers: `com,support,capture,joints,assist,contacts,state,hands`); in a session the V key, in any console `Sandbox.Overlay all\|off\|toggle\|<layers>` |
| One scenario in the headset | add `-VR` (headset on, SteamVR running; takes the scenario's duration in real time) |
| One scenario with other body tuning | add `-Tune "muscles.legs.stiffness_nm_per_rad=400,solver.position_iterations=32"`, or `-TuningDir <dir>` for another set of tuning files |
| All scenarios | `powershell -ExecutionPolicy Bypass -File Tools\suite.ps1` (eight engine processes take the scenarios from one queue; also holds the whole set to 2 cm of joint stretch and a 1.5 ms mean physics step, and prints the worst scenario of each; `-Long` adds the scenarios over 60 s, `stand_npc_10min`; `-Parallel 1` for physics step times nothing disturbed; `-Tune "name=value,..." -Label <name>` runs the whole suite against other values into `Saved\Suites\<name>`, leaving what baselines are made of alone) |
| A motion as a sheet of pictures over time | `powershell -ExecutionPolicy Bypass -File Tools\film.ps1 -Name hit_chest_200_falls -Times "3.1,3.2,3.3,3.4,3.5,3.7,4.0,5.0"` (`-Camera side\|front\|back\|quarter`, `-Events <file>`, `-Tune`; `Saved\Films\<name>\sheet.png`; `-Around 3.0 -Span 0.6 -Every 0.033` for a dense strip instead of `-Times`, `-Video` for an mp4 as well; several films in one call, rendered through one queue, up to four processes side by side: `-Name "a,b"` with the same settings, or `-Films <list.json>` each with its own) |
| Two videos side by side, the same camera and times, before and after a change | `powershell -ExecutionPolicy Bypass -File Tools\film-pair.ps1 -Name getup_push_weak -Around 8.3 -Span 3 -TuneB "getup.hit_by_impulse=1" -LabelA "switch 0" -LabelB "switch 1"` (`-File`, `-TuneA`, `-Camera`, `-Every`, `-Overlay`; the scenario runs to its end whatever its `end_when_standing_s`; `Saved\Films\<tag>\pair.mp4`, each side's film in `<tag>_a` and `<tag>_b`; `docs/active-ragdoll.md`, section 9) |
| What a recorded clip asks of the body, as a sheet of pictures | `powershell -ExecutionPolicy Bypass -File Tools\clip-film.ps1 -Clip M_ragdoll_getup_stand_B -ClipTimes "0,0.4,0.8,1.2,1.6,2.0,2.5" -Carry -RaiseCm 60` (the body hangs by its welded pelvis and is asked for the clip's pose at each time; `-Carry` moves the pelvis as the clip does; several clips separated by commas; `Saved\Films\clip_<name>\sheet.png`) |
| Keep the suite's results, or see what changed since | `powershell -ExecutionPolicy Bypass -File Tools\baseline.ps1 -Save <name>` before a change, `-Compare <name>` after it (or `suite.ps1 -SaveBaseline <name>` and `-Baseline <name>`): which scenarios have other hashes, and what moved in them; `-SaveRuns <folder> -Name <name>` and `-CompareRuns <folder> -Name <name>` the same for any tool's runs (a matrix's, a stand's, the three seeds'), run by run by both hashes |
| A pool of engine processes that wait for jobs | `powershell -ExecutionPolicy Bypass -File Tools\pool.ps1 -Start` (`-Processes 8`, `-IdleS 600`; `-Stop`, `-Status`): every tool's headless runs then go to it without the engine's start (one scenario 2.6 s instead of 7, the suite 48 s instead of 57), until it has waited `-IdleS` with nothing to do; a build, `gen-assets.ps1`, `bake-lighting.ps1`, `bench.ps1` and a run in the headset stop it first; `SANDBOX_NO_POOL=1` ignores it (`docs/fast-checks.md`, SP-4) |
| Several tools of runs through one queue | `powershell -ExecutionPolicy Bypass -File Tools\checks.ps1 -What suite,matrix,knockdowns,stand,seeds` (also `compliance`, `natural`, `natural_quick`, `grabs`; eight engine processes, the longest jobs first whichever tool planned them, each tool's rounds; `-Tune "..." -Label <name>` puts every tool's runs under the label and leaves the usual folders alone; each tool's verdict, `Saved\Checks\<label>\checks.json`; `docs/fast-checks.md`, SP-3) |
| How long the checks and runs take, or every tool's hashes | `powershell -ExecutionPolicy Bypass -File Tools\bench.ps1 -Label <name>` (three passes over the table of `docs/fast-checks.md`, section 3, medians in `Saved\Bench\<name>.json`; `-Builds` the three builds of decision 8, `-Full` a bootstrap, `-Against <name>` that measurement beside; it refuses while another engine process runs or under 18 GB free; about 15 minutes). `-Hashes`: every tool that runs scenarios once with its defaults, every run's hashes and every verdict kept; `-Hashes -Against <name>` holds them run by run (about 16 minutes): what a change to the queue, the start or the tools is held to |
| Scenarios over a set of tuning values | `powershell -ExecutionPolicy Bypass -File Tools\sweep.ps1 -Name legs -Scenarios "stand_npc,hit_head_30_stands" -Vary "muscles.legs.stiffness_nm_per_rad=400,800;muscles.legs.damping_s=0.03,0.06"` (every combination, or `-OneAtATime`; `-Sets "|a=1|a=2,b=3"` instead: sets of values changed together, an empty set the files as they are; `-Metrics` chooses the columns; a verdict line a run with its hashes, then the table, in `Saved\Sweeps\<name>`) |
| Push matrix (160 pushes, outcome by the owner's M3 table) | `powershell -ExecutionPolicy Bypass -File Tools\push-matrix.ps1` (`-PushSeconds 0.1` for pushes instead of blows, `-Tune` to try values, `-Impulses "30" -Enforce "30"` for one row, `-Rules m2` for the old gate; a step is a foot carried 10 cm or more, `-StepMinCm`, and shorter hops are reported as shuffles); against a baseline: `Tools\baseline.ps1 -CompareMatrix <name>` |
| The stand of 50 falls and get-ups (the criterion of M3 part 6: 90% end standing) | `powershell -ExecutionPolicy Bypass -File Tools\getup-stand.ps1` (26 falls on the bare floor, 8 each near a wall, the table and the chair; `-Seed 2` for other falls, `-Only "table,chair"`, `-Tune`, `-TuningDir` (for what `-Tune` cannot give, a clip's path), `-Film failed` for a sheet of pictures of every fall that does not end standing, `-Film "36,42"` of those trials; `-PressN 300` presses every fallen body down for 20 s and counts those that stood under it; `-PropsFile Tools\stand_furniture_props.json -Only floor -Floor 50` puts movable furniture around every fall on the bare floor; `-HoldHands 1` or `2` has that many hands take hold of the chest of every fallen body for 15 s, with the grab on, and counts the grips that came off and the bodies that pulled free and stood in that time; a trial ends once its body has stood for 3 s, `-StopStandingS 0` runs each for the whole 40 s; the places are worked out from where the room spec has the table and the chair; `-Near 70`: a viewer walks to 70 cm of every fallen pelvis at 5 s, stays 8 s and goes, and the stand counts the attempts begun afraid with it near, those turned back and how they came down, R10's C33, with `-Tune "behaviour.fear=1,behaviour.stay_down=1"`; with `-Near`, `-CommandAtS 3` tells every body `get_up` 3 s after
the viewer came and counts the answers and the time to the first motion, C39, with `behaviour.commands=1` in `-Tune`; the stand's
`-Seed` is every trial's `"seed"`) |
| The stand on three seeds side by side, one line a seed | `powershell -ExecutionPolicy Bypass -File Tools\getup-seeds.ps1 -Label <name>` (`-Tune`, `-TuningDir`, `-Seeds "1,2,3"`, `-PressN`, `-HoldHands`, `-Only`, `-StopStandingS`; three engine processes a stand, nine in all, `-Parallel`; results in `Saved\Matrix\getup_seeds\<name>`; a line of R2's gate over all seeds: C19's attempts given up and longest pose held, C27 in the median and by clip; each seed's time from the first attempt to standing and the authority by direction) |
| Friction between the lying body and the floor, by experiment | `powershell -ExecutionPolicy Bypass -File Tools\surface-friction.ps1` (knocks the body down and drags it by the pelvis with `-ForceN 800` for `-PushS 1.0`; the friction from its acceleration; `-Tune` to try values) |
| Knockdowns from 8 directions, judged by the fall's criteria | `powershell -ExecutionPolicy Bypass -File Tools\knockdowns.ps1` (a blow of 200 N*s on the chest from each; `-Impulses "200,300" -Bodies "spine_04,head"` for harder ones and the head, `-Tune`, `-CVars`, `-Film` for a sheet of pictures of every fall; `-ByHand` fells it with two fists that go through the chest at 4 m/s instead) |
| One generated scenario file | `powershell -ExecutionPolicy Bypass -File Tools\scenario.ps1 -File <path.json>` |
| Interactive session (a window, for a person) | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1` (`-Name <scenario>` opens that scenario's scene; keys are on the screen; the grab is on; `-Perception` lets the body see the hands, the foot and the things and switches on its gaze, anticipation and guard (N5), for that session only; `-Grab` switches the grab on for the session: G holds the hands open, and a hand that closes on the body holds it) |
| Self-test of the session, no window | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Smoke` (`-Smoke -Shot` renders it and saves `final.png`) |
| The grab in a session without a person | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Demo -Grab` (the grab switched on: the right hand opens, reaches into the chest and takes hold; the character is put back while it is held; the hand takes hold of the new one, pulls and lets go; a step of the bootstrap) |
| Record the hands of a session | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -Record <name>` (from the first frame; the K key ends a recording and starts the next; `Saved\Recordings\<name>\`: the recording, a scenario that replays it, what the session saw) |
| The session in the headset | `powershell -ExecutionPolicy Bypass -File Tools\play.ps1 -VR` (headset on, SteamVR running, controllers awake; the hands follow the controllers, the player's head is put 80 cm in front of the NPC, the right stick walks the player's body (a capsule the hands pass through), the left stick snap-turns 30 degrees, the view darkens as the eyes come near the NPC or the furniture; Escape in the window ends it; with `-Record <name>` as on the desktop; `-Seconds 180` ends it by itself and keeps the recording; afterwards it prints what was tracked, which controller keys arrived and how many frames a second went by; it refuses to start, `PLAY ERROR`, unless SteamVR's server and compositor are up and ready, `Get-HeadsetBlocker`; with SteamVR ready and no headset it ends after about 40 s with `PLAY ERROR`) |
| A session's hands recorded and replayed, held against each other | `powershell -ExecutionPolicy Bypass -File Tools\record-replay.ps1` (a session without a person uses the hands alone, `play.ps1 -Demo`; `-Perception` a session that sees, `demo_seeing`; `-Recorded <name>` for a recording a person made; `-Twice` also wants two replays with the same hashes; `-Takes` runs a session of three takes, `play.ps1 -Demo -Takes`, and holds each take's replay against it; `-Walk` runs the headset's way with a made-up headset, `play.ps1 -Demo -Walk`: the head put at arm's length, the stick walks the player's body into the NPC and along it, two snap turns, the stick walks away, eight checks, and the replay held to its first contacts (a shove is not replayed to the end); `-PelvisCm`, `-ImpulsePct`, `-Frames` are the limits) |
| What is in recordings of a person's hands | `powershell -ExecutionPolicy Bypass -File Tools\recording-report.ps1 -Name "takes*"` (every recording under `Saved\Recordings` that matches is replayed and read: how fast each hand's target went, how often its grip closed, what each hand delivered and to which bones, every hit the body told of with the hand's speed as it arrived and the largest contact force, what the body did, and whether the replay ends as the session did; `-Tune` replays with other values; `Saved\Reports\<name>\report.json`) |
| Grabs on seven bodies, pulled five ways at three speeds, judged by the grab's criteria | `powershell -ExecutionPolicy Bypass -File Tools\grab-pulls.ps1` (`-Bodies "lowerarm_l,head"`, `-Speeds "1,2,4"`, `-Directions "left,front,up"`, `-DistanceCm`, `-HoldS`, `-Tune`; where each body's surface is, it measures first with a hand of 20 N; a row a pull: what the body did, what the grip did, the joint's force, the stretch of the grab and of the body's joints; `Saved\Matrix\grab_pulls`) |
| The scorecard of the natural body: a row a criterion of `docs/natural-body.md` | `powershell -ExecutionPolicy Bypass -File Tools\natural.ps1 -Label <name>` (`-Baseline before_track` prints that report's figures beside this one's, `-Quick` leaves out the stand of 150 falls, `-Tune`, `-NoPrior`; reports and judges nothing; `Saved\Natural\<label>\report.json`; C25 is read from the latest suite) |
| How far a steady push moves the standing body | `powershell -ExecutionPolicy Bypass -File Tools\compliance.ps1` (20 and 60 N for 1.5 s on eight bodies from the front, the back and a side; `-Bodies`, `-Forces`, `-Ways`, `-Tune`, `-Idle`; `Saved\Matrix\compliance`) |
| The database of recorded poses for `metrics.pose_prior` | `powershell -ExecutionPolicy Bypass -File Tools\pose-prior.ps1 -Build` (from the clips `Specs/pose_prior.json` lists, copied from Epic's sample by `fetch-sample-anims.ps1 -PosePrior`; not committed; without `-Build` says whether it is there and current) |
| How far a hand lags behind its target | `powershell -ExecutionPolicy Bypass -File Tools\swing-lag.ps1` (scripted swings that peak at `-Speeds "2,4,6,8"` over `-DistanceCm 60`, each with the target's velocity given exactly, as the difference of two poses, as the headset's runtime gives it and not at all; `-Recording <file> -FromS 26 -ToS 61` adds a person's recording replayed three ways; `-Tune` for other hands; 35 s with a recording) |
| Run-to-run deviation | `powershell -ExecutionPolicy Bypass -File Tools\repeatability.ps1 -Name drop_passive -IncludeRendered` |
| Compare two runs | `powershell -ExecutionPolicy Bypass -File Tools\compare-runs.ps1 -A <dir> -B <dir>` |
| Regenerate assets from specs | `powershell -ExecutionPolicy Bypass -File Tools\gen-assets.ps1` (`-Force` also rebuilds up-to-date maps) |
| Bake a map's lighting | `powershell -ExecutionPolicy Bypass -File Tools\bake-lighting.ps1 [-Quality Preview] [-Force]` |
| Create, rig and assemble the MetaHuman | `powershell -ExecutionPolicy Bypass -File Tools\metahuman.ps1` (cloud steps need an Epic sign-in; see `docs/pitfalls.md`, assets) |

Typical times on the reference machine (after stage 1 of `docs/fast-checks.md`, 27 November): a build after an edit of the
controller's `.cpp` 10 s, of its header 24 s, a full build 103 s, one scenario 7 s (the 10-minute stand scenario 60 s), unit tests
8 s, the whole suite 53 s (154 scenarios; 62 s with 168 since R2's rest; 64 s with R3's 174, 56 s since SP-6 on 28 November; about 100 s with `-Long`), build, tests and suite after a `.cpp` edit 73 s, the scorecard
96 s (`-Quick` 36 s), the compliance table 18 s, the pose database 35 s, a record and replay 15 s (three takes 33 s, the walk with
a made-up headset 23 s), a report on a 72 s recording 14 s, the hand's lag on four speeds 13 s, the push matrix 43 s (one row of
40: 11 s), the knockdowns 12 s, the grab's 93 pulls 45 s, the stand of 50 falls 34 s (three seeds side by side, nine processes, 78
s), 4 scenarios times 3 sets in one sweep 15 s, regenerating the physics asset 10 s, a film of 12 pictures 10 s (two in one call
11 s, a film pair 16 s), the session's self-test 7 s (its grab demo 7 s), seven tools of runs as one check set 218 s, the whole
bootstrap 139 s (15 steps). An engine process starts in about 3.5 s (without the Zen server and the platform check) against 2 s of
physics in a scenario, which is why runs share processes; `SANDBOX_SEPARATE_PROCESSES=1` goes back to one process per run.

The loop: edit, `build.ps1`, `test.ps1`, `suite.ps1`, read the JSON, repeat. A task is done when its
scenarios pass, not when the code compiles. For anything visual, run a scenario with `-Shot` and look at the PNG.

No loops over tools that start the engine (each start costs seconds that one call pays once): several runs go through one
`sweep.ps1` (`-Sets` for values changed together), several films through one `film.ps1` (`-Name "a,b"`, `-Films`), several tools
through one `checks.ps1`, which replaces the batteries of `Saved\Recon` that ran tools one after another.

Before a change to the body or its controller, take a baseline (`suite.ps1 -SaveBaseline <name>`); after
it, `suite.ps1 -Baseline <name>` names every scenario whose hashes moved. Scenarios the change should not
touch must not be in that list, and the ones that are get explained in the part's report.

## Layout

| Path | Holds |
| --- | --- |
| `Source/SandboxCore` | Shared services: log category `LogSandbox`, JSON helpers, `FSandboxParamStore` (tuning files, overrides, hot reload, the `Sandbox.Tune*` console commands). Depends on no other Sandbox module. |
| `Source/SandboxBody` | The simulated body: `ASandboxRagdollActor`, `USandboxBodyController` (muscles, balance assist and the states Standing, Flinch, Stagger, Falling, Lying and GetUp; it tells whoever listens of every hit and change of state, `OnBodyEvent`, which a run lists under `control.events`, and of a grab and its release, which it does not feel and is told of by the run or the session, `TellHolds`; the sense of balance, `GetBalance` and `MeasureBalance`: standing feet, support polygon, capture point; the decision on it, `IsStepNeeded`; `FollowSupport` moves the assist's anchors with the feet; `UpdateStrategies` turns the leg targets (ankle, hip, lean into a held push); `StartGetUp` and `UpdateGetUp` get a lying body up by a recorded clip (the state GetUp: a body on its side rolls by its own muscles first, `UpdateRoll`; the clip, its start and where it is laid chosen by the motion library's matcher, fitted to the surface and the floor; led, the only get-up since R2's rest: the clip runs at its pace and stops only while the muscles come up, for a press that holds the body down, and at its end until the body is upright on both feet (given up after `getup.stall_s`), and `FSandboxCarry` pulls every body toward it; a get-up whose carry cannot be made is given up and the body stays down; `UpdateDrag` gives an attempt up when two hands drag the body, `UpdateHeldArms` makes a held arm yield or pull in; every attempt given up goes through `GiveUpGetUp` with a kind, `ESandboxGiveUp`, and `CanBeginGetUp` says what holds a get-up back); `ApplySurface` gives the bodies their friction and restitution (`surface.*`); `FeelFrame` splits what is felt into hit and pressure; `GetControlView` hands its targets to overlays), `FSandboxBodyTuning` (the body's tuning values by name, read from the store, poses among them), `FSandboxBodyProbe` (measurements: the support polygon, the capture point, energy, penetration into the world and into itself), `USandboxAnimInstance` (the target pose the muscles hold: a base pose blended from one to the next, turns added on top such as the flinch's; a native instance with its own proxy, no Animation Blueprint), `FSandboxPoseRig` (joint angles of a pose to bone rotations, on the axes of the physics asset's joints), `FSandboxIdleMotion` (the motion of a recorded idle clip, as each bone's turn from the clip's first frame, added to the pose of a standing body), `FSandboxClipPose` (a recorded clip made for another skeleton as target poses for the body's joints: each body turned, in the space of the whole body, by what the clip's bone is turned from its own reference pose, arms and legs pointed where the clip's point, every joint kept within its range; laid over the base pose by the animation instance's layer, `USandboxBodyController::SetClipPose`), `FSandboxMotionStats` (arithmetic on recorded motion: spread, period, a drive's torque), `FSandboxPosePrior` (a database of recorded poses and the likeness of a pose to them), `FSandboxMotionLibrary` (the clips a body may get up by, from `Specs/motion_library.json`, each also read as its mirror image, with what the matcher compares at every key and the contacts it makes with its floor; R2's step 8), `FSandboxMotionMatcher` (the lying body against the first second of each get-up: a weighted fit of the turn about the vertical and the shift, then the places' and the lean's distance; pure arithmetic; `getup.match_*`), `FSandboxCarry` (the authority of a reference over the body: one capped spring from the world on every body toward where a clip has it, per kg of the body, never downward, made when first needed; the led get-up, `getup.carry_*`), `FSandboxLimbTarget` (two-bone placement of a leg or an arm in the animation instance; asked for by `USandboxBodyController::SetFootTarget` for a foot and its private `PlaceArm` for a hand), `FSandboxWeightTorques` (N2's torques that carry the weight beyond each joint; built, its shares at 0), `FSandboxFingerRig` (N8 B's finger poses; its weight at 0), the body's perception (`TellSeen` with `FSandboxSeen`, `UpdatePerception`, `FSandboxPerceptionView`: what it sees, a threat, the gaze, anticipation and the guard, N5; on in a session with `-Perception`). |
| `Source/SandboxInteraction` | The player side. `USandboxEffectorComponent` (a hand, or a kicking foot, which is the same with other tuning and no grip: a sphere with mass, a fist or open by its target's grip; the open hand is shown as a plate and, with `effectors.hand.palm_collides`, is a second body, `USandboxPalmComponent`, that takes the ball's place while the hand is open; ask the hand through `GetHandTransform`, `GetHandVelocity`, `GetProbe`, not the component) held by `FSandboxWorldSpring` (spring with a force cap to a moving world point); `ASandboxEffectorRig` (the hands plus an `ISandboxTargetSource`: desktop, scripted, recorded, tracked; it can keep the targets of every stepped frame, `SetRecording`); `FSandboxTargetRecording` (what the hands were asked for, frame after frame, as a text file) and `FSandboxRecordedTargetSource` (plays one back frame for frame); `FSandboxTrackedTargetSource` (the hands of a player in a headset, as arithmetic on what an `ISandboxControllerTracker` says of each controller) and `FSandboxXRControllers` (that interface answered by the engine's XR system: the only file that knows of a headset); `FSandboxGrabJoint` (a grab: a joint that keeps a hand's own point and another simulated body together, free to turn; the hand takes hold with it when its grip closes within reach of a body, `effectors.hand.grab`, and passes simulated bodies while it holds); `FSandboxDragHandle`; `ASandboxPawn` (free camera, tools, time control, the kick and the open hands of the desktop, input made in C++; in a headset the origin of the play space, with the camera under it as the head, moved by the sticks, and the player's body, the effector `body`: an upright capsule of its own object type, `PlayerBody`, that the hands and feet pass through); `FSandboxLocomotion` (the headset's movement as arithmetic: the right stick, the snap turn about the head, where the player's body is to be and when what presses it back stops the stick; FR-42); `ASandboxPlayerController`, `ASandboxHUD`, `ASandboxGameMode`; `FSandboxInteractionTuning`. |
| `Source/SandboxScenario` | `UScenarioBatchSubsystem` (the job queue: one engine process runs many scenarios, each in a fresh world, with its own `run.log`, after a warm-up run; `-ScenarioQueue=<dir>`; with `-ScenarioKeep=<file>` the assets that file lists are loaded after the warm-up and kept for the process's life, `Tools\batch_keep.json`, SP-6; `SANDBOX_KEEP_ASSETS=0` without), `UScenarioRunnerSubsystem` (also measures the hands from outside: penetration, tunnelling, what a strike moved, and every frame each hand's gap to its target, written to `hands.csv`; and for the body, every frame: balance, energy, rest after a fall, penetration, written to `series.csv`), `FScenarioSpec`, `FScenarioStage` (builds a scenario's scene, its things with mass among it: `"props"`, boxes that simulate, `SpawnProps`), `USandboxPlaySubsystem` (the interactive session, `-Play=`; the only place where pawn and character meet in code; `FSandboxStandInHeadset`, a made-up headset for its session without a person, `-PlayDemoWalk`), `USandboxOverlaySubsystem` (debug overlays, `Sandbox.Overlay`; here because it needs both the character and the hands), `FNaturalMeasures` (how natural the motion is, frame by frame, read only; the runner calls it), automation tests. |
| `Source/SandboxEditor` | Editor-only `GenerateAssets` commandlet; `FPhysicsAssetGenerator` builds physics assets from specs, joints from their anatomy (`ComputeJointFrames`). |
| `Specs/` | Text specs for generated assets (physics assets, room maps with their furniture); format in `Specs/README.md`. |
| `Scenarios/` | Scenario JSON files; format in `Scenarios/README.md`. |
| `Recordings/` | Recordings of a session's hands that scenarios replay (`"recording"` in a scenario file); new ones are written under `Saved\Recordings` and copied here to be kept. |
| `Tools/` | The CLI scripts above. `common.ps1` holds shared helpers (`Invoke-ScenarioBatch` hands scenario runs to a queue that several engine processes empty, for the suite, the push matrix, sweeps and the other tables; a single `scenario.ps1` run builds a queue of one of its own, `New-ScenarioQueue`; a run's own log is `run.log`, the process's `process.log`). |
| `Config/` | Engine and game settings. Physics timing lives in `DefaultEngine.ini`. |
| `Config/Tuning/` | Every tunable number, as JSON: `body.json` (the character), `poses.json` (its poses, as joint angles), `effectors.json` (the hands and the foot), `tools.json` (the desktop tools and the headset); names, units and ways to change them in its `README.md`. |
| `docs/` | `PRD.md` (plan), `fast-checks.md` (faster checks and runs, decided and audited on 27 November 2026: where the time of a run goes, requirements SP-0 to SP-12 in three stages, the owner's decisions, the audit's corrections; stage 1, SP-0 to SP-3, built and accepted on 27 November (decision 10),
section 11: the benchmark, the start without Zen and the platform check, several runs and films in one call, check sets; the owner
decides what comes next), `active-ragdoll.md` (the second architecture of the active ragdoll, proposed on 26 November 2026: the audit, seven causes, what Euphoria and Epic's sample do, the design with its authority, FR-33 to FR-41, parts R1 to R9 and R1's first results; **section 8 is the one order of work of the project**, with where each part of the natural-body track went), `natural-body.md` (the natural-body track, proposed on 25 November 2026: what "natural" means in 26 criteria with human reference figures, and parts N1 to N9 with their design, scenarios, steps and how each ended; N1 is done, N8 A's head is the default body, four of N2's jump repairs are on, the rest is behind switches that are off; its order of work is replaced by the R parts), `testing.md` (repeatability policy, how the journal is kept, and measurements), `pitfalls.md` (what has already bitten, by area), `recon/` (the plans, patches and script the journal cites as `Saved\Recon`, kept since 26 November 2026). |

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
  deaf to a contact. A recorded clip is a target for the muscles and, within a budget, for the authority's springs
  (below); the body is never moved, only pushed, and every state has to say what a hit, a push and a grab do to it.
- **Authority is a rule with budgets** (owner's decision of 26 November 2026; `docs/active-ragdoll.md`, section 5).
  Besides its muscles the body may be pulled toward its reference by capped springs from the world on every body
  (`FSandboxCarry`), never downward, measured in every run. The budget is 0 for a body that stands, flinches,
  staggers, falls or lies; the get-up takes what it needs now and comes down part by part (R7). The balance assist
  on an upright body is the one recorded exception until R4 replaces it with torques of the legs. Any contact from
  outside takes authority away: a hit at once, a press or a hold in proportion.
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

The rest of the list is `docs/pitfalls.md`: 217 entries on 28 November 2026 (238 with the 21 below), grouped by area (the
solver; measuring, runs and tests; muscles, poses and the standing body; falls and the get-up; hands, the grab and blows; the
session, recordings and the headset; PowerShell, scripts and building; assets, the MetaHuman, maps and lighting), each with when
to read it. The entries below are the ones that spoil a result in any part without a sign; they are here only, and every
session has them. **Before a part's plan is
written, read the areas of `docs/pitfalls.md` that its work touches; the plan names them** ("read: solver; falls and the
get-up"). A new pitfall goes into its area there, or here if it can spoil any part without a sign.

- **Toolchain.** Engine 5.8.4 from the Epic launcher; Visual Studio 2026 with MSVC 14.50 and Windows SDK
  10.0.22621. The MSVC 14.51 toolset breaks 5.8. `Tools\common.ps1` finds the engine from the launcher's
  install list, or from `UE_ROOT` if set.
- **Chaos puts a standing character's island to sleep, and the controller's next tick wakes it.** A body
  that holds still is still enough for the solver's sleep thresholds. Under control it then slept one
  substep in every six: no gravity, no contacts and no contact events in that substep. Nothing looked
  wrong; the feet reported five sixths of the body's weight, the balance assist's load read 0.047 of body
  weight instead of 0.0185, and feet at 8 solver iterations rocked twice as fast as they do awake. Found
  only because the floor's push on the feet did not add up to the weight. The controller's bodies are now
  `ESleepType::NeverSleep`. When a sum of forces or impulses is short by a simple fraction, suspect sleep
  (`-CVars "p.Chaos.Solver.Sleep.Enabled=0"` to compare).
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
- **`| Select-Object -First N` stops the script that feeds it.** `suite.ps1 -SaveBaseline x | Select-Object
  -First 15` ended the suite after 15 lines, with exit code 255 and no baseline. Redirect to a file and filter
  that (`*> file; Get-Content file | ...`).
- **The first scenario an engine process runs differs bit for bit from every later one,** and later ones do not
  depend on what ran before them. The job queue (`-ScenarioQueue`, `UScenarioBatchSubsystem`) therefore runs
  `Tools\batch_warmup.json` first in every process, and a single `scenario.ps1` run is a queue of one job; with
  that, 48 scenarios gave the same hashes in one process, in eight and in twelve.
- **Passing every check is not moving like a person.** The suite and the push matrix were green while the body
  stood with its arms held out, fell like a plank and stayed frozen in a lunge. Look at the motion
  (`Tools\film.ps1`: one sheet of pictures over time) before calling a behaviour done.
- **The solver's determinism mode does not make a falling body repeat on a busy machine.** Chaos rebuilds the
  structure it finds colliding pairs with in a background task and takes it in on whichever step the task is
  done by; a pair, and with it a contact, then comes a step earlier or later. With 16 processes side by side
  3 to 18 runs of 100 of a scenario with a fall differed (always the same few other hashes), none in a single
  process, and no standing body ever did. `FScenarioStage::KeepSolverInStep` waits for the rebuild before every
  substep and lets none run during it. A run that differs "once" is a defect to measure, not to rerun: the same
  scenario 160 times side by side tells in a minute.
- **A figure from one seed is luck.** With the clip waiting for a pelvis 12 cm off in any direction, the stand
  gave 48 of 50 on seed 1, and 45 and 45 on seeds 2 and 3: on the criterion's edge. A setting is chosen on three
  seeds (`Tools\getup-stand.ps1 -Seed`), and a single failed fall that changes with a setting is no finding.
- **An engine process takes 1.75 GB.** Fifteen side by side (three stands at once) took 26 GB of 32 and the
  batch was stopped for want of memory. Nine is the most that run at a time (`Tools\getup-seeds.ps1`).
- **A gate's figure is measured at the gate.** "Under 300 N two bodies of three get up" was measured in step 1 of
  part 7 and carried to the gate; step 2 changed the body's friction and nobody ran the pressed stand again: it
  was one of three. A change to the body is followed by every measurement that a written claim rests on, not only
  by the suite.
- **A run in the headset that has to start SteamVR itself rebooted the machine.** On 24 November 2026 the owner
  had closed SteamVR and left; a `-VR` launch 13 minutes later started SteamVR through the OpenXR instance, the
  engine created its D3D12 device while SteamVR's compositor was coming up, and the NVIDIA driver took the system
  down (`nvlddmkm.sys`, bugcheck 0x3B, `Kernel-Power` 41; the engine's log ends between "Emitting draw events
  for PIX profiling" and "Aftermath enabled"). Every uncommitted change was open; none was lost. The scripts now
  refuse a `-VR` run unless `vrserver` and `vrcompositor` are running, the compositor has been up for 30 s and
  SteamVR's own log says ready or standby (`Get-HeadsetBlocker` in `Tools\common.ps1`), and say when it is
  standby (nobody wears the headset). **A headset run is
  started only when the owner has said, in that message, that the headset is on and SteamVR is up; never in
  unattended work.** Before a run that opens the GPU in unattended work, copy what is not committed
  (`Saved\Backups\backup.ps1`). The dump (`C:\Windows\Minidump`) needs an administrator to read.
- **After a night of unwatched changes, have the diff read by someone who did not write it,** and give them the
  code, not the account of it. Two real defects of 41 files came out that way, both in paths no test reaches.
- **Two runs that differ in two things say nothing of either.** "The same kick is seven steps on a body that
  stays upright and a fall on one that folds over it" was written from a run of the night and one of the
  morning; the two feet had other springs (16000 and 24000 N/m). With the reaction switched off on the morning's
  foot the body fell as well. To say what a change does, switch that change alone (`-Tune`).
- **A change to a path that several features share is measured on each of them.** The arm's blend by its joints' turns was made for
  the guarding arm, which it put right (a target that jumped 9 degrees to 0.5); it reached every arm, and the fall's reach, measured
  before it, gained 44.7 J by hand instead of 38.3 and the head struck in twice as many falls. It is the guard's alone now
  (`FSandboxLimbTarget::bJointBlend`). Before calling such a change done, name every caller and run what each was judged by.
- **A part's own limits are not all its limits.** N8 A's list for the head had no C25, and decision 15 bounds C25 for every
  part: the head raised the largest command jump from 429 to 512 N*m and it was seen only when the scorecard ran on the default
  body, after the owner had chosen it. Before a change is offered, read the plan's decided rules that bound a figure and measure those
  too (the scorecard, `Tools\natural.ps1 -Quick`, has them).
- **Look an idea up in `docs/testing.md` by what it does, not by its name, before offering it as new.** "Unload, then step" was put
  to the owner on 26 November as untried ("the toes and the ankle were tried, not a shift of the weight"); the third get-up attempt of
  25 November, "the foot held until it is free", had lifted the foot, made the clip wait and aimed the pelvis at the other foot, and it
  failed as the new one then did. Search for the mechanism's words (foot, lifted, shift, held) and read every section on the subject.
- **Edit source files with the editor, not with a shell one-liner.** A PowerShell helper that was to add a name in five files
  flattened `@(@('old', 'new'))` to two strings, indexed them as characters, and `String.Replace` put a letter in place of every tab
  of two files and doubled the line breaks of three. Taken back from git and made again by hand.
