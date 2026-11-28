# R10, behaviours, protection and commands: the plan from the code

Written 28 November 2026, 04:45, at the end of the owner's autonomous window, after R3 was built (it waits for the owner's look,
decision 58) and stage 2's SP-6 and SP-4 (`docs/fast-checks.md`, section 12). Nothing of it is built. The design is
`docs/active-ragdoll.md`, section 8, "R10" (FR-44 to FR-55 of `docs/PRD.md`; decisions 40 to 48 of section 10; criteria C28 to C40
of `docs/natural-body.md`); this file is how it meets today's code, in which order it is built, what each step is behind and what
checks it. Every step behind a switch of its own at 0 that keeps every hash; values only in `Config/Tuning`; a baseline before each
step and the hashes after it; the stand on three seeds where a draw is made.

**Built on 28 November, 05:15 to 06:30** (`docs/testing.md`, "R10"; `docs/active-ragdoll.md`, decisions 59 to 67): steps 1, 4, 5 and 2,
and step 6 for the head only, every hash kept at 0. Where the build left the plan: the lowering runs the clip back, led, to where the
attempt began (decision 59), not "given up at once and lowered by the muscles"; the held arm is not answered while lowered (decision 63,
until step 3); the stand's `-Near` is built, its `-Drag` is step 7's. The scripts of the part: `docs/recon/r10/make_protect.ps1` (the
scenarios of steps 5 and 6), `docs/recon/r10/grid.ps1` (a few moments of a film pair as one picture).

**Read first** (`docs/pitfalls.md`): "Falls and the get-up", "Hands, the grab and blows", "Muscles, poses and the standing body",
"Measuring, runs and tests", "The session, recordings and the headset" (R10 changes what a recording holds); and the entries of R3's
night on a limb target taken over with a jump, a target let go at a point of the world, and a rule for one caller given to all.

## What exists to build on

- **What the body is told** (`ScenarioStage.cpp`, `FScenarioStage::TellBodyWhatMoves`, line 212; the session does the same): every
  frame the hands, the foot and the props as `FSandboxSeen` (`SandboxBodyController.h`, line 83), the player's capsule left out.
  `ESandboxSeenKind::Viewer` exists and nothing tells it. A hand is told without its grip. Holds come through `TellHolds`
  (`FScenarioStage::TellBodyOfHolds`, line 182), with each hand's bone, force and pull.
- **Perception** (`UpdatePerception`, N5): the gaze, anticipation and the guard (`PlaceArm` with the elbow down, `bEaseTwice`), on in a
  session with `-Perception`, off in the files.
- **The get-up's gates** (R2's rest): `CanBeginGetUp` says what holds a get-up back; `GiveUpGetUp(ESandboxGiveUp, reason)` is the one
  place an attempt is given up, its kinds in `ESandboxGiveUp` (`SandboxBodyController.h`, line 339, "for R10 to add its own reasons
  to"); `UpdateDrag` (decision 18) and `UpdateHeldArms` (decisions 19 and 48); the carry's release over `getup.carry_release_s`.
- **Hits by place:** `OnBodyHit` and `FeelFrame` split what is felt into hit and pressure; the belly's sum of one contact
  (`reactions.double_over_ns`, line 5064) and the head's are counted; the chest's and the legs' are not (decision 45 asks for them).
- **Limbs and their owners:** today each user takes a limb by `SetLimbTarget`/`SetLimbBend` and must look whether another holds it
  (`GetLimbWeight`, R3's catch; `GetLimbGoalWeight`, new in R3): there is no arbiter.
- **Scenario events** (`ScenarioSpec.cpp`): `impulse`, `prop_move`, `pin`, `tone`, `step`, `hand_target`/`foot_target`, `clip_pose`,
  `console`, `edit_tuning_file`, `effector` (with `at_bone` and `follow_bone_s`). No `viewer`, `command` or `seed`.
- **Chance:** none in the body (one `FRandomStream` in a unit test). The stand's `-Seed` draws its falls in PowerShell.

## Steps

1. **Who the player is** (measure only; every hash kept). `FSandboxSeen` gains the hand's grip (open or a fist), told by the stage and
   the session from the effector's target grip. A scenario's `"viewer"` (a place or a path, as an effector's) is told as `Viewer`; a
   session tells the head (the camera on the desktop, the headset's head in VR) and the player's capsule as `Viewer`; a recording keeps
   the head frame by frame, and a recording made before has none and its replay says so. The body reads it only where a switch asks:
   with every switch at 0 nothing reads it (checked by the hashes of the suite and of `record-replay.ps1 -Perception`, whose seen things
   change). Read-only `control.behaviour.viewer_cm` (the nearest viewer's distance a frame). Checks: the suite at 0; a scenario with a
   viewer path whose figure is held to it.
2. **Chance with a seed** (infrastructure; every hash kept). `FSandboxChance`: one `FRandomStream` per purpose from the run's seed and
   the purpose's index, drawn only when a choice is made; a scenario's `"seed"` (0 or none: no draw, each choice its fixed answer); a
   session's seed from a counter of its own, written into its log and its recording; `getup-stand.ps1 -Seed` hands its seed to every
   trial. Unit tests: the same seed draws the same, a purpose's draws do not move when another purpose draws.
3. **The arbiter of limbs** (every hash kept). `FSandboxLimbClaims` in the controller: the legs, the trunk, each arm, the head and neck;
   each user asks for a limb with its priority (1 the fall, R3; 2 balance and steps; 3 protection; 4 the guard; 5 the answer to a hold;
   6 a command; 7 the disposition; 8 idle life) and is told whether it holds it; a user that loses a limb lets it go over the pose's
   blend. First built as a record only: the users that exist (R3's catch, N6's aim, the guard, R2's held arm, the steps' feet) tell it
   what they hold, and `control.behaviour.limbs` says who held what each frame; then the new behaviours of steps 5 to 8 ask through it,
   and an existing user moves in only where a new one meets it (the guard and the cover; the held arm and the struggle), each move
   measured on its own callers (`CLAUDE.md`: a path several features share).
4. **Fear** (`behaviour.fear` 0 off; read only by the choice). One number: up with each hit from something that moves, by its
   impulse (`behaviour.fear_per_ns`), and with each threat N5 sees; down over `behaviour.fear_fade_s` from the last. `control.behaviour.fear`
   a frame. Checks: the suite at 0; scenarios whose fear is held to figures.
5. **Staying down and lowering** (FR-48, 49, 51; C33; `behaviour.stay_down` 0 off). In `CanBeginGetUp`: afraid
   (`behaviour.fear_stay_down`) and a viewer within `behaviour.near_cm` holds the get-up back, and it begins again past `near_cm` and
   `behaviour.near_margin_cm`, or with its fear down, after `getup.rest_s`. A get-up under way is given up after `perception.react_s`
   with a new kind, `ESandboxGiveUp::Afraid`; the carry goes over `getup.carry_release_s` and the muscles lower the body into the
   protective pose (lowered, not dropped: the pelvis's and the head's downward speed under C16's 2.0 m/s). Scenarios
   `protect_near_lying`, `protect_near_rising_sit`, `_knees`, `_rise`; the stand gains `-Near <cm>`.
6. **The protective poses and the cover after a hit** (C34; `behaviour.protect` 0 off). The sample's `M_ragdoll_poses_injured` and
   `_OnGround` looked at frame by frame first (`Tools\clip-film.ps1`), each used where it fits; the rest written as joint angles in
   `poses.json` (`protect_head`, `protect_trunk`, `protect_groin`, `protect_leg`). A hit at or above `behaviour.protect_from_ns` for its
   place while lying or rising (a sum of one contact for the chest and the legs added beside the belly's and the head's) gives an attempt
   up (`ESandboxGiveUp::Protect`) and covers the place for `behaviour.protect_hold_s`; the body's own landing is no such hit (the static
   world told apart, as R3's touches are). Scenarios `protect_hit_lying_head`, `_trunk`, `_groin`, `_legs`, `protect_hit_rising_*`. A
   clip pack, if the sample has none that fits, is the owner's decision with its price and licence (section 8).
7. **The struggle** (FR-50; C35; `behaviour.struggle` 0 off). First a measure, the risk of section 8: can the muscles bring a grip to
   its 450 N for 0.1 s or 45 cm for 0.25 s (`grab-pulls.ps1` held the other way: the body pulls, the hand holds). Then, drawn once a drag
   (`behaviour.struggle_chance`), attempts every `behaviour.struggle_every_s` with a vigour drawn from `behaviour.struggle_vigour_min` to 1:
   the held limb pulled in (R2's held arm's pull, decision 19), the free legs pushing on the floor, a twist away, tone braced by the
   vigour. The body never opens a grip (`CLAUDE.md`, the modules). The stand gains `-Drag 2` (two hands drag it 2 m by the ankles or
   the wrists; each drag run again with the struggle off). Scenarios `drag_lying_ankles_struggles`, `_wrists_struggles` on three seeds,
   `drag_rising_wrists_falls`, `escape_then_waits`.
8. **Commands** (FR-51, 52; C39). `TellCommand` (`get_up`, `give_hand`, `stop`) told by the session or the run; a scenario's
   `command` event; a recording keeps the commands with their frames; the controller answers accepted, done, refused (held, pressed,
   falling, unable) or interrupted, in `control.commands` and its events. Keys on the desktop (listed on the screen), the controllers'
   face buttons in the headset, the console (decision 42). `get_up` sets the disposition aside, is interrupted by a hit of
   `protect_from_ns` and goes on up to `behaviour.command_tries`; `give_hand` reaches with N6's hand target for the player's nearest
   open hand within reach for `behaviour.offer_s`, and a pull along the clip's way does not slow the clip (decision 47). Scenarios
   `command_get_up_near`, `command_get_up_held`, `command_give_hand_lying`, `command_refused_held`.
9. **FR-18 in every new phase** (C26): a hit, a push and a hold in the protective pose, the abandoned rise, the struggle and the
   offered hand (`protect_phase_*`, `struggle_phase_*`, `offer_phase_*`), as the get-up's phases have.
10. **The gate:** C33 to C35 and C39 on three seeds where a draw is made; the push matrix, the knockdowns and the stand as section 7
    says (with the switches at 0 every hash; with them on, the scenarios with a viewer, a drag or a command and those with a hit at or
    above `protect_from_ns` while getting up move, each named); C25 by the scorecard; videos before and after for every switch to be
    switched on (`film-pair.ps1`), the owner's look, then the headset at M4's gate.

**For the owner, before or during the part:** none needed to begin (decisions 40 to 48 are taken); a clip pack for the protective
poses only if the sample's do not fit (step 6). Recommended model: Opus 5.5, high; about three to four nights, step 3 the largest.
