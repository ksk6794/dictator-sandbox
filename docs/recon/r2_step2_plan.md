# R2, step 2: a get-up that was not led is said (decision 17), and authority measured (decision 15)

Written 26 November 2026, the owner away (the autonomous window of 22:12 to 06:42). Measures only: every hash of `walk` kept.

Read before it (`docs/pitfalls.md`): Falls and the get-up; Solver, Chaos and Physics Control; Measuring, runs and tests;
PowerShell, scripts and building; and the cross-cutting list in `CLAUDE.md`.

## What

1. **Decision 17.** `control.get_up` says, for every attempt that asked for the led get-up (`getup.carry` 1), whether it was
   led, and why not. A run with `getup.carry` 1 whose get-up was not led fails (`SCENARIO FAIL`, a check of its own). A session
   says so in its log and on the desktop's HUD (the headset's HUD draws nothing). Checked by breaking the carry's creation on
   purpose: a build in which `FSandboxCarry::Create` refuses, a get-up scenario run, `SCENARIO FAIL` with the reason; the code
   put back.
2. **The overlay** stops drawing the get-up's old assist as active while the body is led (`GetControlView`: an assist is active
   only when its springs, as they are handed to the solver in this state, are above 0), and draws what is active instead: the
   carry's targets and the world's force on each body as a line (`FSandboxBodyControlView::Carry`).
3. **`control.authority`** (decision 15): what the world gives the body, the old assist and the carry together, by state and,
   while getting up, by body; by direction (up, over the floor, down); mean and peak as shares of the body's weight; torques
   too. Measuring only, so the suite keeps every hash.

## Classes and code

- `FSandboxGetUpView` (SandboxBody): `LedAttempts`, `NotLedAttempts`, `NotLed` (why the last one was not led).
  `USandboxBodyController::UpdateCarry` decides it at the first tick of each attempt that asked for the carry, after the carry
  has been made or has failed (where it is made today: the order of creation, and so the solver's, is not moved). The reason of a
  failed creation is kept (`CarryBrokenReason`). `FSandboxCarry::Create` also refuses a mesh with no simulated bodies (it made an
  empty carry before, and "led" a body with nothing).
- `FSandboxAuthority` (SandboxBody, `SandboxBodyController.h`): every force the world puts on the body in the coming step, one
  entry a body and a source (`Assist` or `Carry`): bone, force (N, world), torque (N*m). Filled in `UpdateAssistLoad` from the same
  spring law as `FSandboxAssistLoad` (so `getup` mean equals `control.get_up.assist_force_share_mean`, a check of the metric);
  `FSandboxCarry::TorqueOn` is new (the angular spring's law, capped by size). Read only.
- `FSandboxAuthorityTally` (SandboxScenario, new `AuthorityTally.h/.cpp`): adds a frame's entries under a state name and makes the
  JSON. Pure arithmetic, unit-tested. `UScenarioRunnerSubsystem` adds a frame where it adds the assist's share today.
- `USandboxPlaySubsystem`: watches `NotLedAttempts` of its character; on a new one an error line in the log; `AddHudLines` a line
  `GET-UP NOT LED ...` while the character has one.
- `USandboxOverlaySubsystem`: the assist layer draws `Assists` only where `bActive`, and the carry's bodies (a small cross at the
  target of the centre of mass, the force as a line of 0.05 cm per N from the centre of mass); the state layer's corner line gives
  the authority now (sum, up, over the floor, down, in weights).

## Result file

```
control.get_up: ... "led_attempts": 1, "not_led_attempts": 0, "not_led": ""
control.authority: {
  "weight_n": 784.5,
  "states": { "get_up": { "seconds", "mean", "peak", "up_mean", "up_peak", "horizontal_mean", "horizontal_peak",
                          "down_mean", "down_peak", "torque_mean_nm", "torque_peak_nm" }, "standing": {...}, ... },
  "get_up_bodies": [ { "body", "source", "mean", "peak", "up_mean", "horizontal_mean", "down_mean" }, ... by mean ]
}
checks: "get_up_led" (added when an attempt asked for the carry): not_led_attempts 0
```

A state's `mean` is the mean over its frames of the sum of the forces' sizes over the weight; `up`, `horizontal`, `down` the sums
of each force's upward part, its part over the floor and its downward part. They need not add up to `mean`.

## Scripts

- `Tools\getup-stand.ps1`: each trial keeps `authority_up`, `authority_horizontal`, `authority_down` (get-up means) and the three
  bodies given most; `Tools\getup-seeds.ps1` prints the means by direction and the bodies given most over all falls.

## Settings

None. Nothing here is tuned.

## Unit tests

- `DictatorSandbox.Scenario.Authority.TalliesByStateBodyAndDirection`: made-up frames of two states; means, peaks, the split by
  direction (a force pointing down counted as down, its horizontal part as horizontal), the bodies sorted by their mean; a body
  given nothing in a state is not listed.

## Checks

- Build, unit tests (50).
- Suite against `walk`: 132 of 132 hashes the same (131 pass, `grab_getup_held` known).
- A get-up scenario: `control.get_up.led_attempts` 1, `control.authority.states.getup.mean` equal to
  `control.get_up.assist_force_share_mean`.
- Broken on purpose: `FSandboxCarry::Create` returns false; `getup_back` is `SCENARIO FAIL` on `get_up_led` with the reason; then
  put back and built again.
- The session: `Tools\play.ps1 -Smoke` passes; the HUD's line read from the code path (no window can be judged headless; a
  `-Smoke -Shot` picture of the self-test shows the HUD only when it is the desktop pawn's).
- The stand on three seeds (no hold): the same hashes trial for trial as `r2s0_after`, and the authority by direction and by body
  for the report.

## Risks

- Moving the carry's creation would move the order of the solver's constraints: not moved.
- A per-frame loop over 24 entries in the runner and the controller: cost, not hashes; read from the suite's step time.
- `UpdateAssistLoad` runs before `Control->UpdateControls`; the forces are of the state at the start of the step, as the assist's
  load is. An estimate from the spring law, not the solver's reading (pitfall: the solver does not report drive forces).
