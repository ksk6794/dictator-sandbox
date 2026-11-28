# The second unattended window of 28 November 2026 (the owner away)

Start 09:48:43 (Get-Date), end 17:48:43 (start + 8 h). The last 75 minutes, from 16:33:43, are only the close (bootstrap,
review of the whole diff, documents, pages, report). The clock is read at every change of stage and written here.

The owner's word of 28 November: decisions 59 to 66 and 68 confirmed; 55 to 57 and fast-checks 12 and 13 confirmed; 67: fear,
stay_down, protect and commands on by default. Three commits allowed on `main` (no push): (1) everything before R10, (2) R10,
(3) R10 switched on by decision 67. Nothing else committed tonight; after every step its patch in `Saved\Backups\`. Decisions
that need the owner are taken under the owner's delegation of 28 November (recommended option by the data and the goal), written
into `docs/active-ragdoll.md`, section 10, with the options turned down. Not delegated: the headset (no `-VR`, SteamVR not
touched), push, deleting files of others, `fall.aim` (stays).

## Stages

| Stage | What | Read of `docs/pitfalls.md` |
| --- | --- | --- |
| 0 | Start: `night2_start.patch`, backup; commit 1 (before R10) through the index `r10_start.index`; commit 2 (R10); bootstrap | PowerShell, scripts and building |
| 1 | R10 on by decision 67: baseline `r10on_before`, the four switches to 1 in `body.json`, scenarios of "at 0" switch it off themselves; every moved hash explained; the gates (matrix, knockdowns, seeds plain, `-Near 70`, `-Near 70 -CommandAtS 3`, scorecard C25 <= 439, record-replay `-Perception`/`-Takes`/`-Walk`, `play -Smoke`, bootstrap); baseline `r10on`; commit 3 | measuring, runs and tests; falls and the get-up |
| 2 | Decision 58 (R3) under delegation: variant A measured on the body with R10 (knockdowns 24 directions at 160 and 240, by hand at 3.5 and 4.5 m/s, scorecard, stand on three seeds, film pair); on only if head strikes and C14 better and nothing worse than the plan's bounds | falls and the get-up; hands, the grab and blows |
| 3 | The headset tomorrow: face buttons for the commands (right A get_up, right B stop) through `ISandboxControllerTracker`; a check on the made-up headset; the commands in the recording; the page "VR session 29 November" | the session, recordings and the headset |
| 4 | R10 step 3, the limb arbiter (`FSandboxLimbClaims`), every hash kept; replaces decision 63's rule | muscles, poses and the standing body; falls and the get-up |
| 5 | R10 step 8's rest, `give_hand` (decision 47) | hands, the grab and blows; falls and the get-up |
| 6 | With 2 h or more left: step 7 (the struggle when dragged, the whole body); else C34's kneeling leg, C39's slow four | falls and the get-up |
| Close | bootstrap; review of the whole diff; documents and Claude Docs tabs; pages; memory; the report in Russian | |

## Journal

- 09:48:43 start. `Saved\Backups\night2_start.patch` (450 520 bytes), backup `20261128_094904_night2_start` (107 files).
- 09:50 commit 1 `6da60e9` (before R10: 31 tracked files of `r10_start.index` and the 21 untracked of `r10_start_untracked.txt`;
  `docs/recon/r10_plan.md` went in as amended during R10); the split clean: 32 files of R10 left, 38 new. Commit 2 `cc2dd89` (R10).
  09:51 to 09:52 bootstrap OK, 15 of 15.
- 09:53 stage 1. Baseline `r10on_before` (206); the four switches to 1; 27 hashes moved (all of the expected kinds), 34 failed;
  scenarios put right (`docs/testing.md`, "R10 switched on"); 207 of 207, the gates; 10:19 bootstrap OK; baseline `r10on` and the run
  hashes `r10on_*`; 10:21 commit 3 `e5e8ce2`.
- 10:05 to 10:23 stage 3's code written while the stands ran (the face buttons, the walk's ninth check, the commands in the session
  file and the replay's comparison), kept out of commit 3 as `Saved\Backups\night2_step3_buttons_wip.patch` and put back after it;
  build, 65 tests, the walk 9 of 9 and its replay 7 of 7 with its hashes, the suite 207 of `r10on`; `night2_step3_buttons.patch`.
- 10:24 stage 2 (R3, decision 58): A measured on the body with R10 (96 falls, check set with A, stands); not yet, under the delegation
  (C14 by hand and C34 worse); `fall.head` alone measured too; the film pair of the cover while rising.
- 10:36 the page "VR session 29 November" (https://claude.ai/artifact/XatvKGwU2Ru7fJ3KE1ASYh); the suite's 102-degree step found to be
  the start blend from a displaced arm, not two users (my first reading was wrong; corrected in testing.md and on the page).
- 10:42 stage 4, the arbiter: `FSandboxLimbClaims`, decision 63's rule moved into it, the record; every hash kept (10:59); review of the
  buttons and the arbiter, three findings put right.
- 11:00 stage 5, `give_hand`: built, five scenarios, seven rounds of finds put right (testing.md), every hash at 0, switched on under the
  delegation (decision 71); the walk presses X; films; review launched 11:31.
- 11:33 stage 6: R10's step 7, the struggle (5 h left before the close). Built behind `behaviour.struggle` at 0; measured: it frees
  none within C25 (decision 72, the owner's); review of give_hand's fixes (three defects, put right) and of the struggle (four, put right).
- The owner's word of 28 November (an addendum, about 12:00): finish stage 6 and the close; with time left, R10 to its gate (step 9 and
  step 10's automatic part); after the close stop (no new item); the audit placed in `docs/active-ragdoll.md`, section 8, after R10 (not
  begun); a fourth commit after the close (stages 3 to 7 and what of steps 9 and 10 is done), no push.
- 12:04 the cover's blend measured for C34 and C39 (not taken); 12:05 step 9: `struggle_phase_*`; it found R2's held arm let go at a
  point of the world and placed by a reach's fixed pole (150 and 138 degrees): both put right (seven held-arm scenarios move, all pass;
  the stand held by one hand as before).
- 12:16 step 10's automatic part: `docs/recon/night2/r10_gate.ps1`: every hash at 0, the suite 219 of 219, matrix, knockdowns, stands,
  C33, C39 146 of 150, C25 439; not met: C34's kneeling leg, C35 (in the ankle drags the cover held every limb). The page of the window's
  videos https://claude.ai/artifact/Ht7A6SiqFvMbtuLc6wcda2; the review of the window's code (seven findings; five put right, two left).
- 12:30 a leg a hand takes hold of is not covered (`behaviour.protect_held_leg`): at 0 every hash, on six scenarios move and no stand's
  run; C34's kneeling leg met; on under the delegation (decision 73). C35 on twenty seeds: 9 of 18 attempts free the body, all by the
  ankles. 12:55 the struggle on: flips of 160 degrees as an arm drawn in to 25 cm went into the protective pose; a pole kept with the
  trunk did not help (taken out); drawn in to 45 and 70 cm none (the files' values; the struggle at 0, decision 72's (e) recommended).
- 13:15 C39's slow four: the cover let go in the command's own tick and the get-up waited for the body to lie still;
  `behaviour.command_cover_react` lets it go 0.3 s after and counts it; 150 of 150 at 0.30 s; on under the delegation (decision 74).
- 13:27 the second review (the code since the first): six findings, put right (the cover's react held back the whole wish to stay
  down and turned a get-up back again; the leg's delayed cover overrode another; and four more). 13:52 the third reading of those fixes:
  two real and four small, put right. The Claude Docs tabs brought in step with the files (Fast checks, Natural body, Active ragdoll 2's
  sections 8 and 10, PRD's Status).
- 14:00 the window's last measurement (`docs/recon/night2/final_gate.ps1`): every hash kept where it should be, baseline `n2end`.
- 14:20 the first reading's seventh find put right (an offered arm placed again only once its release has let go;
  `command_give_hand_away_back`, 220 scenarios); a fourth reading of it (sound; the wait widened to any release's weight), every hash of
  `n2end` kept. The give_hand and ankle-struggle videos filmed again on the final code, the page republished.
- 14:34 the close, begun early: R10's gate done in its automatic part and nothing new to be begun (the owner's addendum).
  `record-replay -Perception` (its hashes as in the morning), `-Takes`, `-Walk -Twice` 8 of 8, `play -Smoke` 15 of 15; 14:37 bootstrap
  OK, 15 of 15 in 153 s (the suite 220 of 220). Then the patch `Saved\Backups\night2_commit4.patch` and commit 4.
