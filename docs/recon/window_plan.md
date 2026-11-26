# Window of 25/26 November 2026: plan, risks and limits, written before the runs

Started 21:46:39 on 25 November. Window 8 to 9 hours, to about 06:00-06:45.

First action: the seven files of M4 part 5 are in `Saved\Recon\m4_part5_body.patch` (copies of the files in
`Saved\Recon\m4_part5_files`, and in `Saved\Backups\20261125_214652_before_track_window`); HEAD built, 117 of 117 hashes of
`hold`, 44 tests.

## N8 A, written about 22:00 (the clock was not looked at; the file first said 22:35), before any run on the new asset

**What:** a second asset, `PA_SandboxNPCHead` (`Specs/physics/npc_body_head.json`): the head a body of its own (head 5.55%, neck 1.39%
of body mass; the 6.94% of head and neck split 80 to 20 by McConville's volumes, FROM MEMORY, not opened: the pages tried were not
readable); the neck's range shared between the two joints (flexion 40 + 20, extension 50 + 25, sideways 35 + 10, rotation 35 + 45, the
larger part of the rotation at the upper one); a head with a face (cranium capsule and two cheeks, fitted to the mesh from two pictures
by scale: vertex 171.7, chin 149.5, forehead y 12, back of head -7.3, half width 7.6); the wrist's swings limited in the hand's own
frame, bent back 85 (60 before), the two bodies of the joint exchanged in the asset, no projection on that joint.
Poses: the angles a pose gives the neck are shared out between the two joints in proportion to their ranges; no pose changes.

**Limits (go/no-go), from the plan and, where the plan has none, set here before the runs:**

| # | Limit | From |
| --- | --- | --- |
| A1 | stand_npc: pelvis drift, largest settled turning speed of a foot, mean assist share each at most 1.1 times today's | plan ("within a tenth") |
| A2 | neck and head joints stretch at most 0.5 cm in strike_kick_head_knockdown | plan |
| A3 | a slap on the cheek with the open hand at 6.2 m/s turns the head 20 degrees or more about the vertical (today 1 to 3); cheek = 8 cm ahead of the old aim, at the height of the cheek (156 cm); also reported at 0, 4, 11 cm | plan, owner |
| A4 | a dense strip of palm_slap_head shows the head turning on its neck | plan (looked at) |
| A5 | push matrix 160 of 160 | plan ("pass") |
| A6 | knockdowns 8 of 8 by blow and by hand | plan |
| A7 | stand on three seeds: 147 of 150 stand, each seed 45 or more; at the first attempt 129 or more (132 today) | plan (90%), N2's "within 3" |
| A8 | 93 grab pulls within the stretch and force criteria; falls at most 3 (2 today) | plan; set here |
| A9 | physics step: at most 0.05 ms more than today's, alone on the machine | N8 D's figure, set here for A |
| A10 | suite: every scenario that fails is read; only what names neck_01 for the head may get another expectation; nothing else | set here |
| A11 | wrist: no stretch over 0.5 cm in the grab pulls that today's does not have; what the clips ask beyond the wrist's range falls | set here |

If A1 to A3 or A5 to A9 fail and one correction (tuning of the neck for the new body: stiffness, damping; not the limits) does not
bring them in, the part is dropped for now with its figures, the files stay unused, as for the toes.

**Risks:** a light head on a light neck jitters (A1); the plank of the neck: two joints of 40 N*m/rad in series are 20 (C4 wants 20 to 30,
so this is wanted); the swapped wrist joint without projection stretches (A11); the head knockdown's 35 N*s was set on one body: what the
head's body counts may be another figure, the row of 30 N*s of the matrix says; the pose database knows no head joint.


## N8 A, what the first runs said (22:34)

- The slap: what held the head was not the neck but the world's hold on it (assist.head 10 N*m/rad with 0.2 s, that is 2 N*m*s/rad), found by a
  setting that did not move the figure (neck 40 to 1 N*m/rad: 6 to 15 degrees). With assist.head at 0 and the neck's damping 0.05 the new head turns 17.7, 25.8 and 23.2
  degrees (hand held three ways; the first is the old test's, which meets the face with the rear edge of the plate).
- THE TEST WAS CHANGED: the old path ends at x = 5, which the narrower head hardly reaches (0.8 to 2 N*s); the paths here end 4 cm inside the surface (x = 3), as the old one did for the old head
  (9.1 - 5). Two ways of holding the hand were added.
- The wrist in the child's frame: first version (one joint with exchanged bodies, not projected) stretched 0.80 cm hanging by a hand against 0.25; made again as two joints: 0.25.
- The stand, three seeds (150 falls; stood, first attempt): today 149, 132; head alone with the two values 150, 134; head alone without them 148, 131; today's body with assist.head 0: 147, 126;
  wrist alone 148, 113; both 145, 120. Of the wrist: 60 degrees in the child's frame 148, 112; 85 degrees in the parent's frame 148, 134. So it is the frame, not the 85 degrees.
- By A7 the wrist is out (113 under 129) and the head is in. They are two assets from here on: npc_body_head, npc_body_wrist.

## N2, the jumps first (decision 13), written before the runs (23:00)

Today's body (PA_SandboxNPC), so that the corrections are judged on the body the owner has. Each correction alone and then all, by -Tune: suite of 119, push matrix, stand on seeds 1 to 3.
Baseline today over the suite (the newest run, 117 scenarios): the largest jump is 429 N*m (grab_pull_standing, foot_l), not N1's 367: hold_felt and the blow are newer than that figure.

| # | Limit | From |
| --- | --- | --- |
| J1 | the largest command_jump_max_nm of the suite is at most 367 N*m with the corrections that are kept; aim 60 | the owner's message; plan |
| J2 | push matrix 160 of 160 | plan |
| J3 | stand: 147 of 150, 129 at the first attempt | as A7 |
| J4 | suite: a scenario that fails is read, not loosened | decision 13 |
| J5 | fewer scenarios over 100 N*m than today's 58 of 112 | set here |

A correction that fails J2 or J3 by itself is left off; the set that is kept is run whole.

## N3, written before the runs (00:15)

Step 2, the caps from the published peak torques (stronger direction; `docs/natural-body.md`, "Stiffness and strength"), as factors on the group's cap:
thigh 178/250 = 0.712, calf 247/250 = 0.988, foot 112/250 = 0.448, upperarm 83/80 = 1.04, lowerarm 51/80 = 0.64, hand 25/80 = 0.31, neck_01 52/50 = 1.04, head the same; spine and clavicle as they are (300: within the lower back's 236 to 465; the shoulder girdle has no figure).
Limits: push matrix 160; knockdowns 8 of 8 by a blow and by hand; stand 147 and 129; grab pulls 93, falls at most 3; and the joints at their cap while getting up listed (C7: no joint more than 5% of upright frames at its cap).

Step 3, the tone: limp, relaxed, ready (1, today), braced. Proposed: limp 0.1 (damping 0.3); relaxed spine 0.6, neck 0.5, arms 0.25, legs 0.8 (damping 0.7); braced spine 1.5, neck 2, arms 2.5, legs 1.25 (damping 1.3).
Limits: with tone.standing at 2 (ready) every hash as today; C6: braced against relaxed, the chest and the forearm under the same push move at least twice as far relaxed (compliance tool, 20 N from the front).

## N6, written at 00:47 (step 1's first eight runs, hand1, were made at 00:42 before this was written: exploratory, and said so)

Step 1, the hand target (`hand_target`, the foot's placement taking an arm), on a welded pelvis, idle off. Limits from the plan's `pose_hand_place`:
H1 the hand within 3 cm of points in front, to the side and below (reachable points: from the hand's place at the event, within the arm's length from the shoulder);
H2 no joint outside its range by more than 1 degree (`max_joint_limit_violation_deg`, as `pose_foot_place`); H3 no joint faster than 30 rad/s;
H4 with no hand target every hash of `hold` as before (checked at 00:44: 117 the same).
A point above the head is not in the plan's criterion; it is run for information only.

Step 4, the head (run at 00:35 by -Tune, before limits were written here; judged afterwards by the plan's "Must hold", which existed before):
rest within 3 s, into the floor 2 cm, energy 40 J, the stand no worse than today's 149 and 132. Result: 2.91 s, 1.75 cm, 28.4 J, stand 147 and 132: the stand is two worse,
and the suite has one failure (room_movable_fall_gets_up). By the plan's rule it does not hold.

## N6 step 5, the arms reach for the floor (`fall.aim`), written at 01:05 before its runs

Off by default (`fall.aim` 0). With it on, by -Tune, on the knockdowns by a blow and by hand, the scorecard's fall criteria and the stand:
F1 with `fall.aim` 0 every hash of `hold` as before;
F2 the knockdowns 8 of 8 both ways, each at rest within 3 s, 2 cm into the floor, 40 J at most (C17);
F3 C14 no worse than today (86% by hand, 80% by blows) and at least the proposed 80%;
F4 C15 lower than today's 75% by hand and 50% by blows; C16 no higher than today's 4.32 and 2.82 m/s;
F5 no joint faster than 30 rad/s in the knockdowns;
F6 the stand on three seeds no worse than 149 and 132; F7 the push matrix 160; F8 the suite: what fails is read, not loosened.

## N4 step 1, the clocks after a hit (`reactions.reflex_delay_s`), written at 01:30 before its runs

Off by default (0). The delay tried is 0.08 s (C9's human range 0.07 to 0.11; the calf's medium-latency reflex 72 ms). "Every reaction switched off" of the plan is stood in for by a delay of 10 s
(everything held back for the run's rest), said so.
R1 with the delay at 0 every hash of `hold` as before;
R2 `react_passive_first`: the run at 0.08 and the run at 10 s are the same (`compare-runs`) until the hit + 0.08 s, within one frame;
R3 `react_unseen_chest_30`: `control.reactions.0.first_target_change_s` within two frames of 0.08 (0.058 to 0.102); no step lifts before 0.08;
R4 `react_two_hits`: a second hit 30 ms after the first does not start the delay again (the flinch at the first hit + 0.08 within a frame);
R5 the push matrix at 0, 0.05, 0.08 and 0.11 s, laid beside the owner's table: for the owner, nothing kept (decision 3 of the plan is the owner's);
R6 at 0.08: knockdowns 8 of 8 both ways, the stand on three seeds no worse than 149 and 132, the suite: what fails is read.

## N6 step 5, a third variant (02:10): no reach in a fall backward (`fall.reach_back` 0), judged by the same F1 to F8 written at 01:05.

## N5 step 1, perception that judges and does nothing (`TellSeen`, `perception.*`), written at 02:25 before its runs

P1 a scenario without `"perception"` has the hashes of `hold` (all 117);
P2 with `"perception": true` a run has the same hashes as without it (step 1 has no behaviour; measuring changes nothing);
P3 the measures against known answers: a scripted hand coming at the face from the front at about 3 m/s is seen and counted a threat before the contact, with its time to contact within 0.05 s of the
   path's; the same from behind is not seen and no threat; a hand moving at 0.3 m/s near the chest for seconds is seen and no threat; and broken on purpose (the view turned round) the front hand is not seen;
P4 a run with perception on gives the same hashes twice;
P5 `SandboxBody` includes nothing of `SandboxInteraction`.

## N5 step 2, the gaze (`perception.gaze`), written at 02:40 before its runs

G1 with `perception.gaze` 0 (the file) every hash of `hold` and of the three `perceive_*` as before;
G2 `gaze_follows_hand` (a hand circling 1 m in front of the head, 50 cm round, at 0.5 m/s; gaze on in the scenario): mean gaze error under 15 degrees (the plan), the neck never at its bound;
   and with the gaze off the same measure reads the head not following (break on purpose): above 20 degrees;
G3 the neck's target never jumps: `control.pose.target_jump_max_deg` under 1 degree in that run;
G4 the body stands through it: no step, no fall.

## N5 step 3 in part, the look at a hold (02:58, before its runs)

The body looks at the place it is held at (after a threat, before a thing that moves), behind the gaze (`perception.gaze`). The brace of the held chain and the free hand to the holder are not built.
K1 with the gaze off every hash as before (hold, perceive_*, gaze_follows_hand);
K2 `hold_arm_answer` (a forearm taken by a hand with the grab on and pulled about 150 N to the side, gaze on): the body leans against the pull by at least 2 cm (the plan's figure; hold_felt's lean,
   already there), and the head's facing is within 20 degrees of the hold for most of the hold (gaze error mean under 20 over the frames it looks at the hold);
K3 no step and no fall during the hold; the neck's target does not jump (under 1 degree).

## N5 step 4, anticipation (`perception.anticipate`), written at 03:28 before its runs

A threat noticed starts, after `perception.react_s` (a person's reaction to what is seen, 0.15 s), the brace of N3 (the whole body) and the flinch's pose (shoulders up, head in) before the contact.
A1 with `perception.anticipate` 0 every hash as before (hold, the four perception scenarios);
A2 `seen_slow_punch_braced` against `unseen_punch_from_behind` (the same hand at the chest, from the front and from behind): the brace begins at least 50 ms before the contact, and the struck chest
   moves at most 0.7 times as far as unseen (the plan's figures; the tone's braced levels as proposed for N3 by -Tune, since the file's are 1);
A3 `seen_fast_jab_too_late` (5 m/s from 50 cm): no anticipation;
A4 the hand hovering by the chest: no anticipation (no false alarm);
A5 the pose comes in without a jump: target_jump_max_deg under 1 degree in the seen run.

## N5 step 5 in part, the guarding arm (`perception.guard`), written at 03:46 before its runs

A threat to the head with at least `perception.guard_time_s` to the contact, or held near the face, raises the near arm: its hand is sent to a point on the line from the face to the threat,
`perception.guard_reach_cm` from the face, for `guard_hold_s`, eased in and out. Not through the placed-feet list (that list stops the body stepping). The free hand to a holder is not built.
U1 with `perception.guard` 0 every hash as before;
U2 `seen_fist_raised_guards` (a fist brought slowly to 30 cm before the face and held there a second, perception, anticipation and guard on): the near hand rises at least 30 cm and comes within
   15 cm of the line from the face to the fist; the body neither falls nor flinches;
U3 a fast jab (5 m/s from 50 cm): no guard; the hovering hand by the chest: no guard;
U4 the arm's targets do not jump (target_jump_max_deg under 1 degree in U2's run).
