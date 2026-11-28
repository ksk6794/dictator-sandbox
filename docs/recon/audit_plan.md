# The audit of 28 November 2026 (the owner away)

Start 14:41:19 (Get-Date). Aim: 8 to 12 hours. The clock is read at every change of stage and written here.

The owner's task of 28 November (`docs/active-ragdoll.md`, section 8, item 3): the whole code base and the documents held against
each other and against the code, cleaned, every hash kept; the player's hands (visible hands with fingers) written into the plan as
the next item after the audit, not built. Done whole without questions; every decision that would be the owner's is taken under the
owner's delegation, in this order: (1) the goal (a natural, living body that can be interacted with at every moment, FR-18); (2) the
body's behaviour does not change, every hash kept; (3) the rules of `CLAUDE.md` and decisions 1 to 74; (4) one source for every fact;
(5) less code and text for the same use. Each such decision goes into `docs/active-ragdoll.md`, section 10, with the options turned
down. Not delegated: the headset (no `-VR`, SteamVR not touched), push, a change of the body's behaviour, deleting what waits for the
owner or what a later part names (marked "candidate" in `docs/recon/audit_switches.md` instead), purchases.

Read of `docs/pitfalls.md` before the plan: measuring, runs and tests; PowerShell, scripts and building; the session, recordings and
the headset; hands, the grab and blows (and every entry of `CLAUDE.md`).

The rule of the audit: every hash kept. Before: `suite.ps1 -SaveBaseline audit_before`, `bench.ps1 -Hashes -Label audit_before`;
after every step of code `suite.ps1 -Baseline audit_before`; after part 3 `bench.ps1 -Hashes -Against audit_before`. A hash that
moves is not the audit: taken back, or a finding in the report. Before every part its patch in `Saved\Backups\`.

## Parts

| Part | What | Who |
| --- | --- | --- |
| 0 | The baselines (`audit_before`: the suite, every tool's hashes) | me |
| 1 | The documents: one source for every fact (status in `docs/PRD.md` "Status", order of work in `docs/active-ragdoll.md` section 8, criteria in `docs/natural-body.md`, figures in `docs/testing.md`, pitfalls in `docs/pitfalls.md`, commands and rules in `CLAUDE.md`, tuning in `Config/Tuning`, the scenario format in `Scenarios/README.md`); contradictions and stale facts put right by the code and the data | four agents side by side (A: `docs/PRD.md`, `CLAUDE.md`, `docs/fast-checks.md`; B: `docs/active-ragdoll.md`, `docs/natural-body.md`; C: `docs/testing.md`; D: `docs/pitfalls.md` and the READMEs), each its own files only; then I bring their cross-file lists together |
| 2 | `Tools\check-links.ps1`: links and anchors, paths, scenario names, tuning names, scripts' parameters; unit tests of its parsing; a step of the bootstrap and of `checks.ps1 -What links` | me, while the agents read |
| 3 | The code, tuning, scenarios and tools: the register of switches and values at 0 (`docs/recon/audit_switches.md`), dead code, duplicated arithmetic, scenarios that repeat, unused parameters and helpers, the rules of `CLAUDE.md` against the code; an independent reading of the audit's diff and of the code since `20a9ca1` | me, step by step (the engine's nine processes and the hashes are shared) |
| 4 | The player's hands in the plan (section 8, after the audit, before the rest of M4; a line in `docs/PRD.md`) | me |
| Close | Checks (`build -Rebuild`, tests, suite against `audit_before`, `bench -Hashes -Against audit_before`, the check set, record-replay `-Perception`/`-Takes`/`-Walk`, `play -Smoke`, check-links, bootstrap); the Claude Docs tabs; the memory; two commits (1: parts 1, 2, 4; 2: part 3); the report in Russian | me |

## Journal

- 14:41:19 start, tree clean at `c79519c`. 14:41 `suite.ps1 -SaveBaseline audit_before` begun.
- 14:43 the suite 220 of 220 (74 s), baseline `audit_before`; `bench.ps1 -Hashes -Label audit_before` begun (14:43 to 14:57: 25 tools,
  1764 runs). 14:44 the four agents of part 1 launched side by side.
- 14:45 part 2: `Tools\check-links.ps1` written (links and anchors, paths, tuning names, scenario names, script parameters; a
  self-test of 20 cases: anchors, Cyrillic, backslashes, wildcards). First run 14:50: 260 problems, 16 links in 36 files (the documents
  hardly link to each other); the scenario rule cut by words that end in a unit, the table rule to rows with a `powershell` command.
  The marks `<!-- removed: ... -->`, `<!-- planned: ... -->`, `<!-- not-names: ... -->` for names that are history or plans (decision
  75). Sent to the agents 14:52. Two defects found by agents A and C (a wildcard name was not held against the marks; a parameter
  could not be marked), put right. A step of the bootstrap (beside the check set) and `checks.ps1 -What links`.
- 14:55 the register's facts handed to a fifth agent (read-only but for `docs/recon/audit_switches.md`); the decisions on it mine.
- 14:57 part 3 begun: `Saved\Backups\audit_part3_start.patch`, backup `20261128_145748_audit_part3_start`. Agents C (14:54), A (14:56)
  and D (14:58) done.
- 14:58 step 3.3a, dead code: the getters no code calls (the controller's `GetFallReachCount`, `GetFallHandTouchS`, `GetDoubledWeight`,
  `GetBraceCount`, `GetToneStiffness`, `IsStarted`, `IsFootPlanted`, `GetFootPoint`; the hand's `IsStarted`; the pawn's `GetTool`,
  `IsUserPaused`, `IsSlowMotion`, `IsCameraDetached`, `GetReachCm`; `FSandboxCarry::GetBodyShare`, `FSandboxChance::IsOn`,
  `FSandboxRecordedTargetSource::GetFrame`) and the fields only they read or nothing reads (`Braces`, `FallReaches`, `bRegroupAsked`).
  No free function, file constant or `common.ps1` helper without a caller; one script parameter never read (`pool.ps1 -Status`, the
  default action under its own name: kept). No two scenarios the same; the near pairs differ by a side, a time or a physics asset.
  15:01 build OK (50 s), 67 tests, the suite 220 of 220 with every hash of `audit_before`; `audit_step3a_dead_getters.patch`.
- 15:00 agent B done; 15:02 the agents' cross-file lists brought together (CLAUDE.md's pitfall count 221 and 242, the walk's nine
  checks, `checks.ps1 -What links`, R1's first results no longer a section; natural-body's citation of N2 already right); every
  document `LINKS OK` (36 files, 201 links, 185 anchors).
- 15:04 part 4: the player's hands as section 8's item 4 (the later items renumbered, the decision 40 table with them), a row in
  PRD's Status and a sentence in M4's part 2; the mesh from the engine's `XRMannequins` pack (decision 76). Decisions 75 to 77.
- 15:06 step 3.5, the rules against the code: no include across `SandboxBody` and `SandboxInteraction`, the `.Build.cs` files as the
  rule says, no wall clock in the body, the hands or the core, the overlay writes nothing to a body, no tuning field with a default
  but 0 in C++. Two independent readers of the code since `20a9ca1` launched (the body; the rest and today's tools). Step 3.3b: the
  share of the old assist while getting up, written three times, made one function (`AssistStateEase`). 15:10 build OK, the suite
  with every hash of `audit_before`; `audit_step3b_assist_ease.patch`.
- 15:10 the register done by its agent (75 rows, about 200 values; no value without a reader). 15:11 to 15:16 step 3.2, by the four
  conditions (decision 78): `balance.ankle_side_gain` and `behaviour.lower_end_cm` taken out, `getup.hold_by_body` and
  `getup.hit_by_impulse` folded (the owner's 32 and 35); the code, `body.json`, the tuning's checks and the comments. 15:16 build OK,
  67 tests, the suite 220 of 220 with every hash of `audit_before`; `audit_step3c_switches.patch`. The documents that named them as
  live put right (Config/Tuning and Scenarios READMEs, R2's steps, PRD, pitfalls), the journals' removed lists; `film-pair.ps1`'s
  and CLAUDE.md's example moved to the struggle; 15:19 `LINKS OK`. The candidates for the owner in the register's last section.
- 15:19 the two readers of the code since `20a9ca1` back. The tools' (11 findings, nothing serious): put right the pool's run of
  `scenario.ps1` without its time limit (`Submit-CheckJobs -TimeoutSec`, a job nobody took is taken back), a scenario at another rate
  than 90 handed to the pool or a set, the pool's log named as the run's, the pool's top-up not counting its young processes, a run
  that must be alone waiting two minutes beside the pool's idle processes, a stop whose lock fails leaving the pool closed; and in
  `check-links.ps1` a code span over a line break, the parameters after a pipe, a file deleted but in the index, a path in another
  case, emphasis by underscores in an anchor, a fence closed by one with an info string (six more self-test cases). Left: a yielded
  slot taken by the pool's top-up (plausible, not reproduced). The body's (8 findings): two break the authority rule and are NOT put
  right, since that changes the body (not delegated): a hit that makes a rising body cover itself gives the attempt up before the hit
  takes the world's pull, which then fades over 0.4 s at full strength; after an attempt given up afraid the pull fades over 1.5 s into
  Lying and hits do not take it. With them in the report: a flag of a give-up kept into an attempt begun and given up in one tick, the
  struggle's legs placed with the arm's hinge (at 0), the arbiter now gating R2's held arm with no switch (no scenario reaches it), a
  dug-in foot at an old floor's height (at 0), numbers in the code (the catch's poles and cut, the lowering's window). Put right: no
  lower bound on `offer_blend_s` and `lower_turn_s` (0.02 s now, as the other blends), `FSandboxLimbClaims::Reset` (never called), the
  held arm's pole flag (always false), three comments that said nothing reads or draws. 15:25 build OK, 67 tests, the suite with
  every hash of `audit_before`; `audit_step3d_review_fixes.patch`; the pool with one scenario, its hashes as without it.
- 15:26 to 15:35 the checks after part 3 (`docs/recon/audit/part3_checks.ps1`): rebuild, tests, the suite, the check set of six tools
  (label `audit_after`), record-replay `-Perception`, `-Takes`, `-Walk -Twice`, `play -Smoke`, `LINKS OK`: all as before. The bench did
  not start (its `-Label` missing); 15:35 to 15:49 `bench.ps1 -Hashes -Label audit_after -Against audit_before`: SAME, 1769 runs and
  files, 25 of 25 verdicts.
- 15:27 the independent reading of the audit's whole diff launched; 15:40 back: the code's folds exact, `check-links.ps1` clean;
  put right: three notes of `docs/testing.md` that called the folded switches live, a job past its time in the pool left running (its
  process is ended now), a scenario's `fixed_fps` read only by `scenario.ps1` (now by every batch), the pool's log not named on a
  failure, a stop's lock failure taking away another stop's `draining`, `Content/` held on a clean clone (generated: left out). Left:
  the bootstrap's figure in PRD and CLAUDE.md (15 steps) until the close's bootstrap.
- 15:38 to 15:47 the Claude Docs tabs: "Fast checks" by me (the state paragraph, section 12), "Tab 1", "Natural body" and "Active
  ragdoll 2" by three agents, section by section from the files (the widgets and the Mermaid left as they were); the tab follows the
  file in every word but its relative links, which the doc does not keep. 15:42 the memory brought up to date. 15:50 the pool again on
  the final tools; 15:51 `docs/testing.md`, "The audit"; a last reader on the tools' last fixes.
- 15:52 the last reader back (7 findings in the new time limit and in `check-links.ps1`): put right a job finished as its deadline
  passed (its `.done` looked at again before its process is ended), the jobs taken back from an ending pool run within the time left
  and counted, `Invoke-ScenarioQueue` says what it lost, an empty marker read again before the kill, a code span not joined across a
  list item, a heading or a quote, a stop's give-up leaving another stop's `draining`. Left: the call's time counts the time a job
  waited in the shared queue (said in the code). The time limit tried: a 600 s scenario through the pool with `-TimeoutSec 5` ends in
  7 s as "timed out", its process ended, the pool serves the next run with the same hashes. The blank line that split section 10's
  table of `docs/active-ragdoll.md` in two taken out (found by the tab's agent).
- 15:57 backup `20261128_155703_audit_before_commits`, `Saved\Backups\audit_commit2_part3.patch` and `audit_all.patch`; part 3's
  files set aside (`git stash`, `audit_part3`); 15:57 to 16:00 the bootstrap at commit 1's tree: OK, 16 of 16 in 212 s. The
  bootstrap's figure in CLAUDE.md and PRD's Status (and the tab), and the audit's findings for the owner in Status.
