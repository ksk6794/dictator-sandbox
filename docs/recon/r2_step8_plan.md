# R2, step 8: the motion library and the matcher

Written 27 November 2026, before any code, for the owner's look. R2's own text (`docs/active-ragdoll.md`, R2, "`FSandboxMotionLibrary`"
and "`FSandboxMotionMatcher`") is the brief; this is how it is to be built, checked and judged. Behind a switch at 0, every hash kept at 0.
Steps 9 (the roll first) and 10 (the reference fitted to the floor and to the surface under the body) come after it, each a part of its own.

Read before it (`docs/pitfalls.md`): Falls and the get-up; Solver, Chaos and Physics Control; Measuring, runs and tests; Muscles, poses
and the standing body; and the 21 entries of `CLAUDE.md`. The entries this step leans on:

- *A clip made for another skeleton cannot be copied bone by bone*: the mirror is made on the clip's own skeleton, before the clip is
  carried to this body, so that everything after it (the carry, the alignment of the limbs, the joints held to their ranges, the reached
  chain) is the code that exists.
- *A check that a thing is its own inverse does not say it is right* (the pose database mirrored without changing sides): the mirror's
  unit test uses made-up data whose left and right differ, and is broken on purpose (no swap of sides) to see it fail.
- *An angle averaged across its seam points the other way*: the frame's turn comes from `Atan2` of weighted sums, never from averaged
  angles; a unit test turns a made-up body through 180 degrees.
- *The same clip from the same place fails the same way*: a second attempt is pushed away from the variant just given up.
- *The sample's clips log load errors that are not failures; an automation test must not load one*: the matcher is pure arithmetic on
  arrays of points, tested on made-up candidates; no test loads Epic's clips.
- *A change to a path that several features share is measured on each of them*: `FindOrReadClip` is shared by the get-up, the fall's
  blend (`fall.blend`), a scenario's `clip` event (`SetClipPose`, `Tools\clip-film.ps1`) and the pose database (`ReadClip`); a plain
  path must read exactly as today.
- *A figure from one seed is luck*; *A gate's figure is measured at the gate*; *A part's own limits are not all its limits* (C25 and
  decision 15's authority, measured with the switch on); *Measuring changes nothing* (the new start measures at 0 keep every hash).

## What it has to move, and what it will not

From `docs/testing.md`, "R2's gate today" and step 7: of the stand's 18 attempts given up, ten lost the pelvis, eight of them in the first
quarter second at the clip's time 0, six on the clip from the front: "the clip's first pose and where it is laid were too far from how the
body lay". Today the clip is chosen by one number (the pelvis's up direction against each clip's first pelvis), always started at time 0,
and laid with its pelvis over the body's pelvis, turned by the pelvis's own twist about the vertical. Where the arms and the legs lie plays
no part, and neither does which leg the recording moves first.

The matcher is to move: the pelvis lost at the start on the bare floor; the jump from the lying pose to the clip's (C25, C19's held poses at
the start); and, with the mirrors, a body whose legs lie the other way from the recording's.

It will not move, and the plan does not pretend it will:

- **C27 on the sides** (57 to 63 degrees in the recordings themselves): step 9 (the roll), decision 38.
- **The table** (37% of attempts given up there, 9 of the 10 pelvis-lost attempts on or across the furniture, the clip laid on the floor
  70 to 98 cm under the body): step 10. In this step the frame's height stays `LastSupportHeightCm`.
- **A foot hanging 0.7 cm over the floor at the clip's end** (C19's poses held over 1.0 s): step 10.

So the gate (C19, C27) is not expected to close in step 8.

## Facts the plan rests on (read in the code and on disk)

- **The clips on disk** (`Content/Characters/UEFN_Mannequin/Animations/Ragdoll`, fetched, not committed): **sixteen get-ups**
  (`getup_{stand,crouch,walk,run}_{B,F,L,R}`), not twenty as `docs/active-ragdoll.md` says in three places (put right with this step),
  two rolls (`getup_roll_L`, `_R`), three ground poses, three flails, nine reaches. `docs/testing.md`, M3 part 5: the stand clips are
  5.0 s and stay in place (the pelvis from 12 to 16 cm to 92 cm); the crouch, walk and run clips travel 5 to 8.7 m in 3.0 to 4.5 s; the
  rolls 3.7 s. `getup.end_s` (2.6 s) is where the stand clips stand.
- **`StartGetUp`** (`SandboxBodyController.cpp`): four paths `getup.clip_*`; likeness = the dot product of the world's up in the pelvis's
  space, body against clip at time 0; after an attempt given up a first pass skips that clip and any with likeness below 0. Frame: the
  twist about the vertical of (pelvis now) * (clip's first pelvis)^-1, the clip's pelvis over the body's, height `LastSupportHeightCm`.
  Time 0 everywhere (`HoldClip(BestPath, 0.0, ...)`, `PelvisAt(0.0)`, `ClipHold.FromPelvis`, the old assist's pelvis target).
- **`FSandboxClipPose::Load`** samples the clip's skeleton in its component space key by key (`SampleKey`), carries each body's turn from
  the source's reference to this body's, aligns arms and legs, clamps to the joints' ranges and chains the reached bodies on this body's
  proportions. One place, `Space[...]` inside `SampleKey`, is where a mirror can be put without touching anything after it.
- **`FSandboxPosePrior`** mirrors poses at the level of joint turns, with no places; it is not a whole-body track and is not reused.
- **Searched in `docs/testing.md` by mechanism** (mirror, likeness, nearest, first pose, "chosen by how"): no choice of a get-up by the
  whole pose and no mirrored clip was ever built or measured. N7's design point 5 ("getting ready": the pose held against each clip's first
  pose, `docs/natural-body.md`) was written and never built.

## Design

### The switch and the names

`getup.match` (0 or 1). At 0 `StartGetUp` runs as today and the library is never read: nothing new is loaded, made or measured but the
three read-only start measures below. At 1 the clip, the time in it and the frame come from the matcher, for every attempt, led or not.
The four `getup.clip_*` stay while the switch is 0 in the files; they go with the old path, after R2 is accepted and on the owner's word.

New values (`Config/Tuning/body.json`, each named in `VisitValues`; no default in code):

| Name | Proposed | What |
| --- | --- | --- |
| `getup.match` | 0 | The switch |
| `motion.library` | `Specs/motion_library.json` | The library's list, relative to the project; a tuning directory can name another (the comparison without the side clips, below) |
| `motion.contact_cm` | 3 | A body of the clip touches its floor when its lowest point is this close to it... |
| `motion.contact_mps` | 0.3 | ...and it moves slower than this |
| `getup.match_window_s` | 1.0 | The part of each get-up the lying body is held against: its first second |
| `getup.match_pelvis`, `_trunk`, `_hands`, `_elbows`, `_feet`, `_knees` | 3, 1, 1, 0.5, 1, 1 | Weights of the places compared |
| `getup.match_up_cm_per_deg` | 1.0 | A degree of the pelvis's or the chest's lean counted as this many cm (a degree at the pelvis moves the head about 1 cm) |
| `getup.match_given_up_cm` | 20 | Added to the cost of the variant whose attempt was just given up |

### `FSandboxClipPose`: a mirrored reading

`Load` takes one more argument, `bMirror`. The sagittal plane of the clip's skeleton is found from its reference pose (the principal axis
nearest to `thigh_l - thigh_r`); a skeleton whose `_l` bones are not the mirror images of their `_r` counterparts within 0.5 cm is refused
with the worst bone named. In `SampleKey`, a mirrored reading gives each bone X the transform of its counterpart C (`_l` and `_r` swapped,
the others themselves), reflected by S:

- place `S * p_C`;
- turn `S (R_C * R_C,ref^-1) S * R_X,ref` (the counterpart's turn from its reference, reflected, laid on X's own reference): a proper
  rotation, X's reference pose at the reference, and independent of how the two sides' bone axes happen to lie.

Everything after it is unchanged. The controller's clip map holds a mirrored variant under the path with a mark (`<path>|mirror`); a path
without it reads as today, for every caller of `FindOrReadClip`. A scenario's `clip` event and `Tools\clip-film.ps1` can then show a mirror
(`-Clip "M_ragdoll_getup_stand_B|mirror"`) beside its clip, which is how the mirror is looked at.

### `FSandboxMotionLibrary` (`SandboxBody`)

Reads `motion.library` at a body's first get-up with the switch on, and keeps per controller:

```json
{
  "comment": "...",
  "folder": "/Game/Characters/UEFN_Mannequin/Animations/Ragdoll",
  "features": { "pelvis": ["pelvis"], "trunk": ["spine_04", "head"], "hands": ["hand_l", "hand_r"], "elbows": ["lowerarm_l", "lowerarm_r"],
                "feet": ["foot_l", "foot_r"], "knees": ["calf_l", "calf_r"] },
  "clips": [
    { "name": "M_ragdoll_getup_stand_B", "kind": "getup", "lies": "back", "mirror": true, "match": true },
    { "name": "M_ragdoll_getup_crouch_B", "kind": "getup", "lies": "back", "mirror": true, "match": false },
    { "name": "M_ragdoll_getup_roll_L", "kind": "roll", "lies": "side", "mirror": true, "match": false }
  ]
}
```

(the bones of `features` are the rig's: pelvis, chest, the head's body, elbows as the forearms' origins, knees as the calves'; checked
against the mesh when read). For every clip and its mirror, at every key: the reached place of each feature bone in the clip's root space,
the pelvis's and the chest's up directions, and **contact marks**: each of the feet, hands, knees, elbows, the pelvis and the trunk
touches the clip's floor when the lowest point of its collision shapes (the physics asset's boxes, capsules and spheres, laid on the
reached pose, as `SupportPolygon` takes the soles) is within `motion.contact_cm` of the floor and moves slower than `motion.contact_mps`.
In this step the marks are reported, not used: how high over or under the floor each contact lies on this body is step 10's input. What
the library reports once per run (`control.motion_library`): every variant's keys and length; its pelvis's travel over the first second;
the time its pelvis first stands within 5 cm of its final height on two feet in contact; its contact intervals with their heights; the
mirror's error (its reached bodies against the original's counterparts reflected by this mesh's own sagittal plane, worst cm over the
clip); and how far `stand_L` mirrored lies from `stand_R` over the first second (whether the sample's two sides are one recording).

Nothing of Epic's is written to disk; the library is made in memory at first use, as clips are today.

### `FSandboxMotionMatcher` (`SandboxBody`, pure arithmetic)

Input: the lying body's feature places (bodies' world transforms, read only) and up directions, the support height, and the candidates'
samples (variant, time, places, ups) over `getup.match_window_s`, every key. For each sample:

1. Turn the clip's places by `StartSpace`'s rotation (as the frame is built today).
2. Fit a turn about the vertical and a shift over the floor that lay the sample's places on the body's, weighted (2D Procrustes: the
   centroids, then the turn as `Atan2` of the weighted cross and dot sums). The height is not fitted: the clip's floor is put at the
   support height.
3. Cost = the weighted RMS distance of the places in 3D (cm) + `match_up_cm_per_deg` x the mean of the pelvis's and the chest's lean
   difference (degrees; a lean does not depend on the turn about the vertical, so back against front is never left to the places), + the
   given-up penalty for the variant just given up.

Output: the variant, its time, the frame (turn and shift; height = support), the cost and its parts, and the next best variant with its
cost (how close the choice was). Candidates are the variants with `"match": true`; every other variant of the library is scored too and
only reported (`best_any`), so that what the other twelve get-ups and the rolls would have given is measured before anyone decides to use
them. Ties go to the first variant in the list's order (deterministic).

The weighted pelvis (3 against 1) is what keeps the fit from laying the clip's pelvis away from the body's: today's rule puts them on top of
each other, and a pelvis 40 cm off gives an attempt up. If the start measures below show the fit putting the pelvis off, the fall-back is the
pelvis fixed over the pelvis and only the turn fitted: a measured variant, not a guess.

### `StartGetUp` with the matcher

With the switch on: the library is read (first time only); the matcher chooses; `HoldClip(variant, time)`, the pelvis's first transform,
`ClipHold.FromPelvis`, the old assist's pelvis target and the start blend all take the chosen time instead of 0; `GetUpRun.Frame` is the
matcher's frame. The log line says the variant, the time, the cost and the next best. Everything after the start (the led clip, the hold, the
hit, the press, the end at `getup.end_s`, giving up) is unchanged: a candidate is a stand clip, so `end_s` holds for it (the decision below).
A clip started at 0.6 s reaches its end 0.6 s sooner: C21 (2.6 to 4.2 s, today 3.20 s, the clip's own length) is watched for its lower limit.

### What a run reports

- **At both settings, read only:** for every attempt, `start_pelvis_off_cm` (the clip's reached pelvis at its start against the body's),
  `start_off_cm` (the weighted RMS distance of the feature places, the matcher's own measure, worked out for whatever clip and frame were
  used) and `start_pose_deg` (the mean angle over the joints between the body's own pose and the clip's at its start time). These are what
  the matcher should lower, comparable between 0 and 1 trial by trial.
- **At 1:** `control.get_up.match` for each attempt (variant, mirrored, time, cost, parts, next best, best of all variants), and
  `control.motion_library` once.
- `Tools\getup-stand.ps1` keeps the new fields per trial; `Tools\getup-seeds.ps1` groups a mirrored variant with its base in the gate's
  "by clip" (decision 21 names four clips) and adds a line: the start (pelvis off and RMS in the median and the worst, the pose's angle), the
  attempts that lost the pelvis in their first 0.25 s, and the count of falls by variant chosen; the table "which clip each of 150 falls took
  and how each ended" (section 9) is written next to the seeds' results.

## Scenarios

| Scenario | Asks |
| --- | --- |
| `getup_match_library` | Laid on the back, the switch on at 0 s (`console`): the library holds 36 variants (16 + 2, each mirrored), every mirror's error within 1 cm, the stand clips' first second has the trunk in contact; it stands at the first attempt; `tuning_changes` 1 |
| `getup_match_laid_front` | The front (where the stand lost the pelvis at the start): one attempt, standing, `start_pelvis_off_cm` at most 10 |
| `getup_match_laid_back_right` | Between two clips: the same |
| `getup_match_laid_left` | A side: the same, and C27 only reported |
| `getup_match_beside_table_front` | By the table: the same |

New expectations for the runner: `min_motion_variants`, `max_motion_mirror_error_cm`, `max_start_pelvis_off_cm`. Each is broken on purpose
once (a limit set below what the run gives) to see it fail. The four `docs/recon/r2_step7_furniture` files are run with the switch on and
reported, not added (step 10's).

## Unit tests (54 today, 58 after)

- `DictatorSandbox.Body.Motion.Mirror`: a made-up skeleton whose left and right poses differ; the mirrored turn of `_l` equals the reflected
  turn of `_r`; a turn about the mirror's normal is kept, about the other two axes reversed; mirrored twice is itself; **and a mirror without
  the swap of sides fails it** (run once so, on purpose).
- `DictatorSandbox.Body.Motion.Fit`: made-up places turned by a known angle (among them 179 and -179 degrees) and shifted are found again
  to 0.01 degree and 0.01 cm; weights that pin the pelvis.
- `DictatorSandbox.Body.Motion.Matcher`: three made-up candidates; a body laid as one of them at a time in its window, turned and moved,
  is matched to it, that time and that frame; the given-up penalty moves the choice to the next.
- `DictatorSandbox.Body.Motion.Contacts`: a made-up track that comes down to its floor and leaves it is marked on the right keys.

## Checks and measurements, in order

1. `build.ps1`, `test.ps1` (58).
2. The suite against `start`: 149 hashes kept to the bit (the switch at 0, the start measures read only), the five new scenarios pass.
3. The suite with `-Tune "getup.match=1" -Label match_on`: which scenarios meet their expectations, which hashes move (every get-up's,
   expected), each failure explained; the furniture files with it.
4. The library's report, from `getup_match_library`'s result, in `docs/testing.md`: per variant, its first second, when it stands, its
   contacts and their heights over the floor, the mirror's error, whether `stand_L` is `stand_R` mirrored.
5. The stand on three seeds with the switch on (`getup-seeds.ps1 -Tune "getup.match=1"`): plain, `-HoldHands 1`, `-HoldHands 2`,
   `-PressN 300`; against `start_on_*` and `final_all_*`: standing, first attempts, attempts given up (and how many lost the pelvis at the
   start), C19, C21, C27 by clip, authority (decision 15: 1.16 or a written reason), the start measures 0 against 1 trial by trial.
6. Variants of the matcher, each on three seeds, chosen by figures and not by one seed: `match_window_s` 0 (time 0 only, today's start
   with the better clip and frame) against 1.0; the pelvis fixed over the pelvis against fitted; `match_up_cm_per_deg` 0.5 and 2. And the
   library without the side clips (a tuning directory naming a second list): decision 38's measurement repeated with mirrors and the
   matcher, as step 9's starting point.
7. Decision 7 with the switch on: the push matrix against `walk`, the knockdowns and `-ByHand` (no get-up in them: expected the same;
   run to know, not to assume).
8. The scorecard, `natural.ps1 -Quick -Tune "getup.match=1"`: C2, C19, C21, C25 (the largest command jump; decision 15 bounds it).
9. Films, 0 and 1 side by side (`film-pair.ps1`, the backup first): the stand's front fall that lost its pelvis at the start, a fall whose
   choice is a mirror, a body half on its side, `getup_laid_front`; and `clip-film.ps1` of `stand_B` beside `stand_B|mirror`. A page with
   the players for the owner, as for the night's videos.
10. The diff read by an independent reviewer (the code, not this account), what it finds put right, then `bootstrap.ps1`.
11. `docs/testing.md` ("R2, step 8"), `docs/active-ragdoll.md` (R2, section 10's decision 39, "twenty" put right), `docs/PRD.md` Status,
    `Specs/README.md` (the library's format), `Config/Tuning/README.md`, `Scenarios/README.md`, the plan's tabs in Claude Docs.

## Risks

| Risk | What is done about it |
| --- | --- |
| A mirror that is wrong in a way its own check cannot see | The unit test on asymmetric data broken on purpose; the load's check against the reflected reached bodies on this mesh; the clip and its mirror filmed side by side |
| The clip's skeleton is not symmetric about an axis plane | Found from the reference pose and refused with the worst bone if not within 0.5 cm |
| The fit lays the clip's pelvis away from the body's, and the attempt is lost | The pelvis weighted 3; `start_pelvis_off_cm` in every run; the variant with the pelvis fixed is measured |
| A clip started late makes the get-up shorter than C21's 2.6 s | `match_window_s` 0 against 1.0 measured; C21 on the gate's line |
| Near ties between variants flip with any change, and the stand's trials move for nothing | The next best's cost is reported; every figure on three seeds; a single trial that changes is no finding |
| The second attempt's rule changes (a penalty instead of "another clip if one starts at all like it lies") | Attempts given up and second attempts read on the stand; the penalty is a value |
| A plain path reads otherwise for one of `FindOrReadClip`'s four callers | The suite at 0 keeps every hash; `pose-prior.ps1` says the database is current; `fall.blend=1` run on the knockdowns as before |
| Reading 36 variants at a body's first get-up slows the stand | The stand's wall time against today's 41 s; no clock is read in the simulation |
| A body lying on furniture is matched with heights from the floor | Known, unchanged in this step: the choice is still made, the frame's height is step 10's |
| More attempts given up or fewer first attempts with the switch on | Decision 7: written with its reason and a video, or not offered |

## The decision for the owner (39), before the code

**Decided by the owner on 27 November 2026: (a), the four stand clips and their mirrors.** Recorded in section 10 of
`docs/active-ragdoll.md` with the step.

Which get-ups may the matcher choose in this step?

- **(a) Recommended: the four stand clips and their mirrors** (eight variants); the other twelve get-ups and the two rolls read, mirrored and
  scored for the report only. The stand clips are the ones that end standing in place, where `getup.end_s`, C21 and the stand's end are
  measured; the crouch clips end in a crouch and the walk and run clips walk 5 to 8.7 m away, so each would need an end of its own, a change
  to be measured once the report shows whether their first seconds lie any differently.
- (b) All sixteen with their mirrors, each ended where its recording first stands (found by the library), the walk and the run cut there.
- (c) The back and front stand clips with their mirrors only, a body on its side rolled to them by the world (decision 38's measurement:
  C27 139 of 150, 15.6% of attempts given up), until step 9's roll. Measured in this step anyway as a variant (item 6).

## Not in this step

The roll first (step 9); the reference fitted to the floor and to the surface under the body, the contact marks used, the carry's damping
downward (decision 30) (step 10); the drag by the ankles and the wrists (decision 18); the arms of FR-43 (decision 19); the fingers `flat`
(decision 8); the ground poses (R6). The old clip-following path and `getup.clip_*` stay until R2 is accepted.
