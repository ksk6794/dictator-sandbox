# The register of switches (the audit of 28 November 2026, part 3)

A row for every tuning value of `Config/Tuning` that turns a built feature on or off or keeps it neutral: 0/1 switches, shares
and weights at 0 that disable a mechanism, neutral factors at 1, thresholds at 0 that mean "never", modes. Plain physical values
and the assist's state springs (`states.*.assist.*`) are left out. Where a switch at 1 replaced an older way, the row says what the
0 branch was, because removing such a switch means removing that branch. Who decides what is removed: the owner (the audit's
plan, `docs/recon/audit_plan.md`). Nothing here was changed; no engine was started for it.

**Sources and abbreviations.** BC = `Source/SandboxBody/Private/SandboxBodyController.cpp`; BT = `Source/SandboxBody/Public/SandboxBodyTuning.h`
(the value's own comment, which says what it does); IT = `Source/SandboxInteraction/Public/SandboxInteractionTuning.h`; T =
`docs/testing.md`; AR = `docs/active-ragdoll.md`; NB = `docs/natural-body.md`; PRD = `docs/PRD.md`. "Readers" are every use outside
`VisitValues`/`CheckRange` and the tests, found by `rg` for the C++ field and checked by its struct where the name is common
(`Head`, `Turn`, `Roll`, `Drag`, `Weight`); `SaveRecording` (SandboxPlaySubsystem.cpp:490-555) writes some switches into a
session's recording and is named where it does. "Scenarios" are those that set the value themselves (`Sandbox.Tune` in a `console`
event); none means the suite runs it at the files' value. Line numbers of the documents are those of 28 November, afternoon; other
agents were editing the documents while this was written, so a line may have moved by a few.

**The four conditions** asked of a deletion: (1) rejected in words, or measured worse with no open question; (2) not waiting for
the owner; (3) not named by a later part of the plan; (4) not better than the files' body on any criterion. The verdict of each
row is in the last section.

## Body: perception (`perception.*`, N5)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `perception.enabled` | 1 | Judges what the body is told it sees (threats, notice), only for a body allowed to perceive (a scenario's `"perception": true`, a session with `-Perception`) (BT:831-832) | BC:8261, 8269 `TickComponent` | None set it; `perceive_hand_behind`, `_front`, `_hover`, `gaze_follows_hand`, `viewer_with_gaze`, `fear_threat_rises` set the body's `"perception": true` | R5 (N5 for an upright body, AR:524) | No | Built at 1 (26 Nov), no decision | None on vs off; N5 step 1 P1/P2 "the same: met" (T:4693) | No |
| `perception.gaze` | 0 | Turns the head toward what the body attends to (BT:845-846) | BC:7658 `UpdatePerception`, 8269 `TickComponent`; recorded, SandboxPlaySubsystem.cpp:526 | `gaze_follows_hand`=1, `viewer_with_gaze`=1; `play.ps1 -Perception` sets it to 1 (Tools/play.ps1:116) | R5: "the gaze, anticipation and the guard that exist behind switches" (AR:524) | "for the owner's look in the headset" (T:4930); NB decision 6 (eyes) open until R5 | The owner, 26 Nov, N5 question 6: 0 in the files, 1 in the session (T:4917) | N5 step 2 (T:4708-4720, 25-26 Nov): C13 10.5 degrees, the neck's largest jump 0.48 degrees, met; at a hold 62.6 degrees, missed | No |
| `perception.anticipate` | 0 | Braces the body and draws the head in before the contact of a threat it noticed (BT:857-861) | BC:7628, 7637, 7641 `UpdatePerception` | None (the session's `-Perception`) | R5; R4: "Real tone levels for anticipation's brace (N5)" (AR:497) | AR decision 12: "left until R4" | The owner, 26 Nov (question 6) | N5 step 4 (T:4755): the chest moves 16.5 cm with it, 15.6 without (A2 missed); the face 18.0 against 25.1 | T:4748 "A2 missed; off." (kept for R4 and R5) |
| `perception.guard` | 0 | Raises the near arm against a threat to the head (BT:865-869) | BC:7627 `UpdatePerception` (and `UpdateGuard`) | None (the session's `-Perception`) | R5: the face guarded with both forearms (C36, AR:529) | T:4772 "For the owner to look at." | The owner, 26 Nov (question 6) | N5 step 5 (T:4767-4780): 69 cm, 9.0 cm, standing with no flinch: met; U4 0.48 degrees, met | No |

## Body: behaviour (`behaviour.*`, R10)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `behaviour.fear` | 1 | One number 0 to 1, raised by hits from moving things and seen threats, faded over `fear_fade_s`; read only by the behaviours (BT:880-884) | BC:3165 `IsAfraid`, 3172 `AddFear`, 3189 `UpdateFear`, 2802 `CanBeginGetUp`; recorded :548 | None set it (`fear_blow_fades`, `fear_threat_rises` run it on) | R5 and R6's behaviours "within R10's choice" (AR:291-292) | "Waits for the owner's look at the videos ... and the headset at M4's gate" (AR:277-278) | The owner, decision 67 | R10's gate (T:7541, 28 Nov): C33 met; with a viewer near 92 of 150 at the first attempt (143 nobody near), 27.5% of attempts given up (T:7070, 7148); no viewer: every hash kept | No |
| `behaviour.stay_down` | 1 | An afraid body with the player near begins no get-up; one under way is run back and given up; it lies in `poses.protect_head` (BT:886-897) | BC:2798 `CanBeginGetUp`, 3217 `UpdateStayDown`, 8306 `TickComponent` | `tune_live_stay_down` (0, then 1); `protect_near_*` run it on | R6: "the body that lies covered while the player stands over it" (AR:544) | AR:277-278 | The owner, decision 67 | As `fear` (C33 met, T:7541; C20 92 against 143 with a viewer) | No |
| `behaviour.lower_end_cm` | 0 | The lowering ends at the clip's start (0), or once the chest is this near the floor (BT:902-906) | BC:4434 `UpdateGetUp` | None | No | No | Taken for the owner, decision 59; confirmed 28 Nov | R10 (T:7049-7055): ended at the chest 25 or 35 cm up, a kneeling head comes down at 1.4 to 1.8 m/s (the files: 1.14); at the pelvis 25/40 cm, 2.0 to 2.9 | AR:787 "Not chosen: ... (measured as the lowering ended by the pelvis's or the chest's height: the head of a kneeling body at 1.4 to 2.9 m/s)" |
| `behaviour.commands` | 1 | Answers `get_up` and `stop`; 0 refuses every command (BT:916-921) | BC:3033 to 3116 `TellCommand` (38 reads) | `command_refused_off`=0 | M4b takes the commands that are actions (AR:293-295) | AR:277-278 | The owner, decision 67 | On alone, 204 of 205 hashes kept (T:7203); C39 146 of 150 within 1.0 s (T:7542), 150 of 150 with `command_cover_react` | No |
| `behaviour.command_cover_react` | 1 | The protective pose let go `command_react_s` after a `get_up` or `give_hand`, counted as the command's first motion (BT:925-929) | BC:3213 `UpdateStayDown` | None | No | AR:277-278 (R10 as a whole) | Under the delegation, decision 74 | C39's slow four (T:7617-7623): 150 of 150 answer at 0.30 s (146 within 1.0 s), 94 at the first attempt (92); 149 standing at the end (150) | No |
| `behaviour.protect` | 1 | A hit on the head of `protect_from_head_ns` (elsewhere `protect_from_ns`) while lying or rising gives the attempt up and covers the struck place for `protect_hold_s` (BT:930-935) | BC:2772 `CanBeginGetUp`, 6527, 6543 `FeelFrame`, 3433 `UpdateLimbClaims`, 3300 `UpdateStayDown`, 3870 `UpdateOffer`, 3067 `TellCommand` | `getup_hit_strong_protect_off`=0; `tune_live_protect` (0, then 1); `protect_hit_*` run it on | R5: the face guarded, the groin and the solar plexus covered, within R10's choice (AR:313); R6: breath in the protective pose (AR:543) | AR:277-278 | The owner, decision 67 | R10's gate (T:7544): C34 head, trunk, groin, leg struck lying 0.36 to 0.56 s (met), a leg kneeling 1.07 s (0.56 with `protect_held_leg`); worse: largest target step 102.4 degrees against 96.7 (T:7318), `getup_pressed_two_hands` left lying, a kneeling body falls onto its face first, head 1.90 m/s (T:7096) | No |
| `behaviour.protect_held_leg` | 1 | A struck leg's cover waits `protect_leg_wait_s` and is not begun, or is let go, when a hand takes hold of a leg (BT:941-946) | BC:2855 `UpdateHeldLegCover`, 6566 `FeelFrame`, 8276 `TickComponent` | None | No | AR:277-278 | Under the delegation, decision 73 | T:7564-7577: C34 for a kneeling leg 0.56 s (1.07), its command jump 194 N*m (304); worse: `drag_lying_ankles_still` largest target step 75 degrees (31.6) | No |
| `behaviour.give_hand` | 1 | Answers `give_hand`: the nearer arm reaches for the player's open hand; taken, the body gets up on it; 0 refuses (BT:948-961) | BC:3064 `TellCommand`, 3869 `UpdateOffer` | `command_give_hand_off`=0; `command_give_hand_*`, `offer_phase_*` run it on | No | AR:277-278; on "for the owner's headset session" (AR:799) | Under the delegation, decision 71 | R10 step 8's rest (T:7432-7450): at 0 every hash kept; on, C39 the arm moves 0.30 s after; the get-up 4.22 s, target step at its start 31 degrees, command jump 254 N*m | No |
| `behaviour.struggle` | 0 | A lying body dragged by two hands draws whether it struggles; struggling, it pulls the held limbs in and draws the legs up (BT:963-978) | BC:3636, 3666 `UpdateStruggle`, 8329 `TickComponent` | `drag_lying_ankles_struggles`, `drag_lying_wrists_struggles`, `struggle_phase_hit`, `_hold`, `_push`=1 | R6: "struggling when held" (AR:541; moved to R10 by decision 40) | Decision 72, "For the owner (not taken)", option (e) recommended (AR:800) | Never switched; decision 72 the owner's | "The struggle switched on" (T:7599-7607, 28 Nov): C35 9 of 18 attempts free the body (9 of 9 by the ankles, 0 of 9 by the wrists; none with it off); two hands on the stand 0 of 150 pull free, 145 stand, as off; C25 287 N*m, within | No |

## Body: fall (`fall.*`, N6 and R3)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `fall.aim` | 0 | N6: the arms reach toward a point of the floor while the body falls; with it on the catch does not run (BT:701-702) | BC:7473 `UpdateFallAim`, 985 `UpdateFallRun`, 8349 `TickComponent` | None | R3: "replaced by the catch fall and removed once it lands" (AR:471) | "stays until the owner's word" (AR:484, 337; NB:774) | Built at 0; kept off by the owner, 26 Nov (T:4907) | T:4890 (26 Nov): better C15 18% / 25% struck (55% / 63% off), C16 by blows 1.20 m/s (3.00); worse C16 by hand 4.28 (4.16), rests up to 4.18 s, 38 J (14.2 / 32.1), the stand 148 and 133 (150, 130); C14 the same | T:4907 "The owner kept `fall.aim` off (question 4)" |
| `fall.reach_back` | 1 | With `aim`, the hands reach in a fall backward too (BT:712-713) | BC:7474 `UpdateFallAim` (only with `aim`) | None | With `aim` (R3 removes it) | With `aim` | Built at 1, never switched | N6 step 5 (T:4621): the variant recommended then had it at 0 | No; the recommended variant used 0 |
| `fall.blend` | 0 | Lays a blend of the sample's four reaching poses over the arms by the sensed way (BT:716-721) | BC:717 `UpdateFallBlend`, 8314 `TickComponent` | None | R3: "a blend by direction and time to impact" (AR:461); variant B is A and `blend` (decision 58) | Decision 37: "stays 0, to be worked on" (AR:762) | The owner, decision 37 | R3 step 2 (T:6039, 27 Nov): better C14 by hand 100% (83), C15 by hand 45% (55), C16 by hand 3.88 (4.16); worse C16 by blows 3.69 (3.00), C17 19.4 J (14.2), the stand 130 at the first attempt (140), 20.6% given up (14.3%). As B (T:6800): 128 of 150 stand under 300 N, rests 6.43 s | Not rejected: "to be worked on" |
| `fall.blend_share` | 1 | How much of the blend lies over the written fall pose (BT:722-723) | BC:732, 814 `UpdateFallBlend` (only with `blend`) | None | With `blend` | With `blend` (37) | Decision 37, confirmed | T:6076: at 0.5 the head slower by blows in four of six falls, a rest of 3.03 s, the stand 135 at the first attempt | AR:762 "Not chosen: ... a share of the written pose (left at 1 to be seen whole ...)" |
| `fall.arm_curve` | 0 | The falling arms' tone on a curve of the time to impact: far, braced, touched (BT:733-738) | BC:7413 `UpdateTone`, 868 `UpdateFallRun` | `fall_back_r3`, `fall_catch_forward`, `fall_catch_side`, `fall_phase_catch_hit`, `_hold`, `_push`=1 | R3 itself (variant A; "their strength on a curve", AR:466) | Decision 58 "not yet", to confirm (AR:785) | Built at 0; kept under the delegation, decision 58 | As a part of A (T:7332, see `fall.catch`); without it the rests of three 160 N*s blows 3.08, 3.24, 1.54 s, with A 3.28, 3.10, 3.96 (T:6872) | As A: "not yet" (decision 58) |
| `fall.arm_far_stiffness`, `arm_far_damping`, `arm_brace_stiffness` | 1, 1, 1 | Factors of the falling arms' tone far from the floor and braced; at 1 the curve is flat until the touch (`arm_brace_damping` 1.5 and `arm_touch_*` are not neutral) (BT:739-744) | BC:7418-7419 `UpdateTone` (only with `arm_curve`) | None | R3 (the curve) | With `arm_curve` (58) | Decision 55, confirmed 28 Nov | T:6695: a brace of 2.0 to 3.0 from 0.2 or 0.5 s: C14 by hand 83%, but the head struck in 27% by hand, rests of 4.6 s, 41 J | AR:782 "Not chosen: a brace near the floor (...: ... the head struck in 27% of them, rests of 4.6 s, 41 J)" |
| `fall.catch` | 0 | The arms placed in the foreseen chest's frame toward where the floor will be (BT:745-754) | BC:868, 985 `UpdateFallRun` | The six R3 scenarios =1 | R3 (A); R10's priority 1, "the fall (R3: the catch ...)" (AR:592) | Decision 58 "not yet"; "Recommended: A, after the videos" (AR:785) | Built at 0; kept under the delegation, decision 58 | Decision 58 (T:7332, 28 Nov), A: better the head strikes by hand 9% (55%), by blows 0% (63%), on 96 more by hand 16% (44%), C14 by blows 85% (62%); worse C14 by hand 44% (61%), C16 by hand 5.38 (4.16), C34 trunk 0.73 s (0.56), the stand with a viewer 149 (150) | AR:785 "Not chosen: A now (C14 by hand and C34 worse)" |
| `fall.catch_back` | 0 | The catch in a fall backward too (BT:766-767) | BC:990 `UpdateFallRun` (only with `catch`) | None | No (R3 leaves a fall backward to the written pose's arms) | No (55 confirmed) | Decision 55, confirmed 28 Nov | T:6705: the falls backward gained 37 to 50 J | AR:782 "Not chosen: ... the catch backward (37 to 50 J, over the owner's 40)" |
| `fall.catch_max_torque_scale` | 1 | The arms' torque caps while they catch, a factor (BT:768-769) | BC:1124 `UpdateFallRun` | None | No | No | Built at 1, never switched | Never measured at another value (T:6769, "kept as found") | No |
| `fall.catch_joint_blend` | 1 | A catching arm blended in and out by its joints' turns (1) or by where its hand is (0) (BT:772-776) | BC:1081 `UpdateFallRun` | None | No | No (56 confirmed) | Decision 56, confirmed 28 Nov | AR:783, T:6716: at 0 the elbow's target turned over by up to 175 degrees in 3 of 174 scenarios; at 1 no arm's step over 52 degrees in 120 falls; the head strikes 9% either way | AR:783 "Not chosen: by where the hand is, until its flips are found and mended" (an open "until") |
| `fall.catch_let_go_at_trunk` | 0 | The catch let go once the trunk has met the world (1) or when the body lies (0) (BT:777-778) | BC:986 `UpdateFallRun` | None | No | No (55 confirmed) | Decision 55, confirmed | T:6731: at 1 the one target that turned over was gone, but two sideways knockdowns rested after 6.3 s and never | AR:782 "Not chosen: ... the catch let go as the trunk touches (...)" |
| `fall.head` | 0 | The neck braced and the head turned from the floor by the sensed way (BT:779-784) | BC:868, 976 `UpdateFallRun`, 7427 `UpdateTone` | The six R3 scenarios =1 | R3 (A); R10's priority 1 (AR:592) | Decision 58 | Built at 0; kept under the delegation, decision 58 | Alone (T:7357): the head struck in 25% of the 96 falls (36%), the covers as without; C14 by hand 44%, by blows 52% (62%) | AR:785 "Not chosen: ... `fall.head` alone, measured as a lighter option (...)" |
| `fall.curl` | 0 | Curls the body up (`poses.fall_curl`, no catch) when the time to impact at the fall's first tick is under `curl_below_s` (BT:794-798) | BC:868, 923, 941 `UpdateFallRun`, 505 `ApplyPose` | None | R5: a strong blow into the groin "fells it into R10's curl" (C38, AR:531-533); R6: "the curl after a strong blow" (AR:544) | No; "not proposed" (57, confirmed) | Decision 57, confirmed 28 Nov, which kept it: "Not chosen: dropped (the switches cost nothing at 0)" | T:6779: at 0.3 s only sideways blows curl, the head 0.18 m/s instead of 0.44, but 26.9 J instead of 15.3; at 0.45 s the head strikes in 45% by hand | AR:784 "not proposed" |
| `fall.curl_from_ns` | 0 | Also curls a fall begun within a second of a hit of at least this; 0 never (BT:800-801) | BC:923 `UpdateFallRun` (only with `curl`) | None | C38's strong blow from 100 N*s (AR:533) | No | Decision 57 | T:6779: at 150 the head struck in 34% of 41 blows (27% with the catch), a fall gained 67 J | AR:784 "Measured too ...: the head struck in 34% ... and a fall gained 67 J" |
| `fall.sit` | 0 | Sits a fall backward down (`poses.fall_sit` laid over the fall's pose) (BT:802-806) | BC:868, 977 `UpdateFallRun`, 7422 `UpdateTone` | None | R3's own design: "Sit down going backward" (AR:469) | No (57) | Decision 57, confirmed (kept at 0, not dropped) | T:6785: as written +49 J, at half its angles 37.5 J and a rest of 3.54 s, by the legs' give no head figure better | AR:784 "the sit gained 37 to 49 J (by the legs' give instead, no figure better)"; not proposed |
| `fall.sit_leg_stiffness` | 1 | The legs' stiffness factor while sitting down (BT:809-810) | BC:7424 `UpdateTone` (only with `sit`) | None | With `sit` | No | Never switched in the files (0.4 tried) | T:6785: at 0.4 "no head figure better" | As `sit` |
| `fall.turn` | 0 | Turns a fall backward or forward toward a side when there is time (`poses.fall_turn`) (BT:811-815) | BC:868, 929, 978 `UpdateFallRun` | None | R3's own design: "turn to a side when there is time" (AR:469) | No (57) | Decision 57, confirmed (kept at 0, not dropped) | T:6788: needs 0.35 s and a way that leans to a side; where it ran the head struck more (28% by hand of the 96; 38% at 0.25 s) | AR:784 "where it ran the head struck more"; not proposed |
| `fall.palms` | 0 | Turns each catching hand so that its palm faces the floor (`palm_share` of the turn) (BT:820-824) | BC:1120 `UpdateFallRun` (only with `catch`) | The six R3 scenarios =1 | R3: "`splayed` with the palms to the floor" (AR:470) | Decision 58 | Built at 0; kept under the delegation, decision 58 | T:6790: the palm's angle at the first touch 81 degrees without, 63 at 0.6; on the body with R10 the same figures as `head` alone (T:7358) | As A: "not yet"; `palm_share` 0.8 not chosen (AR:782) |

## Body: tone (`tone.*`, N3)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tone.<limp, relaxed, ready, braced>.<group>.stiffness`, `.damping` (40 values) | All 1 | Factors on each group's stiffness and damping for the four levels; with every one at 1 a level changes nothing (BT:675-683) | BC:7401-7403 `UpdateTone`; a level is asked by a scenario's `"tone"` event (ScenarioRunnerSubsystem.cpp:1694-1704, `RequestTone` BC:7380) | None; no scenario has a `"tone"` event | R4: "Tone (N3): relaxed arms and neck standing, braced on a hit or a threat, limp at an impact" (AR:492), "Real tone levels for anticipation's brace" (AR:497) | Decision 12: "left until R4" (AR:734) | Built at 1, never switched | N3 (T:4538-4542, 25-26 Nov), by `-Tune`: C6 the forearm 3.4 times, met; the chest 1.3 times, not met | T:4545 "not taken further" |
| `tone.standing.<group>` (5 values) | 2 (ready) | The level each group stands at with nothing happening (BT:687-688) | BC:7401 `UpdateTone` | None | R4: "relaxed arms and neck standing" (AR:492) | R4 (decision 12) | Built at 2 | T:4544: arms and neck at 1 (relaxed), after 30 N*s the hands travel 12 cm against 20, "less, not more" | T:4545 "not taken further" |
| `tone.brace_from_ns` | 0 | A hit of at least this braces the whole body after `brace_delay_s` for `brace_hold_s`; 0 never; with `braced` at 1 it would change nothing anyway (BT:689-693) | BC:6440 `FeelFrame` | None | R4: "braced on a hit" (AR:492) | R4 | Built at 0, never switched | T:4541: at 10 the matrix 159 of 160 (160); "does not step less: 6 steps at 30 N*s in both" | No |

## Body: muscles (`muscles.*`, N2 and N3)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `muscles.<group>.weight_share` (5 values) | All 0 | How much of the torque that carries the weight beyond each joint the muscle gives (`FSandboxWeightTorques`) (BT:39-43) | BC:7121, 7141 `UpdateWeight`, which returns at 7124 while every share, `ankle_as_torque` and `internal_share` are 0 | None (`tune_live_weight_share`, NB:481, was never made) | R4: "The weight as feed-forward through the drives' targets" (AR:490) | R4 | Built at 0, never switched | N2 steps 1 and 2 (T:4510-4513, 25-26 Nov): every share at 1, 9 of the suite fail, the matrix 158 (160), the stand 147 and 130 (149, 132); the arms alone bring the hand's miss from 7.2 to 3.1 cm (T:4585) | T:4497 "Not usable as built; everything stays off." |
| `muscles.<group>.pose_velocity_share` (5 values) | All 0 | How much of the target pose's own speed the damping holds to; 1 is Physics Control's default (BT:31-38) | BC:4938 `EnterState`, 8141, 8171 `PushTuningToControls`, 8729 `GetControlView`; NaturalMeasures.cpp:460 | None | Not by name | No | Never switched; why 0: a jumping target gave 600 J (docs/pitfalls.md:91-94). The same mechanism is on for a swinging leg (`balance.swing_pose_velocity_share` 0.75, BC:8141) | Only the get-up state's copy, since gone: N7 X1 (T:4975): the stand 149 and 120 (150, 130), the pause 2.24 s (2.42) | T:4979 "Not met; everything off." (the copy) |
| `muscles.joints.<kind>.stiffness_scale`, `damping_scale`, `max_torque_scale` (36 values) | All 1 | Each kind of joint's factors on its group's muscle (BT:652-658) | BC:8117-8119 `PushTuningToControls` | None | R4: "Springs swept down to a person's (C3, C4), caps to a person's (C5)" (AR:491) | R4 | Built at 1, never switched | N3 (T:4528-4532): the caps only: the matrix 160, the knockdowns 7, the stand 149 and 121; the stiffness and damping scales never tried | T:4534 "C5 is not met within the limits, and nothing is changed" |
| `muscles.weight_legs_by` | 1 | Where the push under a standing foot comes from: 0 as measured, 1 by where the centre of mass lies between the soles; no effect while the weight torques are off (BT:999-1004) | BC:7137 `UpdateWeight` (after the return at 7124) | None | R4 (N2) | R4 | Built at 1, no decision | None | No |

## Body: assist (`assist.*`)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `assist.internal_share` | 0 | How much of what the assist wants of an upright body the legs and the trunk give, not the world (BT:1011-1016) | BC:7123, 7231, 7235 `UpdateWeight`, 7345 `InternalAssist` | None | R4: "The assist on an upright body as torques of the standing legs (N2 step 5)" (AR:494); R7: "Forces through the legs" (AR:552) | R4 | Built at 0, never switched | N2 (T:4496-4497): with the shares at 1 and `ankle_as_torque`, 41 fail and 91 of 150 falls end standing | T:4757 "both off and not recommended" |

## Body: balance (`balance.*`)

`balance.ankle_gain` (1) is a working gain ("0 switches it off", BT:216; chosen at 1 in M3 part 3, T:1103), not a switch, and is
left out.

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `balance.ankle_as_torque` | 0 | The ankle strategy as a shift of where the floor pushes the feet (torques), not a turn of the ankles' targets (BT:1005-1010) | BC:1863 `UpdateStrategies`, 7123, 7231 `UpdateWeight` | None | R4: "with the legs' balance strategies as torques" (AR:491) | R4 | Built at 0, never switched | The combined run of N2 (T:4497); never alone | T:4497 "Not usable as built"; T:4757 "both off and not recommended" |
| `balance.ankle_side_gain` | 0 | The ankle strategy sideways (BT:219-223) | BC:1855 `UpdateStrategies` | None | No | No | M3 part 3, 23 Nov (no owner's decision) | M3 part 3 (T:1105-1108): after 30 N*s on the side of the chest the capture margin -5.1 cm with no strategy, -9.3 at a gain of 1, -9.9 at 3 | T:1105 "Sideways the ankles made things worse." (and BT:220-222) |
| `balance.lift_smooth` | 1 | N2's repair: a lifted foot's leg solved from where it stood, its level and limit eased over `step_ease_s` (BT:336-343) | BC:5386 `UpdatePlacements`, 5835 `StartSwing` | None | R4 takes the jumps left (NB:345) | No | The owner, 26 Nov (NB decision 13) | T:4867 (26 Nov): off alone, the matrix's cell of 30 N*s on the head at 270 degrees takes two steps; with the set C25 439 N*m | No |
| `balance.settle_smooth` | 0 | N2's repair: a landed foot settles from the height its leg was solved for (BT:340, 344) | BC:6234 `UpdateStep` | None | R4 (NB:345, 508) | "stays off until it is understood" (T:4886) | The owner, 26 Nov: taken without it (T:4879) | T:4861: with all five repairs, `effector_hit_head` falls and the cell takes two steps | T:4879 "Taken, by the owner's choice: without `settle_smooth`" |
| `balance.release_smooth` | 1 | N2's repair: a leg let go as a fall begins goes back to the pose from the target it was solved for (BT:341, 345) | BC:5343 `ReleaseFoot` | None | R4 (NB:345) | No | The owner, 26 Nov (NB decision 13) | T:4868: touches neither the fall nor the cell; alone (T:4453) 429 N*m, the stand 149 and 126 | No |

## Body: reactions (`reactions.*`)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `reactions.thing_support` | 1 | A push from below by a thing with weight that does not rise fast is support, not a hit or a press (BT:498-505) | BC:6298 `OnBodyHit` | `prop_getup_on_plate`, `prop_stand_on_plate`, `prop_step_on_plate`=1 (the files' own value) | No | No | The owner, 25 Nov (T:4159) | T:4146-4159: the stand with movable furniture 149 of 150, 130 at the first attempt (134 off); `room_movable_fall_gets_up` 7 (12 off) | No |
| `reactions.hold_felt` | 1 | A hold is felt as a held push: the body leans into it, steps for it, and a get-up is pressed by it (BT:506-507) | BC:6668 `FeelFrame` | None | No | No | The owner, 25 Nov (T:4205) | T:4195-4205: thigh and calf pulls followed by a step 9 of 24 (2); falls in 93 pulls 2 (1 off); one hand 46 pull free (47) | No |
| `reactions.reflex_delay_s` | 0 | After a hit the body moves by mechanics alone for this long; the flinch, the doubling over, the strategies' goals and a step wait (BT:482-487) | BC:6445, 6493, 6606, 6611 `FeelFrame` | `react_two_hits`=0.08, `react_unseen_chest_30`=0.08 | R4: "Mechanics first (N4): the reflex and the step at a person's delays" (AR:493) | R4; "Put off by the owner ... until the assist is torques of the legs" (T:4640) | The owner put it off, 26 Nov (T:4912) | N4 step 1 (T:4667-4674): at 0.08 s the matrix 155 to 156 (160), knockdowns 6 (8), the stand 150 and 127 (150, 130), eleven fail; C25 487 N*m (429) | T:4676 "Not met either way; off." |

## Body: get-up (`getup.*`, R1 and R2)

`getup.rate` (1, "as recorded") and the matcher's weights (`match_*`) are working values, not switches, and are left out.

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `getup.rest_per_ns_s` | 0 | The rest before a get-up grows by this per N*s of the hardest hit; 0 `rest_s` for every fall (BT:1049-1053) | BC:2735, 2740 `GetUpRestS` | None | R6: "lying longer after a harder fall" (AR:540) | "whether it reads as more human is the owner's eye, not a figure" (T:5957) | Never switched | T:5946 (27 Nov): at 0.005 and 3 s it lies 2.70 s, 150 and 138, 14.7% given up (0: 1.90 s, 149 and 139, 10.7%); at 0.01 and 4 s 3.58 s, 16.2% | T:5957 "gives no better get-up here (more attempts lost) ... Left at 0" |
| `getup.rest_max_s` | 0 | The bound on that rest; 0 none (BT:1054) | BC:2741 `GetUpRestS` (only with `rest_per_ns_s`) | None | R6 | As above | Never switched | As above | As above |
| `getup.hold_by_body` | 1 | A hold takes the carry's springs off its chain and slows the clip by its force; 0 R1's hold, which takes the pull off the whole body (BT:1073-1086) | BC:4458 `UpdateGetUp`, 4731 `UpdateCarry` | None | No | No | Decision 32; confirmed by the owner, 27 Nov | R2 step 3 (T:5522-5538): one hand 32 of 150 pull free (119 off), two hands 2 (14), pressed 52 (50); since (T:6137) 29, 0, 53 | Decision 14: "Not chosen: the clip waits while held ... (the pauses the owner called robotic)" (AR:736) |
| `getup.hold_chain` | 3 | Which bodies a hold takes the springs from: 0 the held body, 1 its chain to the pelvis without it, 2 with it, 3 and what hangs from the held body (BT:1075-1077) | BC:4794 `UpdateCarry` | None | No | Decision 31 "to confirm"; "Each is a setting, so that the owner can compare" (AR:754) | Taken for the owner, decision 31 (c) | R2 step 3's screen on seed 1, 26 falls (T:5513): one hand pulls free, mode 0 25, 1 25, 2 24, 3 13 | AR:754 "Not chosen: ... the chain without the pelvis or the held body alone (25 of 26 pulled free)" |
| `getup.hold_chain_falloff` | 0 | Each further body of a chain counts this much less (BT:1077) | BC:4794 `UpdateCarry` | None | No | Decision 31, to confirm | Decision 31 | A screen only (docs/recon/r2_step3_plan.md:89): at 0.3, 25 of 26 pulled free | AR:754 "Not chosen: ... a falloff along the chain (25 of 26)"; but T:5590 says "not tried" |
| `getup.hold_chain_share` | 0 | What a fully held body keeps of its springs (BT:1078) | BC:4803 `UpdateCarry` | None | No | Decision 31, to confirm | Decision 31 (d) | Never varied | No |
| `getup.hold_slow_from_n` | 0 | The clip's pace falls from this force on (BT:1081) | BC:4462-4463 `UpdateGetUp` | None | No | Decision 31, to confirm | Decision 31 (a) | Never measured | By reasoning only: "Not chosen: a threshold of 100 N or more (...)" (AR:754) |
| `getup.hold_sum` | 0 | The holds added as vectors (0) or by their sizes (1) (BT:1083) | BC:4826 `HoldPaceForceN` | None | No | Decision 31, to confirm | Decision 31 (b) | Mode 1 never run | By reasoning only: "the sizes of the forces added (...)" (AR:754) |
| `getup.hit_by_impulse` | 1 | A hit while led takes `impulse / carry_yield_ns` of the pull, given back over `hit_back_s`; 0 a hit of `carry_yield_ns` gives the attempt up (BT:1095-1102) | BC:4329 `UpdateGetUp`, 4738, 4753 `UpdateCarry` | None | No | No | Decision 35; confirmed by the owner, 27 Nov | R2 step 4 (T:5608-5623): `getup_hit_strong` thrown 43 cm off its clip and falls (off: given up by the threshold); a push of 30 N*s, the chest 2.4 cm off against 2.1; the stand as before (T:6146) | Decision 20: "Not chosen: the threshold of 60 kept ..." (AR:742) |
| `getup.hit_overflow` | 1 | What a hit takes beyond the whole share is owed (BT:1110-1111) | BC:4336 `UpdateGetUp` | None | No | Decision 34 "to confirm" (AR:758; PRD:1217) | Taken for the owner, decision 34 | T:5613-5615: at 0 with a clock of 1.5 s or less the world puts a body hit with 200 N*s back up; at 1 it falls 0.17 s after the blow at every clock | AR:758 "Not chosen: a clock of 2 to 3 s (...)" |
| `getup.start_blend_s` | 0.3 | A get-up's start crosses from the body's own pose to the clip's first pose over this; 0 the first pose taken whole (BT:1104-1109) | BC:2353, 2380 `StartClip` | None | No | No | Decision 36; confirmed by the owner, 27 Nov | T:5799-5806: 0 / 0.3 / 0.6 s: 139 / 138 / 141 at the first attempt, 10.7 / 11.6 / 14.1% given up, C19's held pose 0.59 s to 0.30; the largest target jump 105 to 75 degrees (T:6117) | AR:761 "Not chosen: 0.6 s now (more attempts lost)" |
| `getup.roll` | 1 | A body on its side rolls onto its back or front by its own muscles first; the matcher leaves out the side get-ups (BT:1136-1144) | BC:2093 `StartGetUp`, 2609 `ChooseByMatch` | None (`getup_roll_*` run it on) | No | No | Decision 50; on by decision 52 | R2 step 9 (T:6347-6355): on against off, 12.2% given up (9.1), C27 150 of 150 (90), C21 2.53 to 2.60 s (2.60 to 2.97), poses over 1 s 5 (8); with all of R2's rest (T:6482) 7.4%, 150 and 143, C21 3.20 s, C27 22 degrees and 28 cm | No |
| `getup.roll_way` | 0 | 0 by where the front looks, 1 always onto the front, 2 always onto the back (BT:1140-1141) | BC:2155 `StartRoll` | `getup_roll_right_back`=2 | No | No | Never switched | No stand by mode; `getup_roll_right_back` rolls to the back in 0.52 s (T:6338) | No |
| `getup.fit_surface` | 1 | The clip's floor is what the pelvis lies on, not the floor last stood on (BT:1157) | BC:2288 `StartClip` | None | M4b: "what a body on furniture should do is M4b's" (AR:779) | No | Decision 52 | R2 step 10 (T:6393-6401): the three fits 7.4% given up, C27 148, authority 1.12; without them 10.8%, 144, 1.13; each alone worse than none | AR:779 "Not chosen (52): (b) everything but the fit to the surface (`fit_surface` 0 ...: 11.2% given up and 2 poses of 2.98 s, the gate not met ...)" |
| `getup.fit_floor` | 1 | That share of the library's fit of each key to the floor raises or lowers the reference (BT:1158-1159) | BC:4368-4388 `UpdateGetUp`, 4671, 4694 `UpdateCarry` | None | No | No | Decision 52 | As `fit_surface` | No |
| `getup.fit_free_z` | 1 | A body the recording has on its floor that touched the world gets no vertical spring from the carry (BT:1160-1161) | BC:4764 `UpdateCarry`, 8285 `TickComponent` | None | No | No | Decision 52 | As `fit_surface`; "`fit_free_z` changes little anywhere" (T:6389) | No; a known cost, it takes a foot's vertical spring away in a step (T:6529) |
| `getup.drag` | 1 | Holds that drag the body give a get-up up, the world lets go, and the next waits `drag_free_s` (BT:1167-1172) | BC:2766 `CanBeginGetUp`, 8301 `TickComponent` (and `UpdateDrag`) | None (`getup_drag_*`, `grab_drag_fallen_*` run it on) | R6: "dragged by a limb on its muscles alone" (AR:542); R10's struggle | The pressed stand's 98 of 150 "for the owner's eye" (T:6496) | Decision 18; on by decision 52 | R2 step 11 (T:6423-6428): the grips hold 230 to 257 frames, the world gives 0 to 7.8 N*s against the hands, the pelvis dragged 1.0 to 2.0 m; off (decision 18), the grips slip within 0.7 to 1.9 s | No |
| `getup.arm_answer` | 1 | A held arm while getting up: 0 nothing, 1 by the body's state, 2 always yields, 3 always pulls in (BT:1177-1184) | BC:3450 `UpdateLimbClaims`, 4116 `UpdateHeldArms`, 8335 `TickComponent` | `getup_pull_arm_in_crouch`, `_sit`=3; `getup_pull_arm_yield_crouch`, `_sit`=2 | Decision 19: standing in R5, lying in R6 (AR:741) | No; but "by state" yields in every scenario, nothing braces a rising body outside `-Perception` (T:6441) | Decisions 19 and 48; on by decision 52 | R2 step 12 (T:6447-6454): sitting up, the holding hand lags 65.6 / 51.7 / 85.4 cm (off / yields / pulls in); since (T:6533) 50 cm (66 off) | No |

## Body: surface (`surface.*`)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `surface.friction_combine` | 0 (average) | The combine mode the body's surface asks for: 0 average, 1 smaller, 2 product, 3 larger (BT:88-104) | BC:7004 `ApplySurface` | `tune_live_surface`=1 | No | No | Never switched (the engine's own mode) | M3 part 7 step 2 (T:1988-1997): a friction of 0.3 reads 0.50 averaged, 0.30 by the smaller | No |
| `surface.restitution_combine` | 1 (smaller) | The same for restitution | BC:7005 `ApplySurface` | None | No | No | Accepted by the owner, 24 Nov (PRD:887) | T:2019-2074: "no run could tell 0, 0.1 and 0.3 apart beyond noise" (T:2059) | No |

## Poses (`poses.*`)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `poses.fingers.weight` | 1 | How much of the finger poses the fingers take (outside the physics) (BT:556-557) | BC:1479-1524 `UpdateFingers` | None | R3: "`splayed` with the palms to the floor" (AR:470) | "`splayed` in a fall shown with R3" (AR:340) | The owner, decision 53 | N8 B (T:4420): the bodies' hashes the same at 0 and 1; R2's rest switched on (T:6518) | No |
| `poses.idle.weight` | 1 | How much of the idle clip's turns of trunk, neck and arms the body carries (BT:643-644) | BC:1434, 1444 `UpdatePoseTurns` | 12 set it to 0: `measure_arm_at_cap`, `_arm_held_out`, `_cop`, `_drop_head_first`, `_fast_target`, `_head_turn`, `_jumps`, `_push_neck`, `_reaction`, `_still`, `pose_hand_place`, `_below` | R9: "breath, sway, tone" from physics (AR:567) | No | The M3 audit, 24 Nov, no decision | T:1290-1318: the head travels 4.1 cm (0.5), the assist 0.0067 (0.0048; bound 0.0093), the matrix 160 both | No |
| `poses.idle.sway_weight` | 1 | How much of the clip's sway of the pelvis the whole body leans by (BT:645-646) | BC:1738 `UpdateIdle` | The same 12 set it to 0 | R9 (AR:567) | No | As `idle.weight` | As `idle.weight`; never measured apart from it | No |

## Effectors (`effectors.*`)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `effectors.hand.palm_collides` | 1 | The open hand is a second body, a plate that collides in the ball's place (IT:56-65) | SandboxEffectorComponent.cpp:651 `Update`; recorded :500 | 8 set it to 1, the files' own value: `grab_palm_lift_arm`, `palm_close_on_chest`, `palm_open_close`, `palm_open_on_chest`, `palm_push_chest_both`, `palm_slap_head`, `palm_slap_head_edge`, `palm_slap_leg` | M4's rest; the player's hands with fingers after the audit (audit_plan.md:6) | No; PRD:1030 still says it waits for the owner's look (stale: on since 25 Nov, T:3641) | On 25 Nov after the owner's second look (T:3580; commit c5695d8) | T:2812-2817: 30 slaps, no tunnelling, the deepest 0.24 cm; no criterion | No |
| `effectors.hand.grab` | 1 | A hand whose grip closes within reach of a body takes hold of it (IT:66-74) | SandboxEffectorComponent.cpp:634 `Update`; recorded :499 | 16 set it to 1, the files' own value (`grab_*` nine, `prop_drop_box_on_head`, `prop_lift_box`, `prop_lift_heavy_one_hand`, `_two_hands`, `prop_stick_blow_torso`, `prop_stick_strike_arm`, `prop_throw_box`); `palm_close_on_chest`=0 | M4's rest: the yank, the pull along, the lift, the throw (AR:286) | No | The owner, 25 Nov (T:3656; commit c6937be) | T:3656-3662: 101 of 102 scenarios kept their hashes (`palm_close_on_chest` moved) | No |
| `effectors.hand.strike_held` | 1 | A thing held in the hand strikes as a fist does (IT:105-112) | SandboxEffectorComponent.cpp:923 `UpdateHeldThing`, 959 `OnHeldHit` | None | M4's rest, part 4 (things in the hand, AR:284) | "the hold is tuned after the owner's ten minutes with a stick in a session" (T:4116; PRD:1132) | The owner, 25 Nov, option A (T:4116; commit 3def323) | T:4102-4116: a stick's blow 15 to 23 N*s off, 20 to 30 on (one 6); the head (162 cm) misses M4's stick criterion | No |

## Tools (`tools.*`)

| Name | Now | What it does | Read by | Scenarios | Named by a later part? | Waits for the owner? | Set by | Latest on vs off | Rejected in words? |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| `tools.headset.runtime_velocity_share` | 0 | A tracked hand target's velocity: the difference of two poses (0) or the headset runtime's own (1) (IT:205) | SandboxTrackedTargetSource.cpp:85-88 `UpdateHand` | None | M4's part 6, the review in the headset (AR:286) | "The owner's eye in the headset has the last word" (PRD:1076) | The owner's choice, night of 24 to 25 Nov (T:2699) | T:3837-3839: the hand's lag 0.5 to 5.7 cm by speed (the runtime's 0.8 to 12.4), its tremble 3.1 to 3.3 cm/s (3.7 to 3.8) | No |
| `tools.headset.vignette` | 0 | Darkens the view's edges by the stick's speed (IT:239) | SandboxPawn.cpp:524-527 `UpdateHeadset` | None | M4's part 6 | Decision 24 (b): "for the owner's first session to judge" (AR:746); "Not yet tried in the headset" (T:5307) | The owner, decision 24 (b) | Never measured | No |

## 1. Tuning leaves no code reads

**None.** Every name `VisitValues` gives (`SandboxBodyTuning.cpp`, `SandboxInteractionTuning.cpp`; 428 `Visitor` calls, the
`%s` ones over their groups) has a reader outside the tuning files and the tests, and every leaf of the four files is a name the
code visits: the numbers by `VisitValues`, the text values (`rig.*`, `fall.clip_reach_*`, `motion.library`,
`poses.idle.animation`) by the visitor of text (SandboxBodyTuning.cpp:432-437, 531-536), `states.*` by the states' overrides.

What is read but has no effect with the files as they are (they belong to a switch at 0, their comments say so):

- `fall.aim`'s nine values (`reach_ahead_cm`, `hand_height_cm`, `reach_blend_s`, `way_follow_s`, `arm_reach_*`, `reach_back`,
  `arm_yield_*`); `fall.blend`'s seven (`blend_share`, `blend_follow_s`, `blend_clip_s`, the four `clip_reach_*`);
  `arm_curve`'s seven (`arm_brace_from_s`, `arm_far_*`, `arm_brace_*`, `arm_touch_*`); `catch`'s eleven (`catch_*`);
  `head`'s six (`head_*`); `curl_below_s`, `curl_from_ns`; `sit`'s three; `turn`'s three; `palm_share`; and the poses
  `poses.fall_curl`, `fall_sit`, `fall_turn`.
- `behaviour.struggle`'s eleven (`struggle_*`).
- `perception.gaze_*` (7), `anticipate_*` (2), `guard_*` (4): used only in a session with `-Perception` or a scenario that
  switches them on (`perception.react_s` is not among them: the commands, the staying down and the struggle read it too).
- `muscles.weight_legs_by` and `balance.ankle_as_torque` while every `weight_share` is 0: `UpdateWeight` returns first
  (BC:7123-7127).
- `tone.brace_delay_s`, `brace_hold_s` (the brace is never asked for, `brace_from_ns` 0) and every tone level (all 1).
- `getup.rest_max_s` (with `rest_per_ns_s` 0); `tools.headset.vignette` itself.

## 2. Scenarios that exist to test a switch at its off value, or a feature that is off

At the off value (the files have it on):

- `command_refused_off` (`behaviour.commands`=0), `command_give_hand_off` (`behaviour.give_hand`=0),
  `getup_hit_strong_protect_off` (`behaviour.protect`=0), `palm_close_on_chest` (`effectors.hand.grab`=0).
- Both values, live: `tune_live_protect`, `tune_live_stay_down` (0, then 1 in the run).

A feature that is off, switched on by the scenario itself:

- `behaviour.struggle`=1: `drag_lying_ankles_struggles`, `drag_lying_wrists_struggles`, `struggle_phase_hit`, `_hold`, `_push`
  (their controls with the struggle off are `drag_lying_ankles_still`, `drag_lying_wrists_still`, which run the files).
- R3's variant A (`fall.catch`, `arm_curve`, `head`, `palms`=1): `fall_back_r3`, `fall_catch_forward`, `fall_catch_side`,
  `fall_phase_catch_hit`, `_hold`, `_push`.
- `perception.gaze`=1: `gaze_follows_hand`, `viewer_with_gaze`.
- `reactions.reflex_delay_s`=0.08: `react_two_hits`, `react_unseen_chest_30`.
- A mode the files do not use: `getup_roll_right_back` (`getup.roll_way`=2), `getup_pull_arm_in_crouch`, `_sit`
  (`getup.arm_answer`=3), `getup_pull_arm_yield_crouch`, `_sit` (`arm_answer`=2).
- Perception, which judges only for a body that asks (`"perception": true` on the body): `perceive_hand_behind`, `_front`,
  `_hover`, `fear_threat_rises` (and the two gaze scenarios above).

Not tests of a switch: the `measure_*` and `pose_hand_place*` scenarios set the assist, the idle and some muscles to 0 to
measure the body; `stand_foot_moved`, `tune_live_*` of the solver, the surface, the effector and the files test the tuning's
own path.

## 3. By the four conditions

**Meet all four** (rejected in words or measured worse with nothing open; waiting for nobody; named by no later part; better on no
criterion):

- `balance.ankle_side_gain` (the sideways ankle strategy): "Sideways the ankles made things worse" (T:1105), the capture margin
  worse at every gain tried; no part names it.
- `fall.catch_back` (the catch in a fall backward): turned down in decision 55, confirmed by the owner (37 to 50 J over the 40);
  no better figure is written. The catch it belongs to waits for decision 58.
- `behaviour.lower_end_cm` above 0 (the lowering ended by the chest's height): turned down in decision 59, confirmed; the kneeling
  head came down faster at every height tried.
- The 0 branch of `getup.hold_by_body` (R1's hold) and of `getup.hit_by_impulse` (the threshold of `carry_yield_ns`): both
  replaced by the owner's confirmed decisions 32 and 35, each the not-chosen alternative of decisions 14 and 20, each worse on its
  own gate (one hand 119 of 150 pull free against decision 13's 20 to 60; a blow of 200 N*s given up by a threshold instead of the
  mechanics).

**Candidates** (rejected or measured worse, but waiting for the owner or named by a later part, so not the audit's to delete):

- Waiting for the owner: `fall.aim` and `reach_back` ("until the owner's word"); `getup.hold_chain_falloff`, `hold_sum`,
  `hold_slow_from_n` and the alternative modes of `hold_chain` (decision 31, to confirm); the 0 branch of `getup.hit_overflow`
  (34, to confirm); `getup.rest_per_ns_s` and `rest_max_s` (the owner's eye; also R6); `perception.anticipate` (decision 12, R4,
  R5).
- Named by a later part: the 0 branch of `getup.fit_surface` (M4b), of `getup.drag` (R6) and of `getup.arm_answer` (R5, R6);
  `balance.settle_smooth` (R4; "until it is understood"); `muscles.*.weight_share`, `balance.ankle_as_torque`,
  `assist.internal_share`, `muscles.joints.*`, every `tone.*` level, `tone.brace_from_ns`, `reactions.reflex_delay_s` (R4).
- Better on a criterion, so condition 4 fails: `fall.catch_let_go_at_trunk` at 1 (no target turned over); the 0 branch of
  `fall.catch_joint_blend` (turned down only "until its flips are found and mended"); the brace of `fall.arm_far_*` and
  `arm_brace_*` (C14 by hand 83%; also part of R3's curve); the 0 branch of `getup.start_blend_s` and of `getup.roll` (fewer
  attempts given up); the 0 branch of `reactions.thing_support` and `hold_felt` (a first attempt or a fall fewer).
- Kept by the owner's own word: `fall.curl`, `curl_from_ns`, `sit`, `sit_leg_stiffness`, `turn` (decision 57, confirmed: "Not
  chosen: dropped (the switches cost nothing at 0)"); `fall.curl` is also C38's curl (R5, R6), and the sit and the turn are R3's
  own design.

**Not by the conditions** (nothing rejected or measured against): `fall.catch_max_torque_scale` and `muscles.*.pose_velocity_share`
(never measured as they are; the second's mechanism is on for the swinging leg), `muscles.weight_legs_by`, `getup.roll_way`,
`fall.blend` and `blend_share` (decision 37, "to be worked on"), `behaviour.struggle` (decision 72), every switch at 1 not named
above, the surface's modes, the poses', the effectors' and the tools'.

## What the audit did with it (28 November 2026; decision 78)

Every row above was read against the four conditions and the owner's rule for switches that are on ("folded into the behaviour
without a switch only what the owner switched on by the owner's own decision; what is on under the delegation stays a switch").
Each change keeps every hash of `audit_before`; each can be brought back from commit `c79519c` (the tree before the audit).

| Name | What was done | Why | Back from |
| --- | --- | --- | --- |
| `balance.ankle_side_gain` | Taken out: the value, its field, the sideways term of the ankle's shift (now 0 in the code, as the value was) | All four conditions: "sideways the ankles made things worse" (M3 part 3), named by no later part, waits for nobody, better on nothing | `c79519c`: `SandboxBodyTuning.*`, `SandboxBodyController.cpp` `UpdateStrategies`, `body.json` |
| `behaviour.lower_end_cm` | Taken out: the value, its field, the lowering's end by the chest's height (the lowering ends at the clip's start, as at 0) | All four: decision 59 turned the heights down (the kneeling head at 1.4 to 2.9 m/s), confirmed by the owner; named by no later part | `c79519c`: the same files, `UpdateGetUp` |
| `getup.hold_by_body` | Folded: decision 14's hold is the only one, R1's hold of the whole body taken out | Switched on by the owner (decision 32, 27 November); the 0 branch meets the four conditions (decision 14's not chosen; one hand 119 of 150 pull free against decision 13's 20 to 60) | `c79519c`: `UpdateGetUp`, `UpdateCarry`, the tuning's check |
| `getup.hit_by_impulse` | Folded: decision 20's hit is the only one, the threshold that gave an attempt up taken out (`ESandboxGiveUp::Hit` is kept in the counts, never given now) | Switched on by the owner (decision 35, 27 November); the 0 branch meets the four conditions (decision 20's not chosen) | `c79519c`: the same |

**Candidates for the owner's decision** (not taken out; the report of the audit lists them with a recommendation):

| Name | Why it stays | Recommendation |
| --- | --- | --- |
| `fall.catch_back` | Meets the four conditions alone (turned down by decision 55, confirmed), but it is a part of R3's catch, whose decision (58) is open | Take out once 58 is decided |
| `fall.aim` and its nine values (`reach_back` among them) | "Until the owner's word"; R3 is to replace it | Take out when R3's catch goes on (58, A); keep while 58 is "not yet" |
| `fall.curl`, `curl_from_ns`, `sit`, `sit_leg_stiffness`, `turn` | Decision 57 kept them by the owner's word; the curl is C38's (R5, R6) | Keep the curl; take out the sit and the turn (no figure better, named only by R3's own first design) |
| `getup.hold_chain`'s other modes, `hold_chain_falloff`, `hold_sum` 1 | Decision 31, to confirm | Confirm 31; then take out the falloff and `hold_sum` 1 (never chosen; the falloff 25 of 26 pulled free) and keep `hold_chain` as a value |
| `getup.hit_overflow` 0 | Decision 34, to confirm | Confirm 34 and fold it (at 0 a body hit with 200 N*s was put back up by the world) |
| `getup.rest_per_ns_s`, `rest_max_s` | Measured worse (more attempts given up) but named by R6 ("lying longer after a harder fall") | Keep for R6 |
| `perception.anticipate` | Its A2 missed; R4 and R5 name it (decision 12) | Keep for R5 |
| R4's set: `muscles.*.weight_share`, `balance.ankle_as_torque`, `assist.internal_share`, `muscles.joints.*`, every `tone.*` level, `tone.brace_from_ns`, `reactions.reflex_delay_s`, `balance.settle_smooth`, `muscles.weight_legs_by`; the scenario's `tone` event | Named by R4 | Keep for R4; decide each at R4's plan |
| `states.get_up.assist.*` (9 values) | Act only while a get-up is not led, which is now only the tick a carry that cannot be made is given up (`AssistStateEase`); a remnant of the clip-following path, but not a value without any effect | Take out with that path's last branch, checked by the hashes (expected: none move) |
| `behaviour.fear`, `stay_down`, `protect`, `commands` | On by the owner (decision 67) on 28 November, but their videos wait for the owner's look | Fold into the behaviour after the owner has seen the videos |
| `getup.roll`, `fit_surface`, `fit_floor`, `fit_free_z`, `drag`, `arm_answer`; `balance.lift_smooth`, `release_smooth`; `reactions.thing_support`, `hold_felt` | On by the owner, but the 0 branch is named by a later part (M4b, R4, R5, R6) or better on a criterion | Keep as switches until those parts |
| `effectors.hand.grab`, `palm_collides`, `strike_held` | On by the owner; the player's hands (section 8, item 4) change the hand | Keep until the player's hands |
| `behaviour.give_hand`, `protect_held_leg`, `command_cover_react` | On under the delegation (71, 73, 74) | Keep as switches (the owner's rule) |
| `behaviour.struggle`, `fall.blend`, R3's `catch`, `arm_curve`, `head`, `palms`, `catch_let_go_at_trunk`, `catch_joint_blend` | Decisions 72, 37 and 58 are the owner's | The owner's |
| 27 scenario events that set a value the files have (`effectors.hand.grab` 1 in 16, `palm_collides` 1 in 8, `thing_support` 1 in 3) | They keep each scenario on its feature whatever the files say | Keep |

## Found on the way

- 27 scenario events set a value the files already have: `effectors.hand.grab`=1 in 16 scenarios (1 in the files since 25 Nov,
  commit c6937be), `palm_collides`=1 in 8 (since c5695d8), `reactions.thing_support`=1 in 3 (since 1ed08d0). Whether taking them
  out keeps the hashes was not checked (each event counts as a tuning change, and `expect.tuning_changes` may name it).
- A scenario's `"tone"` event and `USandboxBodyController::RequestTone` exist, no scenario uses them, and with every level at 1
  they would change nothing.
- PRD:1030 still says `palm_collides` waits for the owner's look; it has been on since 25 November (T:3641).
- T:5590 says a falloff along the chain was "not tried"; AR:754 rejects it with "25 of 26" (the screen of r2_step3_plan.md:89).
- `getup.arm_answer` 1 ("by the body's state") yields in every scenario: nothing braces a rising body outside `-Perception` (T:6441).
