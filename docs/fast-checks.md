# Faster checks and runs: where the time goes, requirements SP-0 to SP-12

Written on 27 November 2026 on the owner's request: development is slow because checks and test runs take long. This document
is the measurement of where the time goes, the requirements that cut it without changing any result of a run, and the owner's
decisions on them. It was audited the same day by a second session and an independent reviewer: every figure was checked
against the code, the engine's source, the logs and new measurements, and what changed is listed in section 10. The same text is
the tab "Fast checks" of the plan in Claude Docs (with two drawings: a chart of the suite's phases where this file has their
table, and the stages in section 9), kept in step by hand.

**State of this work:** decided and audited on 27 November 2026 (the owner's decisions are at the end of section 9). Stage 1
(SP-0 to SP-3) as built and accepted (decision 10): section 11. Stage 2's SP-6 and SP-4 (started by hand) as built (decisions 12
and 13, confirmed on 28 November): section 12. The rest of stage 2 and stage 3 are not begun; when they come is
[`docs/active-ragdoll.md`, section 8](active-ragdoll.md#8-order-of-work-and-plan-of-parts), and the project's status is
[`docs/PRD.md`, Status](PRD.md#status).

## Terms

| Term | Meaning here |
| --- | --- |
| Run | One scenario simulated in a world of its own: one job of a queue. A run of the whole suite is a "suite run" |
| Call | One shell command of the agent that builds or starts the engine (the unit of the transcripts' count) |
| Batch | The jobs that one tool hands to the queue or the pool at once (`Invoke-ScenarioBatch`); a call may make several |
| Inner loop | What runs after each edit within a step of development: the build, the tests, the smoke tier (SP-11) and the step's own scenarios |
| End of a step | The runs after a step's last edit that its report gives: the whole suite, the comparison with the baseline, the tools the step is judged by |
| Stage check | What closes a stage of this work: the benchmark (SP-0), the hash comparisons and a report to the owner |
| Gate | A milestone's gate of `docs/PRD.md`; this document uses the word for nothing else (struck decision 3 keeps its old words) |
| Check set | Several tools of runs put into one queue by `Tools\checks.ps1` (SP-3) |
| Pool | The engine processes of this machine that stay up between batches and take jobs: one pool, shared by every session (SP-4) |
| Hashes | The two hashes of a run, of all bodies and of all bones over every frame (`metrics.json`, `hashes.bodies` and `hashes.bones`) |
| Baseline | Kept results that later runs are compared with by their hashes (`baseline.ps1`); the benchmark's times are a "measurement", such as `bench_before` |
| N% faster, N% shorter | The time is N% less: 100 s made 40% faster take 60 s |

## 1. Why

The goal is to cut the agent's time on DictatorSandbox by about 22% (1.3 times as fast) without changing a single result of a
run. For that the waiting for runs has to be about 40% shorter.

Waiting for runs is about 54% of the agent's time today: 16.2 hours of 30.4 in the ten sessions of 25 to 27 November 2026 (7.7 h
of calls that start the engine in the foreground, 8.6 h of waits on work in the background, which include some waits for
subagents). The rest is the model's own work, 40%, and the other tools, 6%. The first count put the waiting in the night's
unattended window at 67% and in the sessions with the owner at 31%; those two figures were not counted again.

Most of the waiting is not physics. Of the suite's 61 s, the simulation takes 33 s, and physics only about 45% of that. One
`scenario.ps1` takes 10.7 s on an idle machine, of which 2 s are simulation.

Targets:

- the inner loop after an edit of the controller's `.cpp` (the build, the tests and the smoke tier of SP-11) in 35 s; the build,
  the tests and the whole suite take 87 s today;
- build, tests and the whole suite in 65 s instead of 87;
- one scenario in 4 s instead of 10.7;
- the bootstrap in 160 s instead of 338;
- every run keeps its hashes.

## 2. Where the time goes now

Of the suite's 61 s about 15 s are physics, and a single run spends less than a quarter of its time on useful work. Measured on
27 November on the owner's machine: Ryzen 7 9800X3D (8 cores, 16 threads), 32 GB (19.9 GB of it free with no engine running),
NVMe. Sources: the queue's process logs, the runs' `metrics.json`, `test.log`, `bootstrap.json`, the transcripts of ten agent
sessions, and the audit's runs of the same day (the scripts are in `docs/recon/fast-checks/`).

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
| `scenario.ps1 -Name getup_left` | 10.7 s | 2.0 s of simulation | the engine's start about 6 s (2.65 s of it waiting for the Zen server), the warm-up and the world 0.8 s, exit 0.4 to 0.7 s, PowerShell |
| `test.ps1` | 16 s | 1.6 s (59 tests) | the editor's start 7.5 s (2.65 s of it Zen); a fixed pause of the engine's automation, 5 s (`DefaultDelayTimer = 5.0f`) |
| `suite.ps1`, 154 scenarios, 8 processes | 60 s | 15 s of physics | the table above |
| `bootstrap.ps1` | 338 s | | 15 steps one after another (the steps of `bootstrap.json` add up to 336 s); each step starts its own engine, a record and replay twice |

Four more facts shape the requirements:

- When 8 processes start at once, each takes 6 to 14 s to its first job, 9.9 s on average. Much of it is a check of the
  platforms that every editor process asks UnrealBuildTool for at its start (`-Mode=ValidatePlatforms`). The calls run one at a
  time: the eight returned 0.3, 1.3, 2.4, 3.4, 4.4, 5.5, 6.5 and 7.5 s after the launch, 3.9 s on average. Without the check and
  without Zen the eight reached their first job in 4.8 to 5.6 s (SP-2). Each process takes 1.8 GB (eight peaked at 14.4 GB) and
  about 1.2 cores.
- Each job of the queue opens its world in about 0.41 s (`LoadMap`), and the skeletal mesh is built again inside it
  (`AssetCompile SkinnedAsset`); how much of the 0.41 s that is was not measured.
- A frame of simulation takes 1.54 ms on average, physics 0.65 ms of it. A passive body (`drop_passive`) takes 0.9 ms a frame
  with 0.34 ms of physics: part of the difference is the run's measurements, not the body.
- The processes of runs (`-game`) never write the asset registry's cache (`AssetDataGatherer.cpp`, lines 206 to 210: not when
  `!GIsEditor`). The tests' editor process writes it, 137 MB, during the automation's pause and not at its exit.

### How often and what runs

The ten sessions made 466 calls that built or started the engine (nine of the sessions started it), 393 of them in the foreground
and 73 started in the background: 16 an hour, a median pause of 2 min between them. 137 of them built, about once in 13 min, and
97 of those 137 ran the engine in the same call (a build and a scenario 32 times; build, tests and suite 15). The other 329 ran on
code already built. After a call that only built, the next run came after a median of 6 s (mean 12 s). The table's rows are the
first count's categories, one row a call (by the audit's count 40 calls only built, where the row says 42).

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

That is 7.7 h of waiting in the foreground, 70 s a foreground call on average (460.5 min over 393 calls). The agent waited 8.6 h
more on work in the background, mostly the night's batteries, stands and bootstraps.

## 3. Goals and acceptance

The work is accepted when the benchmark (SP-0) shows the target times of the table and every run's hashes match the baseline taken
before the work. A stage's column is met at its stage check; a row that no requirement's acceptance names (the seeds, the
scorecard, the bootstrap after stage 1) is met there too. "Now" was measured on an idle machine on 27 November, the rows of the
seeds and of `natural.ps1` on the same day's runs (not measured again in the audit); the benchmark measures all of it again as
`bench_before`. The later columns are estimates from the measured parts of a run; the audit measured the first changes of stage 1
(SP-2) as well.

| Measurement | Now | After stage 1 | After stage 2 | After stage 3 |
| --- | --- | --- | --- | --- |
| One scenario, `scenario.ps1 -Name getup_left` | 10.7 s | 7 s or less | 4 s or less | 4 s or less |
| 4 scenarios times 3 sets of tuning in one call (`getup_back`, `getup_front`, `getup_left`, `getup_right`; `getup.start_blend_s` 0.3, 0.6 and 0) | 22 s with `sweep.ps1`, 128 s as a loop of `scenario.ps1` | 18 s or less | 10 s or less | 10 s or less |
| `test.ps1` | 16 s | 8 s or less | 8 s or less | 8 s or less |
| `suite.ps1`, 154 scenarios | 60 s | 55 s or less | 45 s or less | 45 s or less |
| `suite.ps1 -Tier smoke` (SP-11), on a warm pool | | | | 12 s or less |
| Build, tests and suite after a one-line edit of the controller's `.cpp` | 87 s | 74 s or less | 65 s or less | 65 s or less |
| Build, tests and the smoke tier after the same edit (the inner loop) | | | | 35 s or less |
| `getup-seeds.ps1`, three seeds | 92 s | 82 s or less | 70 s or less | 60 s or less (70 s without SP-8) |
| `natural.ps1 -Quick` | 55 s | 45 s or less | 35 s or less | 35 s or less |
| `bootstrap.ps1` | 338 s | 200 s or less | 180 s or less | 160 s or less |

The inner loop's 35 s: the build 11 s, the tests 8 s while the pool starts again after the build (about 5 s with SP-2's flags),
the smoke tier on the warm pool 12 s at most, and the agent's PowerShell.

### The rule of the hashes

- Every requirement keeps the hashes in three checks: all 154 scenarios (`suite.ps1 -Baseline`), the push matrix and the stand on
  three seeds, both run by run by both hashes. SP-0 builds those two comparisons: `baseline.ps1 -CompareMatrix` compares outcomes
  and state sequences, the matrix's result file holds no hash, and the stand's (`getup-stand.json`) holds only `hashes.bodies`.
- A change to the queue, the start or the pool (SP-1 to SP-6) changes a path that every tool shares. Before it, the runs of each
  caller of `Invoke-ScenarioBatch` (the suite, the matrix, the knockdowns, the stand and the seeds, `natural.ps1`,
  `compliance.ps1`, `grab-pulls.ps1`, `sweep.ps1`, `swing-lag.ps1`, `recording-report.ps1`) and of `scenario.ps1` with its callers
  are kept by their hashes (`baseline.ps1 -SaveRuns`, SP-0); after it, each runs once with its defaults and gives the verdicts it
  gave and the same hashes (`-CompareRuns`).
- A name too many in the list of changed scenarios is a defect. The scenario is run again first, then the cause is looked for;
  "it differed once" is not accepted.

### SP-0. Measuring before and after

- `Tools\bench.ps1 -Label <name>` runs the measurements of the table three times and writes the medians to
  `Saved\Bench\<name>.json`. One pass over the table takes about 6 minutes today, three passes about 20; `-Full` adds one
  bootstrap (about 6 minutes). The 4 scenarios times 3 sets are measured as one call of `sweep.ps1`; the loop's 128 s is the
  audit's figure and is not measured again.
- A build row is measured by setting the time stamp of a source file, its content unchanged: the controller's `.cpp`. `-Builds`
  adds the two that decision 8 asks for, the controller's header (`SandboxBodyController.h`) and a full build. No script changes
  a source file (source files are edited with the editor: `CLAUDE.md`).
- Its suite runs as `suite.ps1 -Label bench_<name>`, which without `-Tune` writes to `Saved\Suites\bench_<name>` as it does with
  it, and its other runs write under `Saved\Bench\<name>`: never to `Saved\Scenarios` or `Saved\Tools\suite.json`, which
  `baseline.ps1 -Compare` reads. `-Full` runs the bootstrap as it is, which does write them, so a bench with `-Full` belongs to a
  stage check and never falls between a step's `-SaveBaseline` and its `-Baseline`.
- It measures on an idle machine. It refuses, and says why in its first line, while an engine process runs that it did not start
  (another session's run, the pool while it holds another session's jobs, the owner's editor) or less than 18 GB of memory is
  free. From stage 2 on it starts the pool itself when no other session uses it, and stops it at the end.
- It brings the comparisons the rule of the hashes needs, by both hashes of each run: `baseline.ps1 -SaveRuns <folder> -Name
  <name>` keeps the hashes of every `metrics.json` under a tool's run folder (the matrix's `Saved\Matrix\push\runs`, a stand's
  `runs`, any tool's), and `-CompareRuns <folder> -Name <name>` names every run whose hashes differ or that is missing; the stand
  of three seeds is compared trial by trial in the same way.
- The measurement `bench_before` is taken before the first change, and the benchmark is run again after each stage. A stage's
  report gives both columns.
- The transcripts are counted by the scripts in `docs/recon/fast-checks/` (`ten_sessions.ps1`). Ten sessions after the last stage
  they are counted again the same way. The goals: the mean wait of a foreground call that starts the engine 40% shorter (70 s
  now: 460.5 min over 393 calls), and the share of waiting in the agent's time 41% or less instead of about 54% (40% less
  waiting with the rest as it is gives 0.324 of 0.784, 41.3%). `own_scripts.ps1` counts the agent's own scripts for SP-12: the
  `.ps1` and `.py` files written under a scratchpad or `Saved\Recon`, 173 distinct files (346 writes) in the ten sessions.

## 4. Constraints

The speed-up must break no rule of `CLAUDE.md`. The ones this work touches:

- **Determinism.** The first run of a process differs from the later ones, so the warm-up (`batch_warmup.json`) stays in every new
  process. The solver's determinism mode and `KeepSolverInStep` are not touched. Every job still runs in a fresh world, and a job's
  hashes may not depend on what ran before it in its process: today they do not (three chains gave the same hashes:
  `docs/testing.md`, "M3 part 4").
- **Measuring changes nothing.** Timing scopes for the profile (SP-7) and the lean mode of measurements (SP-8) leave a run's hashes
  as they were; that is checked as for every new metric.
- **Fixed step.** 1/90 s, 2 substeps, 64 solver iterations on the NPC stay. Only a pool process that waits between jobs reads the
  clock, and it sleeps; simulation code does not. With a fixed timestep the engine does not limit its frame rate
  (`docs/pitfalls.md`), so a process with nothing to do must sleep, not tick.
- **The DLLs are locked by every engine process of the project.** A live pool, a test run, a film or a session keeps the project
  from building, so `build.ps1` stops the pool and waits for the other engine processes of this project before it builds (SP-5).
  Live Coding is not used.
- **Memory.** At most 9 engine processes at a time, and every start leaves at least 1.5 GB free. With no engine running 19.9 GB
  of 31.7 are free (the owner's other programs hold the rest), and eight processes peaked at 14.4 GB, so eight and the tests fit
  (16.2 GB) and little else does. Every launcher (`Invoke-Editor`, `Invoke-ScenarioQueue`, the pool, `play.ps1`) goes through one
  count of the engine processes and the free memory of the machine, and an idle pool process gives way first. A rendered process
  (a film, `-Shot`, a session) takes more than 1.8 GB; how much is measured in SP-1 before films run side by side. The owner does
  not buy hardware.
- **Two agent sessions at once, in one working tree.** So it was on 27 November. There is one pool for the machine and both
  sessions' batches go to it; they must not mix up each other's jobs, and no session stops the pool or ends jobs in silence while
  the other session's batches are in it: a build waits for them and says whose they are (SP-5). Both sessions write the same
  result folders (`Saved\Scenarios`, `Saved\Tools\suite.json`, the matrix's and the stand's): a tool that writes there holds a
  lock on its folder, and another session's tool that finds it held refuses and names the holder. Every run records the build it ran on, and a tool whose runs ran on two builds says so (SP-4).
- **Scripts.** Windows PowerShell 5.1. The first line is the verdict, the result is JSON under `Saved\`, success is decided by the
  result files and not the exit code: the bootstrap too, which decides by exit codes today (`bootstrap.ps1`, line 38). A tool
  deletes what it is to make before it runs (`docs/pitfalls.md`: a tool that checks only that its output exists passes an old file
  as new). Source files are edited with the editor, not with shell one-liners.
- **The DDC.** Runs, sessions without the headset and the tests use the cache graph without the Zen server (decision 6).
  `Config\DefaultEngine.ini`, the owner's editor, the generator, the lighting bake and anything in the headset are not changed.
- **Modules.** The queue and the measurements live in `SandboxScenario`. Splitting the controller (SP-10, if it is done) adds no
  dependency between `SandboxBody` and `SandboxInteraction`.
- **Headset.** The pool runs with `-nohmd -DisablePlugins=OpenXR -nullrhi`; no pool process is started while a run in the headset
  is going (SP-4, SP-5); runs in the headset happen only on the owner's word, as now.

## 5. Stage 1: the cheap requirements

Stage 1 is 2 to 3 days of work and cuts the waiting by about 20 to 25% (an estimate). It needs no pool and changes no hash.

### SP-1. Several runs in one call

In ten sessions the agent ran `scenario.ps1` in a loop, one scenario at a time, 76 times and waited 61 min. Each such run takes
10.7 s, 2 s of it simulation. `sweep.ps1` already runs scenarios times settings through one queue (the table's 4 scenarios times 3
settings in 22 s, against 128 s as a loop); what it lacks is a set of several values changed together, the files' own values as
a set, and a verdict line a run.

- `sweep.ps1` takes sets of tuning as well as axes: `-Sets "|getup.start_blend_s=0.6|getup.start_blend_s=0,getup.carry=0"`, the
  sets separated by `|` and an empty set meaning the files as they are; `-Vary` or `-Sets`, not both. Every run is one job of one
  queue on up to 8 processes, its results under `Saved\Sweeps\<name>\runs\<set>\<scenario>` as now.
- It prints a verdict line a run, as `scenario.ps1` does (`Format-ScenarioVerdict`, with the hashes), after the summary line
  `SWEEP DONE`, which stays first.
- `scenario.ps1` keeps its interface, its output and its exit codes: one run, `Saved\Scenarios\<name>` by default. Its callers rely
  on them: `film.ps1` (its first line, which `film-pair.ps1` reads), `pose-prior.ps1`, `record-replay.ps1` and
  `surface-friction.ps1` (they read `Saved\Scenarios\<name>` or the line), `repeatability.ps1` (exit code 2 and a process a run),
  and the path of `Invoke-ScenarioBatch` with `SANDBOX_SEPARATE_PROCESSES=1`.
- `film.ps1` takes several films in one call (`-Name "a,b"`) and renders them through one queue; `film-pair.ps1` films both sides
  at once. Rendered jobs run in processes of their own, grouped by what belongs to the process (rendering, overlays, console
  variables: the header comment of `ScenarioBatchSubsystem.h`). How many rendered processes run side by side is set by the memory
  one takes, measured first.
- `CLAUDE.md` gets the rule: no loops over tools that start the engine; several runs go through one `sweep.ps1`, several films
  through one `film.ps1`.
- **Acceptance:** the table's 4 scenarios times 3 sets in 18 s or less, and every run's hashes match its single run with
  `scenario.ps1`; two films in one `film.ps1` call take no more than 1.3 times one.

### SP-2. Start and exit flags

Every start of the engine pays 3 s alone, and up to 10 s when eight start together, for things a run does not need; the tests pay
5 s more.

1. **The DDC without Zen.** The DDC's server ends with the last process that uses it, and every start waits 2.3 to 2.7 s to launch
   it again. The owner's decision 6 (27 November, replacing decision 1): the engine processes of runs and tests start with the
   cache graph without Zen, `-ddc=InstalledNoZenLocalFallback`, given by the queue's arguments (`Get-QueueArguments`, which
   `Invoke-ScenarioQueue` and `scenario.ps1` use), by `test.ps1` and by `play.ps1` without `-VR`; not by `gen-assets.ps1`,
   `bake-lighting.ps1`, `metahuman.ps1` or a run in the headset, and `Config\DefaultEngine.ini` stays as it is. Measured: one
   run 7.7 s instead of 10.7, the suite's queue 55.7 s instead of 57.6, 154 of 154 hashes the same. Keeping the server up instead (`[Zen.AutoLaunch] LimitProcessLifetime=False`) was
   as fast, 7.8 s, but the server is shared by every project and engine version on the machine and that setting is part of its
   run context (`ZenServerInterface.h`, lines 208 to 211): a process with the other setting shuts the running server down and
   starts its own ("Found existing instance running on port 8558 with different run context, will attempt shut down", measured),
   so our runs and the owner's editor of another project would shut it down under each other.
2. **The automation's pause.** `test.ps1` gets the command `Automation Now;RunTests <filter>;Quit` (today `Automation RunTests
   <filter>;Quit`, line 16): `Now` is a command of its own between semicolons (`AutomationCommandline.cpp`, lines 606 to 616).
   `RunTests Now <filter>` sets the pause to 0 and queues no test: measured, 0 tests and exit code 0. Measured: 10.2 s, and 7.1 s
   with item 1; 59 of 59 tests.
3. **Exit.** When the queue is empty, a process without a renderer exits by force (`RequestExitWithStatus(true, ...)` in
   `UScenarioBatchSubsystem`): the engine tells its will-terminate delegate, flushes the log (`GLog->Flush`) and ends the process
   (`TerminateProcess`; `WindowsPlatformMisc.cpp`, lines 1486 to 1515) without its orderly shutdown, which takes 0.4 to 1.3 s a
   process (0.9 s on average in the audit's run of the suite; the table of section 2, from another run, has 1.4 s). Not in a
   rendered process, whose pictures the engine writes in the background, and not in a process with a trace (SP-7). The process log then
   ends with the queue's "Scenario batch: queue empty" and the engine's exit request, without "Log file closed"; nothing decides
   by the process's exit code (`scenario.ps1` only prints it).
4. **The platform check.** The calls to UnrealBuildTool at the start run one at a time (section 2): 3.9 s on average when 8
   processes start together. The engine skips the call when the environment variable `UE_SKIP_UBT_SDK_SETUP` is 1
   (`TargetPlatformManagerModule.cpp`, lines 54 and 223 to 226); the scripts set it for the engine processes of item 1 alone.
   Measured with item 1: the eight reached their first job in 5.3 s on average instead of 9.9, the suite's queue took 53.2 s
   instead of 57.6, one run 7.3 s instead of 10.7; 154 of 154 hashes the same. (`-Multiprocess` skips the call as well, line 390,
   but measured slower: the suite's queue 65.1 s, one run 8.8 s; not taken.)

- **Acceptance:** one scenario in 7 s or less, `test.ps1` in 8 s or less, the suite in 55 s or less. No log of a run's process
  has a line of `LogZenServiceInstance`. No job of 1000 is lost (`QUEUE WARNING`). A rendered run (`-Shot`) and `gen-assets.ps1`
  work with the graph. Hashes unchanged, and every caller as the rule of the hashes says.

### SP-3. Check sets through one queue

The bootstrap, `natural.ps1` and the agent's own batteries run tools one after another. Each tool starts its processes again, and
the tail of one is not overlapped by the start of the next.

- A tool of runs is split into its plan (the jobs), the run and its judgement (the verdict from the result files). A plan may have
  rounds: it is asked again with the results of the last round until it gives no more jobs. `getup-stand.ps1` already works so (up
  to six rounds that draw again the bodies that did not fall: lines 295 to 320), `grab-pulls.ps1` has to (the standing run, the
  probes placed from it, the pulls placed from the probes: lines 96, 128 and 186). The suite's second run of a scenario over the
  step limit stays a run alone, after the queue, never a round inside it. Alone each tool works as now.
- The tools: `suite`, `push-matrix`, `knockdowns`, `getup-stand`, `getup-seeds` (three stands in one queue instead of three
  PowerShell processes with three engine processes each), `compliance`, `natural` (its own set, `compliance` and the seeds in one
  queue; today it calls the two as scripts, lines 98 to 109), and `grab-pulls` last.
- A new `Tools\checks.ps1 -What suite,matrix,knockdowns,seeds -Tune ... -Label <name>` puts the plans of its tools into one queue
  on up to 8 processes, the longest jobs first, and gives each tool the rounds it asks for. At the end it prints every tool's
  verdict and one table of comparison with the baseline (SP-12).
- The bootstrap runs one `checks.ps1` after the build, `gen-assets` and `bake-lighting`, with the tests first. Sessions and record
  and replay (`play.ps1 -Smoke`, `-Demo`, `record-replay.ps1`) run one at a time beside the queue, as its ninth process.
  `bootstrap.json` keeps one entry per step, as now, with its verdict and its seconds (from its first job's start to its verdict,
  so the steps overlap), and the bootstrap decides by the steps' result files.
- In `CLAUDE.md`, `checks.ps1` replaces the batteries in `Saved\Recon`.
- **Acceptance:** the bootstrap in 200 s or less (338 now), with the same verdict for every step; `checks.ps1 -What
  suite,matrix,knockdowns,seeds` starts its processes once and keeps its queue full to the end, no process idle while a job waits,
  and takes less time than the four tools one after another on the same build after SP-2 (decision 10, in place of "at least 35%
  less": measured 21% less, 147.4 s against 185.4 s; the rest is the work of the runs, stages 2 and 3). Hashes unchanged.

## 6. Stage 2: a pool of warm processes

Stage 2 takes the engine's start out of most calls: 329 of 466 ran on code already built. It is 3 to 4 days of work and, after
stage 1, gains about 3 s on a call of one process and 10 to 12 s on a call of eight (the start, and SP-6's worlds).

### SP-4. A pool that stays

- `UScenarioBatchSubsystem` gets the mode `-ScenarioPool=<folder>`, the folder under `Saved\Pool` (not under `Saved\Queue`, whose
  folders older than an hour `Invoke-ScenarioQueue` removes: `common.ps1`, lines 263 to 268). With no job to do, a pool process
  opens an empty world and sleeps between looks at the folder, every 50 ms, so that it takes under 2% of a core. Jobs are taken by
  the same marker under a lock file (`TakeJob`).
- A pool process exits by itself when it has been idle longer than `-PoolIdleS` (10 minutes, decision 2), when a file `stop`
  appears, when what it read once at its start has changed (below), after 500 jobs, or after a job when its memory has passed
  2.2 GB or the machine has less than 1.5 GB free. An idle one also exits when another engine start needs its room (section 4).
  The warm-up runs once a process.
- What a process reads once, and so the fingerprint it compares before every job: the project's DLLs, its `Config\*.ini` (not
  `Config\Tuning`, which every job reads anew: `ResetForRun`), everything under `Content` (the generated assets, the clips, the
  maps that `bake-lighting.ps1` saves again, the MetaHuman), `Specs\motion_library.json` (with SP-6), `Tools\batch_warmup.json`,
  the pose database `Saved\PosePrior\prior.bin` (`FNaturalMeasures` keeps every database it has read for the life of the
  process: `NaturalMeasures.cpp`, lines 690 to 695), and the engine's build. The DLLs cannot change under a live process
  (Windows does not overwrite a loaded DLL: the build fails), which is why `build.ps1` stops the pool first (SP-5); they are in
  the fingerprint so that every run records the build it ran on.
- `Tools\pool.ps1 -Start | -Stop | -Status` starts 8 processes, stops them or shows the state, kept in `Saved\Pool\pool.json`:
  processes, the fingerprint, busy and idle, whose jobs. Before any run in the headset (`play.ps1 -VR`, `scenario.ps1 -VR`) the pool
  stops by itself (decision 2).
- `Invoke-ScenarioBatch` and `scenario.ps1` (which starts its own process today, not through `Invoke-ScenarioBatch`) hand their
  jobs to a live pool with the same fingerprint. Without one they start processes as now, within the memory rule. Jobs with
  `-CVars` and rendered jobs always run in processes of their own, and so do runs that must be alone for their timing: the
  suite's second run of a scenario over the step limit, `suite.ps1 -Parallel 1`, `repeatability.ps1`. (A batch of one job, such
  as the standing run of `grab-pulls.ps1`, may go to the pool.) `SANDBOX_SEPARATE_PROCESSES=1` still turns off both the queue and
  the pool.
- A batch waits for its own jobs: each one ends with a result, or its process has died (the marker names the process) and the
  job is reported lost, `QUEUE WARNING` as now, and never run again in silence. A job of the pool has no `process.log` of its
  own: `scenario.ps1` points at the pool process's log on an error.
- Batches of a few jobs go before the jobs of a long batch, so that one scenario does not wait behind another session's suite.
  Every job file names its batch: the session, the tool, the label.
- Every run writes the fingerprint it ran on into `metrics.json`; a tool whose batches ran on more than one, or not on the one
  current when it began, says so (`QUEUE WARNING`), and its comparison with a baseline does not count.
- The header comment of `ScenarioBatchSubsystem.h` still says that a job is taken by moving its file; it is put right with this
  part.
- **Acceptance:**
    - one scenario on a warm pool in 4 s or less, the suite in 45 s or less;
    - the hashes of the suite, the matrix and the stand in the pool match fresh processes: in a fresh pool, in processes near
      the end of their 500 jobs, after 10 minutes idle, and after jobs on another physics asset (`SANDBOX_PHYSICS_ASSET`);
    - an idle pool process takes under 2% of a core and its memory does not grow;
    - after `build.ps1` no job runs on the old build, and the batches of two sessions given at once all end with their own
      results.

### SP-5. The pool and the build

- `build.ps1` stops the pool before building: it takes no new batches, waits for the batches that are running (5 minutes at
  most) and says whose they are. If they have not ended in 5 minutes it builds nothing and says so (`BUILD ERROR`, with the
  batches and their sessions); it never ends them itself. It also waits for the other engine processes of this project (tests,
  films, sessions), which hold the DLLs too, and names them.
- After a build that succeeds it starts the pool in the background (`-NoPool` turns that off), unless a run in the headset is
  going. 97 of the ten sessions' 137 builds ran the engine in the same call, and after a build that came alone the next run
  followed after a median of 6 s: the pool is not warmed while the model thinks. What overlaps is the tests: the pool's start
  (about 5 s with SP-2's flags, 7 to 10 s without) goes on while `test.ps1` runs (8 s), which the memory rule allows (8 processes
  and 1).
- `gen-assets.ps1` and `bake-lighting.ps1` stop the pool before and start it after as well.
- **Acceptance:** build, tests and suite in 65 s or less; two builds in a row from two sessions lose no job, and no run runs on a
  build other than the one its batch was planned on.

### SP-6. Assets kept between jobs

Every job now spends about 0.41 s opening its world: the mesh is unloaded by the garbage collector and built again. That is about
8 s of the suite and about 7 s of a stand of three seeds.

- After its warm-up a process loads a fixed set and keeps strong references to it for its life: the NPC's skeletal mesh, the
  physics assets that scenarios and `SANDBOX_PHYSICS_ASSET` name, and the clips of `Specs/motion_library.json` and of the poses.
  The world and all its actors are made anew, as now. A fixed set, not "what the previous job loaded", so that what a job finds in
  memory does not depend on the job before it. (The warm-up runs the stock mannequin, `Tools\batch_warmup.json`, so today every
  process's first job builds the NPC.)
- **Acceptance:** a job's world in 0.15 s or less; `AssetCompile SkinnedAsset` lines only before a process's first job; the suite's
  hashes the same in two orders of its jobs (the longest first, as now, and the reverse); a process's memory grows by 200 MB or
  less over 500 jobs.

## 7. Stage 3: the frame, the tiers and the reports

Stage 3 cuts the work of a run itself and the model's time on reading results. 2 to 3 days. SP-8 starts only after SP-7's
measurement and SP-10 only after SP-0's (decision 8); SP-9 is postponed (decision 7).

### SP-7. A profile of the frame

0.9 ms of a 1.54 ms frame are not physics, and what spends them has not been measured.

- One long scenario with a get-up (`getup_laid_beside_table_back`, 25 s) and one passive one (`drop_passive`) run alone with a
  trace (`-trace=cpu,frame -statnamedevents`), in a process that exits in order.
- Our code has no timing scopes. The parts to be told apart get them (`TRACE_CPUPROFILER_EVENT_SCOPE`): the body's controller by
  its stages, Physics Control's update, the run's measurements (penetration, `FNaturalMeasures`, the balance probe, the writing of
  `series.csv`, `hands.csv` and `trace.json`). The scopes change no hash, checked as for a new metric.
- The trace is read without a window, as Epic's `ExportTimerStatisticsFromUtrace.cs` does it: `UnrealInsights.exe
  -OpenTraceFile=<file> -unattended -autoquit -noui -nullrhi -ExecOnAnalysisCompleteCmd="TimingInsights.ExportTimerStatistics
  <file.csv> -threads=GameThread"`. A new `Tools\profile.ps1` runs both scenarios and prints the table.
- **Result:** a table in `docs/testing.md`, the frame's share by part. It decides what SP-8 takes.

### SP-8. A lean mode of measurements

- A job of the queue gets the field `measure`, the list of the metrics its tool reads; the run computes those and leaves out the
  costly measurements that SP-7 found and that are not in the list.
- What a tool reads is listed in its plan, taken from the tool's code, files included. Today the stand reads `run.log` (when the
  body stood, lines 333 to 337), `trace.json` (how near the furniture it came, line 349), `metrics.fell`, `control.get_up`, the
  effectors, the checks, stretch, the limit violation, joint speed, penetration into the world, `metrics.stillness` (from
  `FNaturalMeasures`) and `control.authority` (lines 381 to 439), and the seeds C19 and C27 from those; the matrix reads the
  states, the checks, the balance, the steps, the stagger, the assist, stretch, joint speed and drift (`push-matrix.ps1`, lines
  150 to 192); the knockdowns read energy, penetration into the world and into the body itself and the fall's measures; a sweep
  reads its `-Metrics`. So the lean mode can leave out only the measures and files that none of the tool's readers use, and SP-7
  says how much of the frame that is.
- A metric the lean mode did not compute is absent from `metrics.json`, not 0, a file it did not write is absent, and a tool that
  reads either fails its run (an absent figure read as 0 has hidden a defect once already: `docs/pitfalls.md`).
- The stand, the seeds and the matrix use it; the suite, the scorecard and sweeps without `-Metrics` run with full measurements.
- SP-8 is done only if SP-7 shows that what the lean mode can leave out is at least 20% of the stand's frame.
- **Acceptance:** hashes and verdicts are the same in the lean and the full mode; the stand's simulation is shorter by at least
  four fifths of the share SP-7 found.

### SP-9. Early end of scenarios: postponed

Postponed by the owner's decision 7 (27 November, after the audit; it replaces decision 3). 65% of the suite's simulated time,
1158 s of 1795, comes after the last input, but most of it is a body that stands or lies after its last change of state. The 33
get-up scenarios (`getup_*`) last 16 to 30 s, 17 of them 16 s, and their bodies are up by 6 to 20 s. If every scenario ended 5 s
after it began to stand for good, or 3 s after it came to rest lying, the suite would simulate 286 s less: 16%, 4 to 5 s of a
suite run. The smoke tier (SP-11) does more for the inner loop, and the tails, where it shows whether the body stands
naturally after getting up, falls again or freezes, keep running at full length everywhere.

What carries over if it comes back, after stage 3: a full-length run records the hashes at the frame where the early end would
have come, so that one full-length baseline serves both modes; the last input counts the hands' and feet's paths and a recording
(the runner counts only `t + duration_s` of the events, `ScenarioRunnerSubsystem.cpp`, line 333, and in 60 of the 154 scenarios
a path ends later); `film.ps1`, like `film-pair.ps1`, takes the field out; an end "at rest lying" only for a scenario whose body
does not get up. `end_when_standing_s` stays as the stand uses it (3 s, `getup-stand.ps1 -StopStandingS`).

### SP-10. Large files split: measured first

The ten sessions built in 137 calls, a median of 27 s, at most 231 s. A build after a one-line edit of the controller's `.cpp`
takes 11.3 s, and of the runner's 12.7 s (measured by setting the files' time stamps): the median comes from edits of headers and
from full builds.

- SP-0 measures three builds (`bench.ps1 -Builds`): the controller's `.cpp`, its header `SandboxBodyController.h` (included by nine
  other files) and a full build, each with the compile time of every file from the build's log. SP-10 is done only if the
  estimate gives a build after a header edit at least 30% shorter (decision 8); otherwise it goes to section 8. The estimate: the
  header edit's build less four fifths of the two longest compiles in it (the controller's and the runner's, each split into
  about five files of a fifth of the size).
- If it is done: `SandboxBodyController.cpp` (6273 lines, 279 KB) is split into files by area (balance and steps, the get-up and its
  lead, reactions and feeling, poses and targets, start and tuning) and `ScenarioRunnerSubsystem.cpp` (3590 lines) likewise (the
  run, measurements, writing). The code moves unchanged, and files that need only part of the controller's interface get a small
  header. `SandboxBody.Build.cs` sets `bUseUnity = false`: the module has 25 source files, and UBT builds a game module from unity
  files at 32 (`MinGameModuleSourceFilesForUnityBuild`, `TargetRules.cs`, line 1936), which would change what each compiler run
  sees. The build is `/fp:precise` without FMA instructions, so moving code between files should change no result; the hashes
  check it.
- **Acceptance:** the build after the header edit at least 30% shorter than SP-0's figure; hashes unchanged.

### SP-11. Tiers of checks

- Scenario files get the field `"tier": "smoke"`. `suite.ps1 -Tier smoke` is about 20 scenarios, every state of the body and every
  kind of interaction at least once, chosen by that rule before the failures below are looked at; it runs in 12 s or less on a
  warm pool and in 20 s or less without one.
- In `CLAUDE.md`: the inner loop runs `smoke` and the step's own scenarios. The whole suite, the comparison with the baseline and
  `checks.ps1` run at the end of a step, in the background while the report is written. The rule "a baseline before a change to
  the body, a comparison after it" stays for the end of the step.
- **Acceptance:** of the suite runs of November that failed, `smoke` catches at least 80% (a suite run is caught when one of the
  scenarios it failed is in `smoke`). The failed runs are read from what is kept: the 61 baselines under `Saved\Baselines` (12 of
  them have a failure, of 15 scenarios), the suites' outputs kept under `Saved\`, and the "SUITE FAILED" lines of the transcripts;
  SP-11's own script in `docs/recon/fast-checks/` gathers them. All of it is on this machine only.

### SP-12. Comparison reports

In ten sessions the model spent about 4.9 h of 12.1 on its own scripts that read JSON, on `grep` over output and on its own
batteries (the first count, `model_split.ps1`).

- `Tools\compare.ps1 -A <label> -B <label>` builds one table of the figures the agent reads most. These are the five analyses the
  transcripts show again and again:
    1. which scenarios' hashes moved and which figures moved in them;
    2. the stand by seed: how many bodies stood, how many at the first attempt, the share of assist;
    3. the cells of the matrix with another outcome;
    4. get-up times and the number of attempts;
    5. energy gained, assist share and joint stretch, the worst of the suite.
- `checks.ps1` prints that table itself at its end. A figure that is missing is printed as missing, never as 0.
- **Acceptance:** the five analyses come from one command in 40 lines or fewer. Ten sessions later the agent writes at most half
  as many scripts of its own: 87 distinct `.ps1` and `.py` files under a scratchpad or `Saved\Recon` or fewer, against 173 in the
  ten sessions of this document (`docs/recon/fast-checks/own_scripts.ps1`).

## 8. Not done and deferred

Nothing that changes the physics or makes runs differ is part of this work.

| Idea | Status | Why |
| --- | --- | --- |
| Fewer solver iterations or substeps, asynchronous physics | not done | 64 iterations is the only count that passes everything (M3, part 7, step 3); the step is fixed by the rules |
| Several bodies in one world | not done | a second body in a world differs by 0.03 cm from its third frame |
| Live Coding | not done | not allowed by the project's rules |
| Building changed files without optimisation | not done | it can change floating-point results, and the hashes with them |
| A cooked game instead of the editor | not done now | clips are read from their raw keys, which a cooked game does not have; content would have to be cooked after every change of an asset; every hash would change |
| The Zen server kept up between processes (`LimitProcessLifetime=False`) | not done (decision 6) | its run context is shared by every Unreal process of the machine, and one with the other setting shuts it down; the graph without Zen is as fast |
| The asset registry's cache not written (`-NoAssetRegistryCacheWrite`) | not done | the processes of runs never write it; the tests write it during the automation's pause, not at exit |
| `-Multiprocess` for the processes of runs | not done | it skips the platform check, but the suite's queue took 65.1 s instead of 55.7 |
| Early end of scenarios (SP-9) | postponed (decision 7) | at most 16% of the suite's simulated time, 4 to 5 s of a suite; the smoke tier does more for the inner loop |
| Large files split (SP-10) | measured first (decision 8) | a build after an edit of the controller's `.cpp` is 11.3 s; done only if a header edit gains 30% or more |
| A cache of results by content | deferred to the end of stage 2 | a false hit gives a wrong conclusion. Done if runs with the same build, tuning and scenario are more than 15% of all runs |
| Memory up to 64 GB | not done | the owner's decision of 27 November: no hardware is bought; the work fits 32 GB and 9 engine processes |
| Automatic syncing of the docs with Claude Docs | outside this document | 1.6 h of the model's time in ten sessions, but not about runs |

## 9. Order, estimates, risks and decisions

Three stages, about 7 to 10 days (the first count said 5 to 6, the audit's reviewer 9 to 12). After each one, the stage check:
the SP-0 measurement, the hash comparisons and a report to the owner. Each stage gains something by itself, so the work can stop
after any of them. SP-0, the benchmark, runs before the first stage and after each.

| Stage | Requirements | Work | Waiting for runs, cumulative | Whole development, cumulative |
| --- | --- | --- | --- | --- |
| 1 | SP-0 to SP-3 | 2 to 3 days | -20 to -25% | -11 to -14% |
| 2 | SP-4 to SP-6 | 3 to 4 days | -30 to -35% | -17 to -19% |
| 3 | SP-7, SP-11, SP-12; SP-8 and SP-10 if their measurements take them | 2 to 3 days | about -40% | about -22%, a little more from SP-12 through the model's own time |

A stage check is passed when the benchmark shows the stage's target times and no hash has moved. The percentages are estimates
from the measured parts of a run (start, world, simulation, exit) and the mix of calls in the ten sessions. At night the gain is
larger, in sessions with the owner smaller. The least certain are SP-8, which depends on the profile, and SP-11, which depends on
what the agent runs in the inner loop.

### Risks

| Risk | Where | How it is caught | Fallback |
| --- | --- | --- | --- |
| State leaks between the pool's jobs and hashes drift | SP-4, SP-6 | the suite, the matrix and the stand compared in a fresh pool, after 500 jobs, after 10 minutes idle, after another physics asset, and in two orders | restart the process more often; without SP-6; `SANDBOX_SEPARATE_PROCESSES=1` |
| A tool's runs run on two builds | SP-4, SP-5 | every run's fingerprint in `metrics.json` | the tool is run again |
| A build fails on DLLs held by the pool while another session's batches are in it, or by its runs | SP-5 | `build.ps1` waits, names them and refuses after 5 minutes | `pool.ps1 -Stop` by hand, after the other session's batches have ended |
| An idle pool takes the processor | SP-4 | under 2% of a core a process, measured | a longer interval between looks |
| The forced exit loses the tail of a log or a result | SP-2 | 1000 jobs without `QUEUE WARNING`, every process log holding "Scenario batch: queue empty" | the orderly exit |
| The graph without Zen slows a rendered run, a build of derived data or the generator | SP-2 | the start and the mesh's build in the log of the first job; a `-Shot` run; `gen-assets.ps1` | the default graph for that kind of process |
| In the lean mode a metric reads 0 in silence | SP-8 | a metric not computed is absent, and reading it fails the run | the full mode |
| The pool, films and a second session do not fit 32 GB | SP-1, SP-4 | one count of engine processes and free memory for every start; idle pool processes give way | the start waits for room and says so |

### Owner's decisions

Taken on 27 November 2026, one question at a time; 6 to 9 after the audit, 10 and 11 after stage 1's check:

1. ~~**The Zen server stays up** (SP-2), if the measurement shows its initialisation at 0.3 s or less.~~ Replaced by decision 6.
2. **The pool idles 10 minutes** (SP-4) and stops itself before any run in the headset.
3. ~~**The early end has two modes** (SP-9): the inner loop ends early, the gates, the bootstrap and baselines run at full
   length.~~ Replaced by decision 7.
4. **No memory is bought:** the work fits the 32 GB there are.
5. **This file is the source**, with a line in `CLAUDE.md` and the tab "Fast checks" of the plan in Claude Docs.
6. **The processes of runs, of sessions without the headset and of the tests use the DDC without the Zen server**
   (`-ddc=InstalledNoZenLocalFallback`, SP-2); the project's `DefaultEngine.ini` and the owner's editor stay as they are.
7. **The early end (SP-9) is postponed**: the inner loop is made fast by the smoke tier (SP-11), and every scenario runs at full
   length. It is looked at again after stage 3.
8. **SP-10 is measured first**: SP-0 measures a build after an edit of the controller's `.cpp`, of its header and a full one, and
   the files are split only if a header edit gains 30% or more.
9. **Stage 1 comes now**, before R2's step 9; after its stage check the owner says whether stage 2 follows at once or R2 first.
10. **Stage 1 is accepted, and SP-3's acceptance is the queue kept full** (27 November, after stage 1's check): the check set starts
    its processes once and keeps its queue full to the end, in place of "35% less than the four tools one after another", which was
    estimated from their times before SP-2 and is out of a set's reach (measured 21% less: the work of the runs is what is left,
    for stages 2 and 3). One scenario's 7.2 s against its 7 s stands as measured.
11. **R2's step 9 comes next, stage 2 after R2** (27 November, after decision 10): stage 1 took the largest waits away, and
    stage 2's 3 to 4 days would gain about 6% more of the development's time; it is looked at again when R2 is done, or sooner if
    the waits grow. (Changed on 27 November by decision 54 of `docs/active-ragdoll.md`: R3 first, then stage 2.)
12. **SP-6 on by default** (taken for the owner in the night of 27 to 28 November, reversible; **confirmed by the owner on 28 November**): every queue's process
    keeps the assets of `Tools\batch_keep.json`, since it changes no run's hashes (1677 runs and files, section 12) and takes about 9%
    off every tool's time; `SANDBOX_KEEP_ASSETS=0` runs without it. Not chosen: off by default until the owner's look (the same
    results, slower).
13. **The pool is started by hand until the owner's look** (taken for the owner in the same night, reversible; **confirmed by the owner on 28 November**): SP-4
    is built and `Tools\pool.ps1 -Start` starts it; a build stops it (SP-5) but does not start it again, so that nothing of it runs
    unless asked for. Not chosen: SP-5's start after every build, as written (the pool would then be in every run of the next
    sessions before the owner has seen it).

## 10. The audit of 27 November

What the audit found and put right, with the evidence. Figures that the audit confirmed are not listed: the suite's 1795 s simulated
and 1158 s after the last input, 466 calls with 137 builds and 329 on code already built, 7.7 h of waiting in the foreground, 1.8 GB a
process, Zen's 2.3 to 2.7 s.

| What | Was | Is | Evidence |
| --- | --- | --- | --- |
| One scenario, now | 13 s | 10.7 s | three runs of `getup_left` on an idle machine: 10.7, 10.6 and 10.7 s |
| `test.ps1`, now | 15 s, a pause of 4.4 s | 16.1 s, a pause of 5 s | `test.log`: "Ready to start automation" 11:23:59.474, the first worker message 11:24:04.497 |
| The bootstrap, now | 381 s | 338 s | two runs in the foreground, 337.8 and 338.6 s; its steps in `bootstrap.json` add up to 336.4 s |
| 4 scenarios times 3 sets, now | 148 s as a loop | 128 s as a loop, 22.2 s with `sweep.ps1` | `sweep.ps1 -Scenarios "getup_back,getup_front,getup_left,getup_right" -Vary "getup.start_blend_s=0.3,0.6,0"` |
| Build, tests and suite after an edit of the controller | 103 s | 87 s | the controller's `.cpp` touched: `BUILD OK (11.3 s)`; tests 16.1 s; suite 59.7 s |
| Targets of section 1 and the table | 55 s and 60 s | 65 s for the whole suite, 35 s for the inner loop | the corrected parts; SP-9 postponed |
| The time after a build | "after 80 s on average" | the next run after a median of 6 s (mean 12 s); 97 of 137 builds ran the engine in the same call | `docs/recon/fast-checks/runs_check.ps1` |
| The share of waiting | 57% of 29 h | about 54%: 16.2 h of 30.4 | `time_split.ps1` over the ten sessions: the model 12.1 h, tools 9.7 h (7.7 h of them calls that start the engine), waits on background work 8.6 h |
| SP-0's goal for the share | 35% or less | 41% or less (40% less waiting gives 0.32 of 0.78); and 40% less waiting a call | arithmetic, by the reviewer |
| SP-0's time | about 10 minutes | about 20 minutes for three passes | the rows of the table added up |
| Zen kept up | decided (decision 1) | the DDC graph without Zen (decision 6) | the run context includes the lifetime setting (`ZenServerInterface.h`, lines 208 to 211); "different run context, will attempt shut down" measured; 7.7 s against 7.8 s; 154 of 154 hashes |
| The asset registry's cache at exit | 0.9 s a process | never written by `-game` processes | `AssetDataGatherer.cpp`, lines 206 to 210; no "cache written" line in any queue log |
| `RunTests Now <filter>` | to be checked | wrong: runs no test, exit code 0; `Now` is its own command | `AutomationCommandline.cpp`, lines 606 to 616; measured |
| The forced exit | flushes logs and results | flushes the log only, then `TerminateProcess`; not for rendered processes or traces | `WindowsPlatformMisc.cpp`, lines 1486 to 1515 |
| The start of 8 processes | 8.5 to 16 s "under load" | UBT's platform check runs one process at a time, 3.9 s on average; skipped by `UE_SKIP_UBT_SDK_SETUP=1` (new SP-2 item 4): first job after 5.3 s instead of 9.9, the suite's queue 53.2 s instead of 57.6, hashes 154 of 154 | the eight logs: returns 0.3 to 7.5 s after the launch, 1 s apart; `TargetPlatformManagerModule.cpp`, lines 54 and 223 to 226; found by the second reviewer |
| The targets of stage 1 | one scenario 9 s, suite 52 s, 4 times 3 in 25 s | 7 s, 55 s, 18 s | measured with SP-2's items 1 and 4: one run 7.3 s, the suite's queue 53.2 s |
| The idle pool | "looks every 50 ms" | must sleep: a fixed timestep does not limit the frame rate | `docs/pitfalls.md`; found by the reviewer |
| The pool's fingerprint | DLLs and generated assets | the DLLs (recorded with every run; they cannot change under a live process), `Config\*.ini`, `Content`, the motion library, the warm-up, the pose database, the engine's build | `NaturalMeasures.cpp`, lines 690 to 695 (a cache for the process's life) |
| The memory | 9 processes of 1.8 GB | one count of processes and free memory for every launcher; 8 pool processes at a 2.5 GB cap would be 20 GB, so the cap is 2.2 GB | `Invoke-ScenarioQueue` counts nothing (`common.ps1`, lines 282 to 293) |
| SP-1 | a list of names for `scenario.ps1` | `sweep.ps1` takes sets; `scenario.ps1` stays as it is | its callers: `film.ps1`, `film-pair.ps1`, `pose-prior.ps1`, `record-replay.ps1`, `surface-friction.ps1`, `repeatability.ps1`, `common.ps1` |
| SP-3's split | plan, run, judgement | plans with rounds | `getup-stand.ps1`, lines 295 to 320; `grab-pulls.ps1`, lines 96, 128 and 186 |
| SP-5's warm-up "while the model thinks" | a median of 80 s | none: the pool warms while the tests run | `runs_check.ps1` |
| SP-6's assets | those the previous job loaded | a fixed set after the warm-up | the warm-up runs the stock mannequin (`Tools\batch_warmup.json`); a job must not depend on the one before |
| SP-7's tool | Unreal Insights (a window) | `UnrealInsights.exe -noui -autoquit` with `TimingInsights.ExportTimerStatistics`; scopes added to our code | `ExportTimerStatisticsFromUtrace.cs` in the engine |
| SP-8's lean mode | "states, events, the end pose, hashes" | what each tool reads, from its code, files included; the stand reads much more | `getup-stand.ps1`, lines 333 to 439; `push-matrix.ps1`, lines 150 to 192 |
| SP-8's threshold | dropped below 15%, accepted at 20% faster | done only from 20%, accepted at four fifths of the share found | the two numbers left a gap |
| SP-9's premise | get-ups of 25 to 40 s, 1795 s to 1300 s | get-ups of 16 to 30 s; at most 286 s less; postponed (decision 7) | `docs/recon/fast-checks/suite_tails.ps1`; the runner's last input misses paths in 60 scenarios |
| SP-9's baselines | the same mode, and full length | not needed while postponed; a hash at the early-end frame if it comes back | the contradiction in section 3 |
| SP-10's sizes and gain | 6037 and 3425 lines; 60% of today's build | 6273 and 3590 lines; 11.3 s a `.cpp` edit; measured first (decision 8); unity at 32 files | `TargetRules.cs`, line 1936; `UEBuildModuleCPP.cs`, line 952 |
| SP-11's data | "the recorded failures of November" | 61 baselines (12 with a failure), outputs under `Saved\`, the transcripts | `Saved\Baselines` |
| The hash checks | `-CompareMatrix` and the stand "run by run" | every tool's runs kept and compared by both hashes (`baseline.ps1 -SaveRuns`, `-CompareRuns`), built in SP-0 | `-CompareMatrix` compares outcomes and states; the matrix's result file holds no hash; `getup-stand.json` holds `hashes.bodies` only |
| Effort | 5 to 6 days | 7 to 10 days | the code of the seven tools and of the queue read |
| One pool or one a session | unclear | one pool for the machine, shared; jobs name their batch and session | two pools of 8 would break the 9 processes |
| SP-12's goal | "at least half as many scripts" | at most half: 87 distinct files or fewer, against 173 | `docs/recon/fast-checks/own_scripts.ps1` |
| Terms | "gate" in three senses, "inner loop" undefined, "call" for two things | the terms table (batch, suite run, baseline added); `gate.ps1` is `checks.ps1` | |

## 11. Stage 1 as built (27 November)

SP-0 to SP-3 are built; the measurements are in `docs/testing.md`, "Faster checks, stage 1", and the scripts they were made with in
`docs/recon/fast-checks/` (`queue_1000.ps1`, `rendered_memory.ps1`, `sweep_vs_single.ps1`). Not a run's hash moved: after each part
every tool that runs scenarios ran once with its defaults and was held against its runs before stage 1, run by run (the last time 1637
runs and files, the check set's included), and the suite against the baseline `fc_before`.

**The stage check** (`Tools\bench.ps1`, three passes, medians; `Saved\Bench\before.json` and `after_stage1.json`):

| Measurement | Before | After stage 1 | Target |
| --- | --- | --- | --- |
| One scenario | 11.7 s | 7.2 s | 7 s or less |
| 4 scenarios times 3 sets | 21.6 s | 15.5 s | 18 s or less |
| `test.ps1` | 16.7 s | 8.0 s | 8 s or less |
| `suite.ps1` | 59.4 s | 53.5 s | 55 s or less |
| Build, tests and suite after an edit of the controller's `.cpp` | 91.3 s | 72.7 s | 74 s or less |
| `getup-seeds.ps1` | 88.8 s | 77.9 s | 82 s or less |
| `natural.ps1 -Quick` | 59.3 s | 36.3 s | 45 s or less |
| `bootstrap.ps1` | 333.5 s | 138.9 s | 200 s or less |

The benchmark times a tool's whole call, PowerShell's start included, so its "before" differs from section 3's "Now" by up to a second
(one scenario 11.7 s against 10.7). Seven of the eight targets are met. One scenario misses its 7 s by 0.2 s: its engine call, which
section 3's 10.7 s timed, takes 5.8 to 6.1 s now; the rest is PowerShell, the script and the engine's process before its log opens.

**Where the built differs from the text, and why:**

- **SP-0.** The benchmark's second mode, `bench.ps1 -Hashes`, is the rule of the hashes' "each caller runs once with its defaults": 24
  tools (and the check set), every run's two hashes, exit codes and verdict lines in `Saved\Bench\<label>\hashes.json`, held against
  another battery with `-Against`. `baseline.ps1 -SaveRuns` keeps its sets in `Saved\RunHashes`. A build's compile times come from
  UnrealBuildTool's own per-action times, which the project's `Saved\UnrealBuildTool\BuildConfiguration.xml` (not committed) asks for:
  its log's timestamps stop when the header tool runs. Decision 8's estimate reads two ways (section 10's formula 63%, the compiles'
  critical path about 13%: `docs/testing.md`); that is for SP-10 in stage 3.
- **SP-1.** As written. A rendered process takes 2.5 GB of the machine's memory, so up to four film processes run side by side,
  fewer as memory says.
- **SP-2.** As written, and with it section 4's one count of engine processes and free memory for every launcher
  (`Test-EngineRoom`: nine at most, 1.5 GB left free), since stage 1's check sets put eight processes and a ninth side by side.
- **SP-3.** The split of a tool into plan, run and judgement is at `Invoke-ScenarioBatch`: inside a check set (`SANDBOX_CHECK_QUEUE`)
  a tool's call hands its jobs to the set's queue and waits for each job's `taken\<job>.done`, which its process writes once every
  file of the job is written; so what comes before a call is the plan, what follows is the judgement, and a further call is a round;
  each tool runs as a process of its own, unchanged, and alone it runs as before. The suite's second run over the step limit waits
  until every process of the set, and every other run of the machine (two minutes at most), has ended. `checks.ps1` judges each tool
  by its result file. `getup-seeds.ps1` alone keeps its three
  queues: one queue of nine took 87 to 91 s against 78 s, since a stand's trials drawn again wait there for a process the other stands
  keep busy; inside a set its stands use the set's queue.
- **SP-3's acceptance, corrected by decision 10:** the four tools as one set took 147.4 s against 185.4 s one after another on the
  same build, 21% less where 35% was asked. The set started its eight processes once and its queue never emptied: what is left is
  the work of the runs (about 1100 process-seconds), which only stage 2 (SP-6, a job's world) and stage 3 (SP-8) make smaller. The
  35% was estimated from the four tools' times before SP-2, much of which SP-2 itself took away. The owner accepted stage 1 with the
  criterion "the queue kept full" (decision 10).

## 12. Stage 2, SP-6 first (28 November, the night, the owner away)

By decision 54 stage 2 comes after R3; R3 was built in the night behind its switches (then waiting for the owner's look), and SP-6
needs nothing of the owner and nothing of SP-4 (a process of a queue lives for its batch: the suite's 174 jobs are about 22 a process), so it was taken first.

- **Built:** `-ScenarioKeep=<file>` (`UScenarioBatchSubsystem::KeepAssets`): after the warm-up a queue's process loads the assets
  `Tools\batch_keep.json` lists and holds them for its life, a fixed set whatever its jobs: the NPC's body and face meshes and its
  physics asset. `Get-QueueArguments` passes it to every queue's process; `SANDBOX_KEEP_ASSETS=0` runs without it; a rendered
  process (films, shots, the headset) keeps nothing (after the third reading: kept meshes keep their textures streamed in, so a
  rendered job's pictures could depend on the jobs before it, and in the headset the load is a hitch). Each job's
  `run.log` now says how much memory its process uses as the job ends (read only). The clips are not in the set: their reading showed
  no build in the logs, and the set grows only if a measurement asks for it.
- **Measured** (`docs/testing.md`, "Faster checks, SP-6"): a job's world in 0.036 s instead of 0.38 (`LoadMap`, `drop_npc`), no
  `AssetCompile SkinnedAsset` line in a job's log; the suite in 56.8 s instead of 64.2 (8 at a time); the suite's 174 hashes those of
  the baseline `r3rev2` with eight processes and with one (each job after another job than before: the order's check); in one process
  over its 174 jobs the memory went from 1804 to 1862 MB, most of it in the first ten jobs. **Every tool's hashes** (`bench.ps1
  -Hashes`, `keep0` with `SANDBOX_KEEP_ASSETS=0` and `keep1` against it): 1677 runs and files with the same hashes, the verdicts the
  same in 25 of 25 tools; every tool once in 762 s instead of 836 (the suite 64.4 to 56.1 s, the matrix 43.7 to 36.5, the stand of
  three seeds 81.3 to 73.8, the scorecard 104.7 to 91.7, the check set of seven tools 230.8 to 200.9; a call of one or two runs as
  before, its process starts for that run alone).
- **SP-6's acceptance:** a job's world in 0.15 s or less, met (0.036); `AssetCompile SkinnedAsset` only before a process's first job,
  met; the suite's hashes the same in two orders, met (eight processes and one); a process's memory 200 MB or less over 500 jobs, 58 MB
  over 174 and levelling off (500 not run). On by default since decision 12 (confirmed on 28 November).
- **SP-4, the pool, built the same night as far as it is started by hand** (the record of its plan: `docs/recon/sp4_plan.md`):
  `Tools\pool.ps1 -Start [-Processes 8] [-IdleS 600] | -Stop | -Status`. Its processes (`-ScenarioPoolIdleS`, `-ScenarioPoolPrint`)
  wait in the start map when their queue (`Saved\Pool\queue`) is empty, sleeping 0.1 s a frame, and exit when they have waited that
  long, on a file `stop` or `yield_<pid>`, when `Saved\Pool\pool.json` no longer names the fingerprint they started with, after 500
  jobs, over 2.2 GB, or with under 1.5 GB of the machine free (why: `exited_<pid>.txt`). `Invoke-ScenarioBatch` and `scenario.ps1`
  hand a live pool with today's fingerprint (the project's DLLs, `Config\*.ini`, the warm-up, the kept assets, the motion library,
  the pose database) their headless jobs through the check set's `Submit-CheckJobs`, and top it up while their jobs wait; runs that
  must be alone, rendered ones and those with console variables keep processes of their own; a start with no room asks the pool's
  idle processes to give way (`Test-EngineRoom`, `Request-PoolYield`); `SANDBOX_NO_POOL=1` ignores a pool. **SP-5 in part:**
  `build.ps1`, `gen-assets.ps1`, `bake-lighting.ps1`, `bench.ps1` and every run in the headset stop the pool first (once the jobs in
  it are done, 5 minutes at most, then they refuse); a build does not start it again (decision 13).
- **Measured:** an idle pool process 0.6 to 1.4% of a core (3 to 4.5% when it slept 0.05 s a frame), 1.69 GB, flat over 10 minutes;
  the suite through the pool 48.4 s (57.1 s without), its 174 hashes those of `r3rev2` in a fresh pool, after 10 minutes idle and
  after jobs on another physics asset; one scenario 2.6 s (7.2 s); six more tools through the pool with every run's hashes those of
  the bench `keep1` (the matrix 30 s instead of 37, the knockdowns 4 instead of 11, the grab's pulls 25 instead of 42); a build with
  the pool running stops it and builds. The rest in `docs/testing.md`, "Faster checks, SP-4". **SP-4's acceptance:** one scenario in
  4 s or less, met; the suite in 45 s or less, not met (48.4); the hashes in a fresh pool, after 10 minutes idle and after another
  physics asset, met; near the end of 500 jobs, not run; an idle process under 2% of a core, met, its memory flat, met. `-Parallel 1`
  with several jobs and `repeatability.ps1` keep processes of their own.
- **Read by an independent reader and put right** (three blocking defects and five others in the scripts, then one found by a test:
  the pool's processes held their starter's output open; `docs/testing.md`, "Faster checks, SP-4"): a stop takes no new batches
  first (`draining`), refuses while a process still runs, and a batch whose pool ended takes its untaken jobs back; the pool's file
  under a lock and replaced whole; processes known by id, name and start time; batch names with a GUID; no pool with
  `SANDBOX_SEPARATE_PROCESSES=1`; one idle process at a time gives way. Read again after the fixes: nothing blocking; its edges put
  right (a job taken back under its lock as a process takes one; a stop that waits for a top-up under way and looks again under the
  lock; no job taken after a stop) and checked: a pool stopped under a batch ran 6 of its 40 jobs, took 34 back, every hash as
  without a pool.
- **Not done:** the start after a build (SP-5, decision 13); a check set beside a pool makes the pool's idle processes give way
  rather than using the pool's queue; the fingerprint has no content assets (gen-assets.ps1 and bake-lighting.ps1 stop the pool;
  `metahuman.ps1` and the fetch scripts do not: stop it by hand before them), nor the engine's build or the `.uproject`; the fingerprint
  is not written into `metrics.json` nor a tool's runs on two fingerprints warned of; small batches do not go before a long one (one
  scenario waits behind another session's suite); a stop does not name whose batches it waits for; the memory's and the 500 jobs'
  exits are not run to their end. (Put right in the audit of 28 November, after its reader: `scenario.ps1` through the pool keeps its
  `-TimeoutSec`, the job past it given up and its process ended; a scenario at another frame rate than 90 never goes to the pool or a
  check set; the pool's top-up counts its processes started in the last 20 s as memory still to be taken; a run that must be alone
  asks the pool's idle processes to give way; a stop whose lock fails leaves the pool taking batches.)

<!-- removed: getup.carry -->
<!-- planned: Tools\compare.ps1 Tools\profile.ps1 -Tier -->

