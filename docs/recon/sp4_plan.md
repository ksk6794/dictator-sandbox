# Stage 2's pool (SP-4 and SP-5): the plan from the code

**Outcome (the same night, 03:25 to 04:30):** steps 1 and 2 built and checked, step 3 in part, step 4 without the start after a
build (decision 13 of `docs/fast-checks.md`); what was measured and what is not done is in `docs/fast-checks.md`, section 12. The
plan as written follows.

Written 28 November 2026, 03:20, in the owner's autonomous window, after SP-6 was built and checked (`docs/fast-checks.md`, section
12). The requirements are `docs/fast-checks.md`, SP-4 and SP-5 (section 6) and decision 2 (the pool idles 10
minutes and stops itself before any run in the headset); this file is how they meet today's code, what to build in which order and
what each step is checked by.

**Read first** (`docs/pitfalls.md`): "PowerShell, scripts and building" (moving a file is not a way to take a job; Windows PowerShell
5.1; a tuning name and its value in the same build); "Measuring, runs and tests" (a tuning file edited while runs go is read by them:
a pool process that outlives a build would refuse a new name); in `CLAUDE.md`: the first scenario of a process differs from every
later one (the warm-up stays once a process), an engine process takes 1.75 GB and nine at most run at a time.

## What exists to build on

- **The check set's queue already is a pool without the waiting** (`Tools\common.ps1`, lines 434 to 641): `Enter-CheckQueue` makes
  a folder under `Saved\Queue` with `owner.pid`; `Submit-CheckJobs` writes each job whole (`.part`, then moved), named by its length
  so that the longest go first, and waits for `taken\<job>.done`, reporting a job lost when the process named in its `.taken`
  marker has ended without a `.done`; `Update-CheckQueue` starts processes for waiting jobs within `Test-EngineRoom`, and grants a
  run that must be alone. Jobs with console variables, a renderer or a shot go to processes of their own (`Invoke-ScenarioBatch`,
  lines 203 to 213).
- **The engine side** (`UScenarioBatchSubsystem`): takes a job under a lock file (`TakeJob`), opens a fresh world per job, resets the
  tuning store per job (`ResetForRun`), writes `.done` after the job's log is closed, and when `TakeJob` finds nothing exits at once
  (`OpenNext`). Since SP-6 a headless process keeps the NPC's meshes and physics asset after its warm-up (`KeepAssets`), and each
  job's `run.log` says the process's memory.

## Design

1. **The engine waits instead of exiting** (`-ScenarioPoolIdleS=<s>`): when `TakeJob` finds nothing, the process opens the start map
   once (so that the last job's world, a body still simulating, is gone) and from then on, every engine frame, sleeps 50 ms
   (`FPlatformProcess::Sleep` on the game thread: with a fixed timestep and no renderer the loop runs as fast as it can, and an idle
   process would spin a core) and looks for a job. It exits, writing `exited_<pid>.txt` with the reason, when: idle longer than the
   given seconds (600, decision 2); a file `stop` is in the queue; the fingerprint file (`pool.json`'s) no longer says what it read at
   its start; it has run 500 jobs; after a job its memory is over 2.2 GB, or the machine has under 1.5 GB free; a file
   `yield_<pid>` names it (another start needs its room).
2. **The pool's queue is a check-set queue that outlives its owner:** `Saved\Pool\queue` (not under `Saved\Queue`, whose old folders
   `Invoke-ScenarioQueue` removes), with `pool.json` (processes, fingerprint, started) in place of `owner.pid`, written under a lock
   file. `Invoke-ScenarioBatch`, outside a check set, asks `Get-LivePool`: a pool with a live process and the current fingerprint
   takes the jobs through `Submit-CheckJobs` (unchanged: naming, `.done`, lost jobs), and whoever submits keeps the pool topped up
   (`Update-PoolProcesses`: pool processes up to 8 while jobs wait, within `Test-EngineRoom`, each recorded in `pool.json`), as the
   check set's owner does. Rendered jobs, jobs with console variables, runs that must be alone (`-Alone`, `suite.ps1 -Parallel 1`,
   `repeatability.ps1`) and `SANDBOX_SEPARATE_PROCESSES=1` go on as now. A check set inside a live pool uses the pool's queue as its
   own (its owner does not start a second set of processes). `scenario.ps1`, which builds a queue of one today (`New-ScenarioQueue`),
   goes through the same call.
3. **The fingerprint** (one hash, written into `pool.json` and passed to each process as `-ScenarioPoolPrint=`): the project's DLLs
   (size and write time), `Config\*.ini`, `Tools\batch_warmup.json`, `Tools\batch_keep.json`, `Specs\motion_library.json`,
   `Saved\PosePrior\prior.bin` and the newest write time under `Content`. Every run writes the one it ran on into its `metrics.json`
   (`process.fingerprint`, read only, outside the hashes), and a tool whose runs ran on more than one says `QUEUE WARNING` (a
   comparison with a baseline then does not count).
4. **SP-5:** `build.ps1`, `gen-assets.ps1` and `bake-lighting.ps1` stop the pool first (`Stop-Pool`: writes `stop`, takes no new
   batches, waits up to 5 minutes for the batches in it and names their sessions, then refuses with `BUILD ERROR`; never kills
   them), and after a build that succeeds start it in the background unless `-NoPool` or a run in the headset is going. `play.ps1
   -VR` and `scenario.ps1 -VR` stop it before they start (decision 2).
5. **`Tools\pool.ps1 -Start | -Stop | -Status`.**

## Order and checks

1. The engine's waiting mode and `pool.ps1 -Start/-Stop`, with jobs put in by hand: an idle process under 2% of a core (Task Manager's
   figure through `Get-Counter`), its memory flat over 10 minutes; `stop` ends every process within a second.
2. `Get-LivePool` and the routing: the suite in a fresh pool against the baseline of the night (`r3rev2`, 174 of 174 the same hashes);
   one scenario on a warm pool in 4 s or less, the suite in 45 s or less.
3. The pool's states: the suite three times in a row (522 jobs: processes past 500 restart), after 10 minutes idle, and after jobs on
   another physics asset (`SANDBOX_PHYSICS_ASSET`), each against `r3rev2`; the matrix and the stand against their run hashes
   (`baseline.ps1 -CompareRuns`).
4. SP-5: a build with the pool running and with a batch in it from another session (the build waits, then builds; no job runs on
   the old build: the fingerprints in `metrics.json`); build, tests and suite in 65 s or less.
5. Every tool's hashes: `bench.ps1 -Hashes -Label pool -Against keep1` (28 November: 1677 runs and files), then `bench.ps1` for the
   times against stage 1's.
6. The documents (`docs/fast-checks.md` section 12, `docs/testing.md`, `CLAUDE.md`'s commands and times), an independent reading of
   the diff, the bootstrap.

**Risks:** a state that leaks from an idle world or from 500 jobs into a later job's hashes (checked by step 3; fallback: restart a
process more often, or after every idle); a batch waiting on a pool whose processes all exited (the submitter tops it up; a lost job
says so); memory with films beside an idle pool (an idle process yields). About one night of work.
