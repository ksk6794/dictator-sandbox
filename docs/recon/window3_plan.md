# The third unattended window of 28 November 2026 (the owner away)

Start 16:22:34 (Get-Date), end 22:22:34 (start + 6 h). The last 60 minutes, from 21:22:34, are only the close. The clock is read
at every change of stage and written here, from `Get-Date` only.

The owner's task of 28 November (evening): (1) the two breaches of the authority rule the audit found (`docs/testing.md`, "The
audit"), behind one switch at 0, measured, with a recommendation; (2) item 4 of `docs/active-ragdoll.md`, section 8, the player's
hands (decision 76): visible hands with fingers behind a switch at 0, no collision, every hash kept; then fingers that close around
what a grab holds; then the physics of the fist and the palm fitted to the visible hand behind a switch of its own, measured; (3) the
close. Everything new behind a switch at 0, every hash of `audit_before` kept; a switch goes on only with measurements, as "taken for
the owner". The audit's candidates are not touched (the owner answers tomorrow). Decisions that would be the owner's are taken under
the owner's delegation (the recommended option by the data and the goal), written into section 10 from 79 on with the options turned
down. Not delegated: the headset (no `-VR`, SteamVR not touched), push, purchases and paid content, commits (only on the owner's
command). After every step its patch in `Saved\Backups\` (`git diff --output=...`).

## Stages

| Stage | What | Read of `docs/pitfalls.md` |
| --- | --- | --- |
| 1 | The authority rule after a hit and after an attempt given up afraid, behind `getup.strict_release` (0): (a) a hit that makes a rising body cover itself takes the world's pull by its impulse before the attempt is given up; (b) a hit takes the pull in every state while it fades; the marks of a give-up cleared as an attempt begins (level 1); (c) a body that lies keeps the pull only for the get-up's own release (level 2, added at 17:02). Measured: the suite (what moves and why), the stand on three seeds, `-Near 70`, C33, C34, C39, a film pair; a recommendation | falls and the get-up; measuring, runs and tests; PowerShell, scripts and building |
| 2a | The player's hands visible: the engine's `XRMannequins` hands copied by script (`Tools\fetch-stock-content.ps1`'s way), a posed mesh on each hand behind `effectors.hand.mesh` (0), no collision, the fingers' poses in C++ (open, fist, a blend by the grip); orientation checked by pictures | hands, the grab and blows; assets, the MetaHuman, maps and lighting; the session, recordings and the headset |
| 2b | Fingers that close around what a grab holds (a scene query that reads only), behind its own switch at 0 | hands, the grab and blows; measuring, runs and tests |
| 2c | The physics of the fist and the palm fitted to the visible hand, behind a switch of its own at 0; measured (the strikes, the matrix, `swing-lag`, `grab-pulls`, `record-replay -Takes`/`-Walk`) | hands, the grab and blows |
| Close | build, tests, the suite against `audit_before`, `bench.ps1 -Hashes -Label w3 -Against audit_before`, check-links, bootstrap; an independent reading of the whole diff (the code, not the account); the Claude Docs tabs; the memory; the report in Russian | |

## Journal

- 16:22:34 start, tree clean at `4a4b873`; the documents read (CLAUDE.md, PRD Status, section 8 item 4 and section 10 to 78, the audit's
  plan, the register's last section, `docs/testing.md` "The audit", the memory); the pitfalls' areas of the stages read.
- 16:26 stage 1 built: `getup.strict_release` (`TakeCarryByHit` made of decision 20's arithmetic, called by the get-up's flinch as before,
  by a cover that gives an attempt up with the hit no flinch judged yet, and by a hit felt while the pull fades; `ClearGiveUpMarks` as an
  attempt begins; the Lying clock). 16:29 build OK, 67 tests, the suite 220 of 220 with every hash of `audit_before` at 0;
  `Saved\Backups\w3_step1_strict_release.patch`. With it on, 15 scenarios move (all of the expected kinds), two fail (the head down at 2.13
  and 2.08 m/s against the scenarios' 2.0, `protect_hit_rising_head`, `_trunk`). 16:33 the pool; 16:34 to 16:52 the measurements
  (`docs/recon/window3/step1_measure.ps1`: the check set on, then the seeds with a viewer, with get_up, pressed, held by one hand, each off
  and on).
- 16:37 stage 2a begun while the runs went: `Tools\fetch-stock-content.ps1` copies the XR hands (27 files to `Content\XRMannequins`, ignored
  by git); their bones read by the editor's Python (`docs/recon/window3/dump_xr_hands.py`): both meshes have all 47 bones of one
  skeleton, the wrist at the origin, the fingers along +y, the right thumb toward +x, the palm toward -z.
- 16:40 to 16:41 **my mistake:** `Config\Tuning\effectors.json` given the new hand values while the seeds ran (the pitfall "a tuning file
  edited while runs go"); taken back within a minute; five runs of `w3s1_near_on` logged the refused reload (their values as before);
  that stand is measured again on the new build.
- 16:53 build (one C4458 put right), 68 tests (the hand rig's), the suite at 0 with every hash of `audit_before`, and with
  `effectors.hand.mesh=1` every hash the same too (220 runs: the mesh has no collision). Pictures (`docs/recon/window3/hands_look_*.json`):
  the hands on their frames, the palms facing each other, the fingertips along the fingers' way; the thumb of the first fist pointed at
  the other hand, so it folds toward a point in front of the middle knuckle now (`FSandboxHandRig`); the fist then a fist.
- 17:02 `getup.strict_release` split in two levels (1: the hits and the marks; 2: also the Lying clock), after the first figures: with the
  Lying clock the lowered body's head came down at 2.36 m/s on seed 1 (1.86 at 0). The levels measured in the background.
- 17:03 to 17:14 stage 2b while the levels ran: `effectors.hand.mesh_wrap` (`WrapShares`: each finger by six halvings against the held
  body's `OverlapTest`); the hand holding a forearm closes four fingers to 0.51 to 0.54 of a fist. Every hash the same with the mesh and
  the wrap on (220 runs); `play.ps1 -Smoke -Shot` with both on: 15 of 15 and the two fists in view. `play.ps1 -Demo -Grab -Shot` made no
  picture and said nothing (now refused; a pitfall).
- 17:12 the levels' figures: at 1, C33 and C39 keep every hash, the suite moves ten (two fail on the head's 2.0 m/s), the scorecard the
  same; at 2 the lowered head 2.36 m/s again (the stand run again after my mistake gave the same figures). Decision 79 written for the
  owner, level 1 recommended; film pairs `protect_hit_rising_trunk_pair`, `getup_hit_strong_pair`. A sweep for option (c): the cover's
  trunk at 400 N*m/rad brings the heads to 1.95 and 1.99 m/s (on the edge), its neck changes nothing.
- 17:15 to 17:27 stage 2c: `effectors.hand.mesh_fit` (the ball and the plate from the mesh's bones); the open hand made straighter
  (-22, -14, -8 degrees) so that its plate is not 6 cm thick; measured (`docs/recon/window3/step2c_measure.ps1`): 80 scenarios move, two
  fail, the takes' replays 1 of 3, the walk's hands 10 cm behind; kept at 0 (decision 82); `record-replay.ps1 -Tune` added for it. The
  walk and the seeing session with the mesh and the wrap on: OK, the seeing replay's hashes `194c0969` as at 0.
- 17:30 films of the hands (`docs/recon/window3/hand_films.json`: open and fist from the NPC's side, a forearm held, the palm on the chest,
  a punch with the knuckles first), a stick taken by its handle; the documents (PRD Status, section 8, decisions 79 to 82, testing,
  pitfalls, CLAUDE.md, the tuning README); `LINKS OK`; an independent reader of the code launched at 17:30.
- 17:33 the owner's page of the window: https://claude.ai/artifact/TJ2vJpkxX91SnU5azFpEtH (the two film pairs, the hands' four films, five
  pictures, the figures of decision 79). 17:36 the blows with the fit (knockdowns by two fists 8 of 8 either way; the owner's punches of
  25 November replayed, the first hits within 2 to 12%).
- 17:40 the reader back: nothing changes at 0 (the refactor exact, no hit taken twice, the marks cleared after every failed start).
  Put right: a hit the get-up's flinch judged in a tick was lost when the attempt ended later in the same tick (a leg's cover come due,
  an afraid give-up, a drag): `GiveUpGetUp` takes it at 1; the fade's hits no longer lower the get-up's `hit_free_min`; the fingers are
  posed every frame (angles tuned at run time showed only when they next moved); the test's message read after the build; `play.ps1`
  refuses `-Shot` before the headset's checks (which stop the pool); `fetch-stock-content.ps1` gives a verdict when a copy throws;
  `record-replay.ps1 -Tune` with `-Recorded` documented (the replay alone). Noted, not changed: level 2's Lying clock makes
  `lower_release_s` short for a body that hangs still on the pull (that is the 2.36 m/s measured; level 2 not recommended); a hit by a
  movable thing takes the fading pull as it does while led; `mesh_fit`'s plate stays on the hand's axis (kept at 0); `WrapShares` runs up
  to about 200 shape tests a hand a frame while holding. 17:43 build OK, 68 tests, `FETCH OK`, `play.ps1 -Demo -Grab -Shot` refused;
  the suite at 0 and level 1 again in the background.
- 17:49 after the fixes: the suite at 0 with every hash of `audit_before`; level 1 gives the same hashes as before the fix in the suite,
  the viewer's stand and the stand held by one hand (the path did not come up); the stand with movable furniture: every hash the same at
  0 and 1. 17:52 the VR session's page (https://claude.ai/artifact/XatvKGwU2Ru7fJ3KE1ASYh) given the hands: the launch with them, two rows,
  a first step "look at your hands", C39's figure after decision 74. The memory brought up to date.
- 17:55 to 17:59 decision 79 on 90 hits while rising (`docs/recon/window3/rising_hits.ps1`, one sweep at 0 and 1, then with
  `fall.head`): at 0 the world holds the falling body with 2.25 body weights at the peak in the median, and the head is over 2.0 m/s in 44
  of 70 already; at 1 the pull goes (none after 120 N*s) and the head lands harder after the strong blows (over 3.2 m/s in 13 of 30 after
  120 N*s, 6 at 0); `fall.head` changes nothing there. Decision 79, testing and the page say so.
- 18:01 **my mistake:** the first film pair of decision 79 had no `-Tag` and wrote over decision 58's pair of the same scenario (R3 0
  against A), which the documents name; that pair made again with its own settings, the window's pairs made again as `w3_strict_trunk`
  and `w3_strict_strong`; a pitfall.
- 18:02 the close's checks begun early (the code done): `build.ps1 -Rebuild` OK (100 s), 68 tests, the suite 220 of 220 with every hash
  of `audit_before`, `LINKS OK` (after two names marked as not scenarios). 18:05 to 18:19 `bench.ps1 -Hashes -Label w3 -Against
  audit_before`: SAME, 1769 runs and files, 0 changed, the verdicts the same in 25 of 25 tools. 18:05 to 18:10 the Claude Docs tabs
  ("Tab 1": the Status rows, the waiting list, the tests; "Active ragdoll 2": section 8's items 3 and 4, decisions 79 to 82).
- 18:21 R3's `fall.curl` on the 90 hits while rising: every figure the same at 0 and 1; no behaviour of R3 reaches a fall from a get-up
  given up (a gap for R3 or R6, written into decision 79). 18:24 the bootstrap and a last independent reader of the whole diff.
- 18:25 the bootstrap OK, 16 of 16 in 169 s. 18:34 the last reader back: nothing changes a run at 0. Put right: the README's open-hand
  angles (-15, -10, -5 where the files have -22, -14, -8) and the wording of `strict_release` (the unjudged part of a hit); a roll takes
  no hit at 1 either (`StartCover`, `GiveUpGetUp`: nothing of the world pulls on a body that rolls); the fitted sizes held to the files'
  ranges; the angle table written once (`CurlsAt`), `FSandboxHandCurls::Blend` (no caller) taken out; the after-review script without a
  pipe to `Select-Object -First`; the plan's names; `-Tag` in CLAUDE.md's film-pair row. Left, written: a hit under 5 N*s across a
  give-up from outside FeelFrame is not carried into the fade's sum; `fetch-stock-content.ps1` fails when the engine has no XR pack
  (the unit test needs it anyway). 18:35 build OK, 68 tests, the suite at 0 with every hash of `audit_before`, with the mesh and the
  wrap the same, level 1 the same hashes as before the fixes; the fists' picture the same.
- 18:39 option (c) on the 90 hits: a stiffer cover (trunk 400 or 800, neck 120) changes nothing (15 to 19 over 3.2). 18:42 decision 58's
  A on the 90 hits: the catch does reach these falls (the head's median 1.84 at 0 and 2.06 at 1, 5 and 10 over 3.2), the cover later.
  **My mistake:** at 18:21 I wrote that no behaviour of R3 reaches these falls from `fall.curl` alone (and `fall.head`, whose figures did
  move); put right in decision 79, testing, the tab and the page.
- 18:47 to 18:50 the bootstrap on the final tree: OK, 16 of 16 in 160 s. Film pairs of the hardest of the 90 hits (120 N*s into the chest
  kneeling: the head 1.93 m/s at 0, 4.81 at 1) and of the same at 1 with A, `w3_strict_hard`, `w3_strict_hard_r3a`, on the page.
  18:51 `LINKS OK`; the final patch `Saved\Backups\w3_final.patch`. Nothing committed (the owner's command); no push; the headset not
  touched.
- 18:53 the order done with 3.5 h of the window left; the next item of section 8 (5, the rest of M4) holds measurements the owner has
  already designed and left "to set from measurements": decision 46 (the two hands' lift) taken first, measurement only, by `-Tune`
  (nothing in the files). 18:55 to 19:04 `docs/recon/window3/lift_two_hands.ps1` (two scenario files of its own, not the suite; a
  PowerShell comma before a plus gave a path of four numbers at first) and `lift_stands.ps1`: 300 N a hand lifts a standing body 6.7 cm
  onto its toes, 400 N hangs it from the hands with the feet off the floor; at 450 N the grips of the two-hand hold tear 26 times; films
  `w3_lift_standing`, `w3_lift_lying`; decision 46's row, testing, PRD Status, the tabs and the page.
- 19:05 to 19:06 the sweep table on today's body (C30, decision 43; `docs/recon/window3/sweep_table.ps1`, 24 kicks at the left leg):
  none fells it, 4 stand and 20 step; written into testing and the page, to be measured again on R4's body.
- 19:07 the pull table's figures (C28, C29) read from the latest `grab-pulls` run, no new runs: 33 of 34 pulls at walking pace step and
  do not fall; a yank is no harder than the hand's 300 N. The rest of M4's items (the yank and the throw as verbs, the kick in the headset
  by a button) are features to build from a part's plan, with the owner's choices (which button); not begun. 19:08 the close's last
  things (the patch, the memory, the report).
