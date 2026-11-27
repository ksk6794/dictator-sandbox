# R2, the rest: the body on its side, the fit to the floor, the drag, the held arm, the fingers, the gate

Written 27 November 2026, before any code, for the owner's look. R2's own text (`docs/active-ragdoll.md`, section 8, R2) and
`docs/recon/r2_step8_plan.md`, "Not in this step", are the brief; this is how the rest is to be built, checked and judged. Three
decisions come first, one at a time (section 10 of `docs/active-ragdoll.md`, 49 to 51, open until the owner answers): whether the
matcher is switched on, what replaces step 9's roll, and the start's window against C21. No code before them. Every new behaviour is
behind a switch at 0 that keeps every hash; each switch is measured alone before any two together.

Read before it (`docs/pitfalls.md`): Falls and the get-up; Measuring, runs and tests; Hands, the grab and blows; Solver, Chaos and
Physics Control; and the 21 entries of `CLAUDE.md`. The entries this plan leans on:

- *A clip's name is not what it begins with* (the sample's rolls are a dive's, step 8): any clip a step rests on is read first, its
  first frames and its contacts, before the step is planned on it.
- *An angle averaged across its seam points the other way*: the roll's way and the surface's turn are made from `Atan2` of sums.
- *A reference made for another skeleton lies lower than the floor lets this body lie*; *"Never pushes down" made for a spring's place
  still pushes down through its damping*; and decision 30's trial (step 5: the damping's downward part taken off everywhere threw bodies
  off the floor, 18.5% of attempts given up): the fit to the floor takes the vertical pull off only where the body touches.
- *A pull from the world that gets the body up gets it up against a hand too*; *Half of a stiff spring still takes a push back*: the
  drag and the held arm are measured by what the body and the hands do, not by a share.
- *A scripted hand goes to a point of the world and slides off a body that moves*; *A scripted hand can be sent to a part of the body*
  (`at_bone`): the drags and the pulled arms use `at_bone`, and a phase a hand cannot catch is left out and said.
- *A scenario that starts with a fall is written for where that fall puts the body*: the new scenarios lay the body (`body.laid_deg`)
  where they can; what something does to a get-up is asked of the stand on three seeds.
- *A get-up that is given up moves the body*; *The same clip from the same place fails the same way*; *A count that starts again
  whenever there is progress does not bound a wait*: every new wait is bounded by a total.
- *A figure taken at a threshold is as steady as the motion is where it crosses it* (C27's 70 cm on the side clips).
- *The engine caps a joint drive's force axis by axis*; *A drive's target is measured in the frame of the joint's second body*: the
  vertical spring of a body in contact goes by the control's own per-axis multiplier, not by moving its target.
- From `CLAUDE.md`: *A figure from one seed is luck*; *A gate's figure is measured at the gate*; *A part's own limits are not all its
  limits* (C25 and decision 15's authority with every switch on); *A change to a path that several features share is measured on each
  of them*; *Two runs that differ in two things say nothing of either*; *Passing every check is not moving like a person*.

## Where R2 stands (27 November, the files' body: the hold, the hit and the start on, the matcher off)

| | The files (`getup.match` 0) | Matcher on (four stand clips and mirrors) | Matcher on, back and front only (decision 38's measure) | R2's gate (decisions 16, 21, 15) |
| --- | --- | --- | --- | --- |
| Standing; at the first attempt (three seeds) | 150; 140 | 150; 142 | 150; 143 | 95%; 135 or more |
| Attempts given up | 10.2% | 9.1% | 8.0% | at most 10% |
| Poses held over 1.0 s | 5 | 8 | 2 | none (C19: no pose held longer than 1.0 s) |
| C27 both limits; trunk, farther foot (median) | 67; 58.1 deg, 25.0 cm | 90; 29.7 deg, 24.8 cm | 150; 23.1 deg, 24.8 cm | median 45 deg and 30 cm, and by each clip |
| C27 by clip (B, F, L, R) | 30, 22, 63, 61 deg | 30, 22, 77, 61 deg | 30, 22, -, - | each 45 deg |
| C21, seed medians | 3.20 s | 2.60, 2.97, 2.93 s | 2.53, 2.57, 2.53 s | 2.6 to 4.2 s |
| Authority, mean | 1.16 | 1.18 | 1.27 | 1.16 or a reason written |
| One hand pulled free; two; pressed 300 N stood | 31; 0; 56 | 39; 0; 49 | not run | 20 to 60; at most 5; reported |

What is left to close: C27 on the sides (the recordings' own 57 to 63 degrees), every pose held over 1.0 s (all at a wall, the table or
the chair: a side clip's end waiting for a body against the furniture, or the clip laid on the floor under a body on the table), C21's
lower limit once the matcher starts the clip late, and the authority.

## Facts the plan rests on (read in the code on 27 November)

- **One place begins a get-up:** the Lying state, once `LyingS` reaches `GetUpRestS()` (`UpdateState`, `SandboxBodyController.cpp`
  about line 4504), calls `StartGetUp`. **One place gives one up:** `GiveUpGetUp(Reason)`: the reason as text, `GivenUpClip`, the clip
  released, state Falling; the carry's share then goes down over `getup.carry_release_s`, except the old hit path, which sets it to 0.
  R10 will add reasons (the player near, fear, a command) and a condition on beginning; both places are made to take them (step 11).
- **The clip's floor is `LastSupportHeightCm`**, the floor the body last stood on: the matcher's `SupportZ` (`MatchBody`), the frame's
  height (`FSandboxMotionMatcher::Frame`), the rule's frame (`ChooseByRule`), C27's 70 cm (`MeasureGetUpShape`). A body on the table has
  its clip laid 70 to 98 cm under it.
- **The carry** (`FSandboxCarry`): one Physics Control per body; the target raised to the body's own height when the body is above it
  (`RaiseCm`), so the spring never pulls down; the damping does (decision 30). The vertical cap is a per-axis multiplier already
  (`MaxForceMultiplier.Z`); the strength and the damping are one number a body today.
- **The motion library** marks, for every key of every variant, which of the 11 compared bodies touch the clip's floor
  (`FSandboxMotionVariant::Touches`, `Contacts` with their lowest points over the floor, `motion.contact_cm` 3, `motion.contact_mps`
  0.3), and reports it; nothing uses it yet.
- **The end of a clip** stands until the body is upright (`getup.upright_deg` 15) with both feet loaded, given up after `getup.stall_s`
  (3 s): seed 2's trial 38, the right foot 0.7 cm over the floor, held there by its clip pose for the whole 3 s.
- **A hold** is told as holder, bone, force, pull and point (`FSandboxBodyHold`). With `getup.hold_by_body` the held chain loses its
  springs, every body loses them in proportion up to `hold_rest_full_n` (450 N), and the clip's pace goes to 0 at `hold_force_n`
  (450 N), which then stands for `stall_s` before the attempt is given up ("held down").
- **The fingers** (`UpdateFingers`): `flat` under a hand the world pushes on in any state but Lying, the get-up included; all four poses
  by one weight, `poses.fingers.weight` 0; the bodies' hashes are the same at 0 and 1 (no bodies; the bones' hashes move).
- **An arm can be placed** (`PlaceArm`, `FSandboxLimbTarget`), and a muscle chain can be scaled (`MuscleScale`); the player's controller
  already vibrates by the force its hand feels (`ASandboxPawn`, `FSandboxXRControllers::HapticAmplitude`).
- **A scene query in the controller kept every hash** (R3's `SenseFall` sweeps along the fall).

## The three decisions first (section 10, 49 to 51)

**Decided by the owner on 27 November 2026, each as recommended: 49 (a), 50 (a), 51 (a).** Recorded in section 10 of
`docs/active-ragdoll.md`. The text below is as it was put to the owner.

1. **49, the matcher.** Recommended: on now (`getup.match` 1), `getup_held`'s hands to `at_bone`, a new baseline; every later step is
   built on it, and its 1.18 of authority is the fit's to bring down or to explain at the gate. Otherwise the rest is built under
   `-Tune "getup.match=1"` and the baseline measures a body nobody keeps.
2. **50, the body on its side** (step 9's roll cannot be built on the sample's rolls). The options and the design of each are below
   (step 9); recommended: the body rolls itself by its muscles, at an authority of 0, with the world's roll (the back and front clips
   only) measured beside it as the fallback.
3. **51, the start's window against C21.** A clip started 0.67 s in ends 0.67 s sooner (the ease's 0.6 s and 2.6 less the start).
   Recommended: the window stays 1 s and C21 is judged at the gate, the roll's time counted from the first move; only if the median is
   then under 2.6 s, a price on a late start, swept on three seeds.

## Step 9 (replaced): the body on its side

Whichever the owner chooses, the side clips (`stand_L`, `stand_R` and mirrors) stop being candidates while its switch is on, so C27 is
judged on the back and front clips (decision 38), and a get-up's C21 counts from its first move.

### (a) The roll by the body's own muscles (recommended; `getup.roll` 0 or 1)

- **When:** a body that begins an attempt lying on its side, its pelvis's left-right axis within `getup.roll_side_deg` (proposed 45) of
  the vertical. Otherwise as today.
- **What:** the attempt (state GetUp, so C21 and the attempt's count start here) begins with a phase `roll`: no clip and no carry, an
  authority of 0 (section 5: lying, 0); the muscles are asked for a written pose, as `poses.double_over` is: `poses.roll_to_back`
  (the upper shoulder and hip go back, the upper arm reaches back, the trunk twists toward the back) or `poses.roll_to_front` (the upper
  knee comes over the lower leg, the upper arm across the chest to the floor, the trunk twists toward the front), joint angles in
  `Config/Tuning/poses.json`, written for a body lying on its left and mirrored for the right by the pose rig (left and right
  swapped, a unit test on asymmetric data broken on purpose without the swap). Blended in over `poses.blend_s`.
- **Which way:** `getup.roll_way` (0 by the trunk's lean: facing down rolls to the front, up to the back; 1 always the front; 2 always
  the back), chosen on three seeds; the front is the way most people get up from a side and the front clip had the best C27.
- **Done:** once the pelvis's left-right axis is within `getup.roll_done_deg` (proposed 30) of level, or after `getup.roll_s` (proposed
  1.5 s) whatever it reached; then the matcher, among the back and front clips and their mirrors, starts the clip phase from the body
  as it lies, by the start blend as today. A roll that did not turn the body is counted, and the clip phase begins anyway (the world then
  turns it, as in (b)).
- **Touched in the roll** (FR-18, C26): a hit or a push moves the body (no authority to take away); a hold of a drag gives the attempt
  up (step 11); a press slows nothing (no clip) and is bounded by `roll_s`. Three scenarios, `getup_roll_push`, `_hit`, `_hold`.
- **Measured:** `control.get_up.rolls` (way, time, the turn reached, done or not); the stand's side falls (about half of 150) by
  first attempts, attempts given up, C21 and the authority.
- **Why recommended:** a person on their side throws the upper arm and knee over and rolls; physics decides whether it turns, at no
  cost of authority, and the roll's time is part of the get-up (C21 for the side falls goes up by it). Risk: written poses can look
  like a machine; the owner's eye, beside (b).

### (b) The world rolls it (the back and front clips only, measured in step 8)

No new code: the side clips' `match` false (a second list, `Specs/motion_library_bf.json`, named by `motion.library`). The carry turns
the side body into the back or front clip's pose at the matched time. Measured: 143 at the first attempt, 8.0% given up, 2 poses held
over 1.0 s, C27 150 of 150, but C21 2.53 to 2.57 s (under the gate) and authority 1.27 (over 1.16; its reason: the world rolls the body).

### (c) Other recorded clips

A side get-up that rolls first, recorded on a person. Mixamo: free with an Adobe account, usable in a project, its files not to be given
away (so fetched by a script and never committed, as Epic's); which of its get-ups begin on a side is unknown until read. A pack on Fab
(for instance a "Knockdown & Get-Up Animation Pack", directional get-ups): its price was not shown in a search on 27 November and is read
from its page before anything is bought; Fab's standard licence. Either needs the owner's account, a reading of its first frames
(pitfall above), and a bone map for another skeleton in the library's list. Days, not a step.

## Step 10: the reference fitted to the floor and to the surface under the body

Three switches, each measured alone on three seeds, then together.

- **10a, the surface under the body** (`getup.fit_surface` 0 or 1). At an attempt's start, the height of what the pelvis lies on: a
  sweep down from the pelvis by the object types of the static and the movable world, the character left out (read only, as `SenseFall`
  sweeps). It replaces `LastSupportHeightCm` for the matcher's `SupportZ` and the frame's height, and C27's 70 cm is read from it. Each
  attempt records the surface and how far it is from the floor last stood on. A body on the table gets up on the table (as the step-7
  file `getup_laid_on_table_back` already does by the carry); what a body on furniture should do, climb down or slide off, is M4b's
  ("get out from under the table") and is shown, not decided, here.
- **10b, the reference fitted to the floor** (`getup.fit_floor` 0 or 1, `getup.fit_ease_s`). Per key of the chosen variant, the
  library's contact marks say which bodies the recording has on its floor; the reference is raised or lowered so that their lowest points
  on this body's shapes (the library's own figure, kept per key) lie on the surface, eased between keys so that it never jumps (C25).
  Today a led foot can hang a few centimetres over the floor or be pressed into it, and the world pushes for nothing.
- **10c, no vertical pull on a body that touches** (`getup.fit_free_z` 0 or 1). A body the recording marks in contact and that touches
  the world now gets no vertical spring and no vertical damping (the control's per-axis multipliers): the floor carries it, gravity and
  the muscles bring it down. Decision 30's damping downward goes where it pressed bodies into the floor; free bodies keep it, since step 5
  showed it brakes the carry's overshoot. Seed 2's trial 38 (a foot held 0.7 cm up for 3 s) is its case.
- **Measured for each:** the stand on three seeds (standing, first attempts, given up, poses held over 1.0 s, C27 by clip, C21, the
  authority by body and by direction, the downward part above all), the four furniture files of `docs/recon/r2_step7_furniture`, and
  the stand's table trials apart (14 of 38 attempts given up there today).
- **A variant, measured and not taken in R2:** the horizontal pull off a loaded contact body too ("a limb that carries weight is not
  pulled" is R7's); its figures handed to R7.
- **If the end still holds a pose over 1.0 s** (a body against a wall or the table at the clip's end): `getup.end_hold_s`, the end
  waits this long for both feet and then hands an upright, supported body to Standing, whose balance steps; measured, as a variant,
  against the falls it may cause.

## Step 11: dragged by the ankles and the wrists (decision 18), and a clean way to give an attempt up

- **The drag** (`getup.drag` 0 or 1). While getting up, holds that together pull with `getup.drag_force_n` (proposed 450, the force
  that holds a get-up down) for `getup.drag_s` (0.1 s), or that move the pelvis along their pull faster than `getup.drag_mps` for
  `drag_s`, give the attempt up at once, the carry's share to 0 at once on every body (not over the release), and the body goes down
  on its muscles (Falling, Lying). No attempt begins while it is dragged: until the holds pull under half of `drag_force_n` and the pelvis
  is still for `getup.drag_free_s`; then `rest_s` and a get-up. Today such a pull takes the pull away and stands the clip for `stall_s`
  (3 s) with the body upright under the hands.
- **For R10:** both places become one each: `GiveUpGetUp(Kind, Reason)` with a kind (hit, held down, dragged, lost, stalled, held up,
  clip gone) counted by kind in `control.get_up`, and `CanBeginGetUp(Why)` before `StartGetUp`, which says why not (dragged; R10 adds
  the player near, fear, a command). The text of every reason stays as it is, so every hash is kept at 0. What happens to the authority
  is said by the kind, in one place.
- **Scenarios:** `getup_drag_ankles_sit`, `_crouch`, `_rise` and `getup_drag_wrists_sit`, `_crouch`, `_rise`: a laid body, two hands
  `at_bone` on both feet or both hands as it sits up, kneels and rises, pulled 2 m at walking pace. Each asks: the attempt given up by the
  drag within 0.2 s, the world's force against the hands while dragged under 5% of the weight (new: `control.get_up.carry_against_holds_n`,
  the carry's force along the holds' pull), the grips held the whole pull, the pelvis 150 cm or more, and standing after the hands let go.
  A phase a scripted hand cannot catch is left out and said, as the hold in the later phases was.
- **Measured:** the hold and press figures of decision 13 on three seeds (one hand, two hands, 300 N), since the drag's line sits near
  them; `grab_drag_fallen_two` and `grab_getup_held` as they are.

## Step 12: the held arm while getting up (FR-43, decisions 19 and 48)

- **`getup.arm_answer`** (0 off; 1 by the body's state; 2 yield; 3 pull in). By state, while getting up: a braced body pulls the arm
  in, any other yields (decision 48's "standing and braced" read for a body getting up; today nothing braces it outside a session with
  `-Perception`, so by state means yield in every scenario, and 3 shows the other).
- **Yield:** while a hand holds a hand, a forearm or an upper arm, that arm's muscles go to `getup.arm_yield_scale` and its clip pose
  is let go over `poses.blend_s` for a placement (`PlaceArm`) toward the holding point along the pull, `getup.arm_reach_cm` beyond it: the
  arm reaches after the hand instead of being drawn straight. Its springs from the world are already off (the held chain).
- **Pull in:** the arm's muscles to `getup.arm_pull_scale` (within their caps), the hand placed `getup.arm_pull_in_cm` from its
  shoulder with the elbow bent: the body draws the arm, the player's hand falls behind its target and its controller vibrates by the force
  it feels (exists).
- **Scenarios:** `getup_pull_arm_yield_sit`, `_crouch` and `getup_pull_arm_in_sit`, `_crouch` (and `_rise` where a hand can catch the
  wrist): one hand `at_bone` on a wrist, pulled 60 cm to the side at 1 m/s. Yield asks a straighter elbow and less force at the grip than
  at 0, and the grip held longer; pull in a bent elbow and the holding hand 15 cm or more behind its target. Both stand at the end.
- **Measured:** the elbow's angle, the grip's frames and force, the holding hand's gap (`hands.csv`), what the body did; films of each.

## Step 13: the fingers `flat` (decision 8)

Nothing to build: a film pair of get-ups from the front and from a side (the hands carry the body) at `poses.fingers.weight` 0 and 1,
a camera on the hands; the suite at 1 to show the bodies' hashes kept. The weight brings all four poses (rest, half a fist, splayed in a
fall, flat), and a hand on its edge still has two fingers in the floor (N8 B): both shown. On or off is the owner's.

## Step 14: the gate (decisions 16 and 21; section 7 and decision 7)

On three seeds, every switch the owner chose on, through one `checks.ps1 -What suite,matrix,knockdowns,seeds` and the stands with
`-HoldHands 1`, `-HoldHands 2` and `-PressN 300`:

| Criterion | Limit | Read from |
| --- | --- | --- |
| C19 | no pose held over 1.0 s; at most 10% of attempts given up | `getup-seeds.ps1`'s gate line |
| C20 | 95% stand; 135 or more of 150 at the first attempt | the same |
| C21 | a median of 2.6 to 4.2 s, from the first move (the roll's included) | the same, each seed |
| C27 | 45 degrees and 30 cm as the median of 150, and for each clip apart | the same, by clip |
| Authority | the mean 1.16 or below, or a reason written | `control.authority`, by body and direction |
| Decision 13 | one hand 20 to 60 of 150 pull free; two hands at most 5; the press reported | the held stands |
| Decision 7 | the push matrix, the knockdowns, the stand: worse only with a reason and a video | `checks.ps1` |
| C25, C2 | C25 at or below 439 N*m (decision 15 of the track) | `natural.ps1 -Quick` |

Then videos side by side for each change switched on (`film-pair.ps1`: the matcher's exist, `Saved\Films\r2s8_*`; the roll, the fit,
a drag, an arm yielding and pulled in, the fingers), the bootstrap, the diff read by an independent reviewer from the files, and the
documents. R2 is done when the owner has seen the videos. The old clip-following path and the four `getup.clip_*` go after that, on the
owner's word, as a clean-up of its own. (Done on 27 November, on the owner's "finish what remains", every hash kept: `docs/testing.md`,
"R2's rest finished".)

## Scenarios, in all

| Scenario | Step | Asks |
| --- | --- | --- |
| `getup_roll_left_back`, `_left_front`, `_right_back`, `_right_front` | 9 (a) | laid on a side; rolled within `roll_s`; the back or front clip; standing at the first attempt; C27 within both limits |
| `getup_roll_push`, `_hit`, `_hold` | 9 (a) | C26 in the roll: a push does not end the attempt, a blow moves the body, a hold of a drag gives it up |
| `getup_fit_foot_on_floor` | 10 | the clip's end with both feet loaded (trial 38's way); no vertical pull on a touching foot |
| `getup_laid_on_table_back`, `getup_laid_across_table_end_front` | 10 | reported, the surface recorded; added to the suite once they pass by a rule the owner has seen |
| `getup_drag_ankles_*`, `getup_drag_wrists_*` (sit, crouch, rise) | 11 | as above |
| `getup_pull_arm_yield_*`, `getup_pull_arm_in_*` | 12 | as above |
| `tune_live_getup_roll`, `_fit`, `_drag`, `_arm` | each | the switch changed in the middle of a run moves the hashes (pitfall: a tuning change that "applied" has not reached the solver) |

New expectations for the runner only where an old one cannot say it (`max_carry_against_holds_n`, `min_rolls_done`), each broken once
on purpose to see it fail.

## Checks, in order, for every step

1. The baseline before it (`suite.ps1 -SaveBaseline r2_<step>`), `build.ps1`, `test.ps1`.
2. The suite at 0 against it: every hash kept, the new scenarios pass.
3. The suite with its switch at 1 (`-Tune ... -Label`): which hashes move (named and explained), which expectations fail and why.
4. The stand on three seeds with it, plain, held by one and two hands, pressed with 300 N (one `checks.ps1` set); the furniture files.
5. Variants, each on three seeds, chosen by figures; one switch at a time, then the chosen ones together.
6. Decision 7 with it: the push matrix, the knockdowns (expected unchanged: no get-up in them; run, not assumed).
7. `natural.ps1 -Quick` with it (C2, C19, C21, C25).
8. A film pair for the owner (the backup first).
9. `docs/testing.md` ("R2, step N"), `docs/active-ragdoll.md` (R2), `docs/PRD.md`, Status, the READMEs, the plan's tabs.

## Risks

| Risk | What is done about it |
| --- | --- |
| The muscles cannot roll a body off its side (an arm under it, the caps) | Measured on the four laid sides first; a roll not done is counted and the world finishes it, as (b) |
| A written roll reads as a machine | Filmed beside (b) and the side clips; the owner's eye decides, not a figure |
| The fit makes the reference jump as the contacts change | Eased over `fit_ease_s`; the largest jump of a target and C25 read with it |
| A body with no vertical pull sinks or slips where the clip has it in contact and this body is not | It falls to the floor, which is what the floor fit is for; poses held and attempts given up read on three seeds |
| The sweep finds the table's edge or the chair's seat and lays the clip there | The surface and its height recorded per attempt; the table trials read apart |
| The drag's line catches one hand's hold (300 N) | 450 N summed; the one-hand stand (20 to 60 of 150) run again |
| A scripted hand cannot catch a limb in a later phase | `at_bone`; the phase left out and said |
| The arm's answer changes the held stands | The stand's hands hold the chest, not an arm: expected the same, run to know |
| A change to `StartGetUp` or `GiveUpGetUp` reaches the session's get-up and R10's paths | Every caller named; the session's self-test and the record and replay in the bootstrap |
| `getup_match_*` count `tuning_changes` when the file already has the matcher on | Read once the switch is 1; their expectations written for the files as they then are |
| C21 under 2.6 s with late starts | Decision 51; the gate reads it per seed |
| The authority stays over 1.16 | The fit's three switches read by direction; the reason written if it stays (the roll by the world, if (b)) |

## Not in R2

The ground poses and life on the floor (R6); the authority brought down and contact-aware leading in the horizontal (R7); what a body
on furniture should do (M4b); the struggle when dragged, the player near, fear and commands (R10, which takes up `CanBeginGetUp` and the
kinds of giving up); how long the body lies by how it fell and a get-up that begins while the body still settles (measured on 27
November, not taken: `docs/testing.md`).
