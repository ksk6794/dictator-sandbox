# Faster checks and runs: where the time goes, requirements SP-0 to SP-12

Written on 27 November 2026 on the owner's request: development is slow because checks and test runs take long. This document
is the measurement of where the time goes, the requirements that cut it without changing any result of a run, and the owner's
decisions on them. The same text is the tab "Fast checks" of the plan in Claude Docs (with two drawings: a chart of the suite's
phases where this file has their table, and the stages in section 9), kept in step by hand; the Russian draft the owner decided
on is replaced by it.

**Status:** proposed and decided on 27 November 2026 (the owner's five decisions are in "Owner's decisions" at the end); no
requirement is built yet. Where it goes in the order of work (`docs/active-ragdoll.md`, section 8) is the owner's to say.

## 1. Why

The goal is to cut the agent's time on DictatorSandbox by about 22% (1.3 times as fast) without changing a single result of a run.
For that the runs have to get about 40% faster.

Waiting for runs is 57% of the agent's time today: 16 to 17 hours of 29 in the ten sessions of 25 to 27 November 2026. The other
43% is the model's own work. In the night's unattended window the waiting is 67%, in sessions with the owner 31%.

Most of the waiting is not physics. Of the suite's 61 s, the simulation takes 33 s, and physics only about 45% of that. One
`scenario.ps1` takes 13 s, of which 2 to 3 s are simulation.

Targets:

- build, tests and suite in 55 s instead of 103;
- one scenario in 4 s instead of 13;
- the bootstrap in 160 s instead of 381;
- every scenario keeps its hashes in full-length runs; the inner loop's early end (SP-9) has a baseline of its own.

## 2. Where the time goes now

Of the suite's 61 s about 15 s are physics, and a single run spends less than a quarter of its time on useful work. Measured on
27 November on the owner's machine: Ryzen 7 9800X3D (8 cores, 16 threads), 32 GB, NVMe. Sources: the queue's process logs, the
runs' `metrics.json`, `test.log`, `bootstrap.json` and the transcripts of ten agent sessions.

The suite of 154 scenarios on 8 processes, phase by phase, the mean over the processes:

| Phase | Seconds |
| --- | --- |
| The queue and the processes launched | 3.0 |
| Waiting for the Zen server | 2.5 |
| The rest of the engine's start | 9.9 |
| Worlds prepared, 0.42 s a job | 8.2 |
| **Physics** | **14.7** |
| The rest of the frame's work | 18.0 |
| The processes exit | 1.4 |
| Results read and compared | 3.1 |
| In all | 60.8 |

The scenarios themselves (worlds, physics and the rest of the frame) take about 41 s; the other 20 s are start, exit and reading.

### One run of each tool

| Run | In all | Useful work | Where the rest goes |
| --- | --- | --- | --- |
| `scenario.ps1`, one scenario | 13 s | 2 to 3 s of simulation | the engine's start about 8 s (2.6 s of it waiting for the Zen server), exit 1.9 s, PowerShell |
| `test.ps1` | 15 s | 1.2 s (59 tests) | the editor's start 6.8 s; a fixed pause of the engine's automation, 4.4 s (`DefaultDelayTimer = 5.0f`) |
| `suite.ps1`, 154 scenarios, 8 processes | 61 s | 15 s of physics | the table above |
| `bootstrap.ps1` | 381 s | | 15 steps one after another; each step starts its own engine, a record and replay twice |

Three more facts shape the requirements:

- Under load each of the 8 processes starts in 8.5 to 16 s (6.4 s alone), takes 1.8 GB and about 1.2 cores.
- Each job of the queue prepares a fresh world in 0.42 s. More than half of it is the skeletal mesh being built again
  (`AssetCompile SkinnedAsset`).
- A frame of simulation takes 1.54 ms on average, physics 0.65 ms of it. A passive body (`drop_passive`) takes 0.9 ms a frame with
  0.34 ms of physics: part of the difference is the run's measurements, not the body.

### How often and what runs

The ten sessions made 466 calls that built or started the engine: 16 an hour, a median pause of 2 min between them. 137 of them
built, about once in 13 min. The other 329 ran on code already built; the first of them followed a build after 80 s on average.

| What runs | Calls | Waiting, min | Median, s |
| --- | --- | --- | --- |
| The agent's own batteries (`Saved\Recon\*.ps1`: suite, matrix, stand in a row) | 96 | 101 | 81 |
| The suite, often with a build and the tests | 83 | 98 | 70 |
| A `for` loop over `scenario.ps1` | 76 | 61 | 32 |
| The stand and three seeds | 35 | 58 | 122 |
| Matrix, knockdowns, grabs, sweeps | 22 | 32 | 86 |
| Films | 44 | 30 | 36 |
| A build without a run | 42 | 25 | 26 |
| The bootstrap | 15 | 23 | 339 |
| Sessions and replays | 20 | 15 | 41 |
| Scorecard, tests, single scenarios | 33 | 17 | |

That is 7.7 h of waiting in the foreground. The agent waited 8 h more for runs in the background, mostly the night's batteries,
stands and bootstraps.

## 3. Goals and acceptance

The work is accepted when the benchmark (SP-0) shows the target times of the table and every run's hashes match the baseline taken
before the work.

| Measurement | Now | After stage 1 | After all stages |
| --- | --- | --- | --- |
| One scenario, `scenario.ps1 -Name getup_left` | 13 s | 9 s or less | 4 s or less |
| 4 scenarios times 3 sets of tuning in one call | 148 s as a loop | 25 s or less | 15 s or less |
| `test.ps1` | 15 s | 9 s or less | 9 s or less |
| `suite.ps1`, 154 scenarios, compared with a baseline | 61 s | 52 s or less | 35 s or less in the inner loop, 45 s or less at full length |
| Build, tests and suite after an edit of the controller | 103 s | 90 s or less | 60 s or less |
| `getup-seeds.ps1`, three seeds | 92 s | 82 s or less | 60 s or less |
| `natural.ps1 -Quick` | 55 s | 45 s or less | 35 s or less |
| `bootstrap.ps1` | 381 s | 200 s or less | 160 s or less |

### The rule of the hashes

- Every requirement but SP-9 keeps the hashes in three checks: all 154 scenarios (`suite.ps1 -Baseline`), the push matrix
  (`baseline.ps1 -CompareMatrix`) and the stand on three seeds, run by run.
- SP-9 changes no full-length hash: the gates, the bootstrap and the taking of baselines run at full length. The inner loop's runs
  with an early end are compared with a baseline of the same mode.
- A name too many in the list of changed scenarios is a defect. The scenario is run again first, then the cause is looked for;
  "it differed once" is not accepted.

### SP-0. Measuring before and after

- `Tools\bench.ps1 -Label <name>` runs the measurements of the table three times and writes the medians to
  `Saved\Bench\<name>.json`. Without the bootstrap it takes about 10 minutes; `-Full` adds the bootstrap.
- It measures on an idle machine. If another `UnrealEditor-Cmd` is running, it refuses and says so in its first line.
- The baseline `bench_before` is taken before the first change, and the measurement is repeated after each stage. A stage's report
  gives both columns.
- Ten sessions after the last stage, the share of waiting in the agent's transcripts is counted as it was for this document. The
  goal is 35% or less instead of 57%.

## 4. Constraints

The speed-up must break no rule of `CLAUDE.md`. The ones this work touches:

- **Determinism.** The first run of a process differs from the later ones, so the warm-up (`batch_warmup.json`) stays in every new
  process. The solver's determinism mode and `KeepSolverInStep` are not touched. Every job still runs in a fresh world.
- **Measuring changes nothing.** The lean mode of measurements leaves out only reading and writing figures; a run's hashes are the
  same with it and without it.
- **Fixed step.** 1/90 s, 2 substeps, 64 solver iterations on the NPC stay. Only the pool's queue reads the clock, and only to decide
  whether to wait or to exit; simulation code does not.
- **The editor locks the module DLLs.** A live pool would keep the project from building, so `build.ps1` stops the pool before it
  builds. Live Coding is not used.
- **Memory.** At most 9 engine processes of 1.8 GB at a time on 32 GB. The pool's processes count, with those of films and sessions.
  The owner does not buy hardware: everything fits this machine.
- **Two agent sessions at once.** So it was on 27 November. Queues and the pool must not mix up each other's jobs, and no session
  stops another's pool in silence: a build waits for that session's jobs to end and says what it is waiting for.
- **Scripts.** Windows PowerShell 5.1. The first line is the verdict, the result is JSON under `Saved\`, success is decided by the
  result file and not the exit code. Source files are edited with the editor, not with shell one-liners.
- **Modules.** The queue and the measurements live in `SandboxScenario`. Splitting the controller adds no dependency between
  `SandboxBody` and `SandboxInteraction`.
- **Headset.** The pool runs with `-nohmd -DisablePlugins=OpenXR -nullrhi`; runs in the headset happen only on the owner's word, as
  now.

## 5. Stage 1: the cheap requirements

Stage 1 is about a day of work and cuts the waiting by about 15 to 20%. It needs no pool and changes no hash.

### SP-1. One queue instead of loops

In ten sessions the agent ran `scenario.ps1` in a loop, one scenario at a time, 76 times and waited 61 min. Each such run takes
13 s, 2 to 3 s of it simulation.

- `scenario.ps1` takes a list of names (`-Name "getup_back,getup_left"`) and several sets of tuning separated by `;`
  (`-Tune ";getup.start_blend_s=0.6;getup.start_blend_s=0.3"`; an empty set is the defaults). Every combination is one job of one
  queue on up to 8 processes.
- Results go to `Saved\Runs\<label>\<scenario>__<set number>`. One name without `-Tune` writes as now, to
  `Saved\Scenarios\<name>`. Output: a verdict line a run and the summary line first.
- `-Shot` runs through a queue as well, rendered and on 4 processes at most; `film-pair.ps1` films both sides at once, not one
  after the other.
- `CLAUDE.md` gets the rule: no loops over tools that start the engine; several runs go in one call or through `sweep.ps1`.
- **Acceptance:** 4 scenarios times 3 sets take 25 s or less instead of 148. Every run's hashes match its single run.

### SP-2. Start and exit flags

Every start of the engine pays 4 to 5 s today for things a run does not need, and the tests 4.4 s more.

1. **Zen.** The DDC server ends with the last process, and every start waits 2.3 to 2.7 s for it. The owner's decision
   (27 November): `[Zen.AutoLaunch] LimitProcessLifetime=False` in `DefaultEngine.ini`, so the server stays up. The fallback, the
   graph `-ddc=InstalledNoZenLocalFallback` for runs, is taken only if a measurement does not show Zen's initialisation at 0.3 s or
   less (2.6 s today when it is launched, 0.1 s when it is alive; the server takes 127 MB).
2. **Asset registry.** Every process writes the registry's cache of 137 MB as it exits (about 0.9 s). The queue's and the tests'
   processes start with `-NoAssetRegistryCacheWrite`. Only `gen-assets.ps1` writes the cache.
3. **Automation.** `test.ps1` takes away the five-second pause with the engine's `Now` command. The exact spelling is checked
   against `AutomationCommandline.cpp`, lines 602 to 616.
4. **Exit.** When the queue is empty and every result and log is written, the process exits by force
   (`RequestExitWithStatus(true, ...)` in `UScenarioBatchSubsystem`), without the engine's orderly shutdown.

- **Acceptance:** one scenario in 9 s or less, `test.ps1` in 9 s or less. In a process log `AutoLaunch initialization` takes 0.3 s
  or less. No job of 1000 is lost (`QUEUE WARNING`). Hashes unchanged.

### SP-3. Composite checks through one queue

The bootstrap, `natural.ps1` and the agent's own batteries run tools one after another. Each tool starts its processes again, and
the tail of one is not overlapped by the start of the next.

- Every tool of runs (`suite`, `push-matrix`, `knockdowns`, `getup-stand`, `grab-pulls`, `compliance`, `natural`) is split in
  three: the plan (the list of jobs), the run and the judgement (the verdict from the result files). Alone it works as now.
- A new `Tools\gate.ps1 -What suite,matrix,knockdowns,seeds -Tune ... -Label <name>` puts the plans of all its tools into one queue
  on up to 9 processes, the longest jobs first. At the end it prints every tool's verdict and one table of comparison with the
  baseline (SP-12).
- The bootstrap runs one `gate` after the build and `gen-assets`. Sessions (`play.ps1 -Smoke`, `-Demo`) and record and replay run
  beside the queue and count toward the 9 processes; `test.ps1` runs beside the first jobs.
- In `CLAUDE.md`, `gate.ps1` replaces the batteries in `Saved\Recon`.
- **Acceptance:** the bootstrap in 200 s or less (381 now), with the same verdicts and result files for every step.
  `gate -What suite,matrix,knockdowns,seeds` is at least 35% faster than the sum of the tools run one after another. Hashes
  unchanged.

## 6. Stage 2: a pool of warm processes

Stage 2 takes the engine's start out of most calls: 329 of 466 run on code already built. It is about 2 to 3 days of work and the
largest gain, 12 to 15 s a call.

### SP-4. A pool that stays

- `UScenarioBatchSubsystem` gets the mode `-ScenarioPool=<folder>`. When the queue is empty the process does not exit; it waits for
  new jobs, looking at the folder every 50 ms. Jobs are taken by the same marker under a lock file (`TakeJob`).
- A pool process exits by itself in four cases: idle longer than `-PoolIdleS` (10 minutes, the owner's decision of 27 November), a
  file `stop` appears, the project's DLLs or generated assets change (a fingerprint compared between jobs), or after 500 jobs or
  2.5 GB of memory. The warm-up runs once a process.
- `Tools\pool.ps1 -Start | -Stop | -Status` starts 8 processes, stops them or shows the state, kept in `Saved\Pool\pool.json`:
  processes, the build's fingerprint, busy and idle. Before any run in the headset (`play.ps1 -VR`, `scenario.ps1 -VR`) the pool
  stops by itself, so as not to take memory and processor from the owner's session (the owner's decision of 27 November).
- `Invoke-ScenarioBatch` hands jobs to a live pool with the same build fingerprint first. Without a pool it starts processes as
  now. Jobs with `-CVars` and rendered jobs always run in processes of their own. `SANDBOX_SEPARATE_PROCESSES=1` still turns off
  both the queue and the pool.
- The jobs of two sessions run in the order they come, each writing its own `Out`. A job without a result gives
  `QUEUE WARNING`, as now.
- **Acceptance:**
    - one scenario on a warm pool in 4 s or less, the suite in 45 s or less;
    - the hashes of the suite, the matrix and the stand in the pool match fresh processes, in a fresh pool and in a pool that has
      run 1000 jobs;
    - after `build.ps1` no job runs on the old DLLs.

### SP-5. The pool and the build

- `build.ps1` stops the pool before building: it takes no new jobs, waits for the running ones (5 min at most) and says whose jobs
  it is waiting for.
- After a build that succeeds it starts the pool in the background (`-NoPool` turns that off). While the tests run (about 9 s) or
  the model thinks (a median of 80 s to the next call), the pool warms up.
- `gen-assets.ps1` stops the pool before and starts it after as well.
- **Acceptance:** build, tests and suite in 75 s or less until stage 3 is done. Two builds in a row from two sessions lose no job.

### SP-6. Assets kept between jobs

Every job now spends 0.42 s preparing its world: the mesh is unloaded by the garbage collector and built again. That is 8 s of the
suite and about 7 s of the stand.

- The queue keeps strong references to the meshes, physics assets and clips the previous job loaded. The world and all its actors
  are made anew, as now.
- **Acceptance:** a job's preparation in 0.15 s or less; `AssetCompile SkinnedAsset` lines only in a process's first job. Hashes
  unchanged; a process's memory grows by 200 MB or less over 1000 jobs.

## 7. Stage 3: simulation, build and reports

Stage 3 cuts the work of a run itself and the model's time on reading results. About 2 days. SP-8 starts only after SP-7's
measurement.

### SP-7. A profile of the frame

0.9 ms of a 1.54 ms frame are not physics, and what spends them has not been measured.

- One long scenario with a get-up (`getup_laid_beside_table_back`, 25 s) and one passive one (`drop_passive`) run alone with an
  Unreal Insights trace (`-trace=cpu,frame`).
- The frame's share is split by part: the solver, the body's controller, Physics Control, animation and the mesh's bones, the
  MetaHuman post-process, the run's measurements (penetration, `FNaturalMeasures`, writing `series.csv` and `trace.json`).
- **Result:** a table in `docs/testing.md`. It decides what SP-8 takes. If the measurements are less than 15% of the frame, SP-8 is
  dropped.

### SP-8. A lean mode of measurements

- A job of the queue gets the field `measure: "outcome"`. In it a run computes only what the tool's verdict reads (states, events,
  the end pose, hashes) and leaves out the costly measurements SP-7 found.
- The stand, the seeds, the matrix and sweeps use it; the suite and the scorecard run with full measurements.
- A check whose metric the lean mode did not compute is an error of the run, not a zero: a metric that always read 0 has hidden a
  defect once already.
- **Acceptance:** hashes and verdicts are the same in the lean and the full mode. The stand's simulation is at least 20% faster.

### SP-9. Early end of scenarios

65% of the suite's simulated time, 1158 s of 1795, comes after the last event. The get-up scenarios last 25 to 40 s, and the body is
up by about 10 s. The mechanism `end_when_standing_s` exists, but no scenario of the suite uses it.

- **The owner's decision (27 November): two modes.** The inner loop ends scenarios early. The gates check the tails at full length:
  that is where it shows whether the body stands naturally after getting up, falls again or freezes.
- In the inner loop `suite.ps1` runs with the early end by default; `-Full` turns it off. A scenario ends when, after its last
  event, the body has stood for 5 s (`end_when_standing_s`) or lain at rest for 3 s (a new `end_when_at_rest_s`).
- A scenario takes the early end by a field in its file. Its window is no shorter than the longest time its checks read. The agent
  lists the scenarios, with the reason for each, in the stage's report.
- The gates and `gate.ps1`, the bootstrap and the taking of a baseline run at full length. A baseline knows its mode; comparing two
  modes is refused with a message that says so.
- **Acceptance:**
    - the suite's simulated time in the inner loop is 1300 s or less (1795 now);
    - every scenario that takes the early end has the same outcome and check verdicts as at full length;
    - full-length hashes unchanged.

### SP-10. Large files split

The ten sessions built in 137 calls, a median of 27 s, at most 231 s. An edit of any line of the controller compiles the whole file.

- `SandboxBodyController.cpp` (6037 lines, 278 KB) is split into files by area: balance and steps, the get-up and its lead, reactions
  and feeling, poses and targets, start and tuning. `ScenarioRunnerSubsystem.cpp` (3425 lines) likewise: the run, measurements,
  writing.
- The code moves unchanged. Ten files include the controller's header; those that need only its interface get a small header.
- **Acceptance:** a build after a set one-line edit in the get-up takes 60% or less of today's time (today's is taken in SP-0).
  Hashes unchanged.

### SP-11. Tiers of checks

- Scenario files get the field `"tier": "smoke"`. `suite.ps1 -Tier smoke` is about 20 scenarios: every state of the body and every
  kind of interaction at least once, in 20 s or less.
- In `CLAUDE.md`: the inner loop runs `smoke` and the step's own scenarios. The full suite and `gate` run at the end of a step, in
  the background while the report is written. The rule "a baseline before a change to the body, a comparison after it" stays for
  the end of the step.
- **Acceptance:** on the suite's recorded failures of November, `smoke` catches at least 80% of the cases where the full suite
  failed.

### SP-12. Comparison reports

In ten sessions the model spent about 4.9 h of 11.8 on its own scripts that read JSON, on `grep` over output and on its own batteries.

- `Tools\compare.ps1 -A <label> -B <label>` builds one table of the figures the agent reads most. These are the five analyses the
  transcripts show again and again:
    1. which scenarios' hashes moved and which figures moved in them;
    2. the stand by seed: how many bodies stood, how many at the first attempt, the share of assist;
    3. the cells of the matrix with another outcome;
    4. get-up times and the number of attempts;
    5. energy gained, assist share and joint stretch, the worst of the suite.
- `gate.ps1` prints that table itself at its end.
- **Acceptance:** the five analyses come from one command in 40 lines or fewer. Ten sessions later the agent writes at least half
  as many scripts of its own for reading results.

## 8. Not done and deferred

Nothing that changes the physics or makes runs differ is part of this work.

| Idea | Status | Why |
| --- | --- | --- |
| Fewer solver iterations or substeps, asynchronous physics | not done | 64 iterations is the only count that passes everything (M3, part 7, step 3); the step is fixed by the rules |
| Several bodies in one world | not done | a second body in a world differs by 0.03 cm from its third frame |
| Live Coding | not done | not allowed by the project's rules |
| Building changed files without optimisation | not done | it can change floating-point results, and the hashes with them |
| A cooked game instead of the editor | not done now | clips are read from their raw keys, which a cooked game does not have; content would have to be cooked after every change of an asset; every hash would change |
| A cache of results by content | deferred to the end of stage 2 | a false hit gives a wrong conclusion. Done if runs with the same build, tuning and scenario are more than 15% of all runs |
| Memory up to 64 GB | not done | the owner's decision of 27 November: no hardware is bought; the work fits 32 GB and 9 engine processes |
| Automatic syncing of the docs with Claude Docs | outside this document | 1.6 h of the model's time in ten sessions, but not about runs |

## 9. Order, estimates, risks and decisions

Three stages, about 5 to 6 days. After each one: the SP-0 measurement, the hash comparison and a report to the owner. Each stage
gains something by itself, so the work can stop after any of them. SP-0, the benchmark, runs before the first stage and after each.

| Stage | Requirements | Work | Waiting for runs, cumulative | Whole development, cumulative |
| --- | --- | --- | --- | --- |
| 1 | SP-0 to SP-3 | about 1 day | -15 to -20% | -8 to -10% |
| 2 | SP-4 to SP-6 | 2 to 3 days | -30% | -16% |
| 3 | SP-7 to SP-12 | about 2 days | -40% | -22%, about -5% more from SP-12 through the model's time |

A stage's gate is passed when the benchmark shows the stage's target times and no hash has moved. The estimates come from the
measured parts of a run (start, world, simulation, tails) and the mix of calls in ten sessions. At night the gain is larger (about
-27%), in sessions with the owner smaller (about -12%). The least certain are SP-8 and SP-9: they depend on the profile and on the
list of scenarios.

### Risks

| Risk | Where | How it is caught | Fallback |
| --- | --- | --- | --- |
| State leaks between the pool's jobs and hashes drift | SP-4, SP-6 | all 154 scenarios, the matrix and the stand compared in a pool that has run 1000 jobs | restart the process more often; without SP-6; `SANDBOX_SEPARATE_PROCESSES=1` |
| A build fails on DLLs held by another session's pool | SP-5 | `build.ps1` stops the pool and says what it waits for | `pool.ps1 -Stop` by hand |
| The forced exit loses the tail of a log or a result | SP-2 | 1000 jobs without `QUEUE WARNING`, logs that end with "done" | the orderly exit |
| The change of Zen breaks or slows the DDC | SP-2 | the start time and the mesh build in the log of the first job | the old setting, or the graph without Zen |
| The early end hides a late failure | SP-9 | a window no shorter than the longest check; the gates and the bootstrap run at full length | the field taken out of the scenario |
| In the lean mode a metric reads 0 in silence | SP-8 | a metric not computed is an error of the run | the full mode |
| The pool, films and a second session do not fit 32 GB | SP-4 | `pool.ps1` counts every engine process and starts none beyond 9 | the job waits for room |

### Owner's decisions

Taken on 27 November 2026, one question at a time:

1. **The Zen server stays up** (SP-2), if the measurement shows its initialisation at 0.3 s or less.
2. **The pool idles 10 minutes** (SP-4) and stops itself before any run in the headset.
3. **The early end has two modes** (SP-9): the inner loop ends early, the gates, the bootstrap and baselines run at full length.
4. **No memory is bought:** the work fits the 32 GB there are.
5. **This file is the source**, with a line in `CLAUDE.md` and the tab "Fast checks" of the plan in Claude Docs.
