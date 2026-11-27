# R3, step 1: the way and the time of a fall, sensed (measure only)

**Outcome (27 November, 02:45): built and measured, every hash kept** (`docs/testing.md`, "R3, step 1"): free fall foresees the
trunk's impact 0.020 s late at the first tick (median), 0.026 s off over the fall, after the reader's corrections (the first of the
pelvis, the chest and the head; furniture with mass swept; held-up ticks left out), each part swept along its own free-fall path
instead of horizontally (03:20), and that path as two chords instead of one (04:20, the second reading). The plan as written follows.

Written 27 November 2026, 02:35, the owner away (the autonomous window of 22:12 to 06:42), after the owner's list for the night and
every step of R2 that needs nothing of the owner were done. R3 is the next part in `docs/active-ragdoll.md`, section 8; its first item
("The way and the time") is a sense, and this step builds only that: what the body would know of its fall, read every tick and
measured against what then happens. Nothing acts on it: every hash stays (the rule "measuring changes nothing"). Not the 40 J, the toes
or the eyes the owner kept back.

Read before it (`docs/pitfalls.md`): Falls and the get-up ("The pose of a fall is for meeting the floor", "A body knocked into a wall
often does not fall"); Solver, Chaos and Physics Control ("A skeletal mesh answers an overlap query as one thing"); Measuring, runs and
tests ("A measure can be met by what it was not meant for", "Check a new measure on a case whose answer is known").

## What

Every tick while the body is Falling, `USandboxBodyController` works out (`FSandboxFallSense`):
- **The way:** the horizontal direction the body goes, from its centre of mass's velocity and the pelvis's lean (as `ChooseFallWay`
  does once at the fall's start), as a unit vector and as one of back, forward, left, right of the body.
- **The time until the trunk meets something:** the floor, from the chest's centre of mass height above the support and its vertical
  velocity under gravity (the trunk's radius taken off); and a wall or a piece of furniture, from a sphere swept along the chest's
  horizontal velocity for that time (a scene query against the static and movable world, the character itself left out). The
  smaller of the two, and which it was.

The runner keeps, per fall: the prediction at the fall's first tick, the trunk's real first touch (the controller's `TrunkTouchedS`),
the error of the first prediction and the mean error of all of them (each tick's predicted moment against the real one), and whether a
wall or furniture was foreseen. `control.fall.sense` in `metrics.json`.

## Classes

- `FSandboxFallSense` (`SandboxBodyController.h`): way, time to impact, what (floor, other), valid. `GetFallSense()`.
- `USandboxBodyController::SenseFall`: worked out in the tick while Falling, after the balance is measured; read only (bodies and a scene
  sweep).
- `FSandboxFallTimeModel` (pure, `SandboxBodyProbe.h`): the floor's time from height, vertical speed, gravity and radius. Unit-tested.
- `UScenarioRunnerSubsystem`: the per-fall figures.

## Settings

None: a sense with constants of geometry (the trunk's radius 12 cm, the sweep's sphere the same). Nothing is tuned.

## Checks

- Unit test: the time model against free fall worked by hand (from rest, moving up, moving down, already below).
- The suite: every hash of `r2night`. The knockdowns (8 directions, the answer known: the floor) and the stand of 150 (walls, the table,
  the chair): how early and how well the time is foreseen, and whether the way matches the fall's own direction.

## Risks

- The trunk often meets an arm or a knee first, not the floor: the error of the floor's time is what R3 then has to live with.
- A sweep against the room finds the character's own bodies unless they are left out by the query's parameters.
