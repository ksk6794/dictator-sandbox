# R2, step 5: the world never pushes a body down, its damping included (decision 30's finding)

**Outcome (27 November, 00:40): not met, taken out.** On the stand of 150 the attempts given up went from 10.7% to 18.5%, six bodies
were thrown off the floor and first attempts fell from 139 to 136: the downward damping was braking the overshoot of the carry's
upward springs. `docs/testing.md`, "R2, step 5". The plan as it was written follows.

Written 27 November 2026, 00:30, the owner away (the autonomous window of 22:12 to 06:42), after steps 2 to 4 were done and read by
an independent reader. Not on the owner's list for the night; taken because it needs nothing of the owner, is behind a switch that is
0 in the files, and measures what decision 30 put off. Not the fit to the floor (which the owner kept for later).

Read before it (`docs/pitfalls.md`): Falls and the get-up (the reference below the floor, "Capping what the world may lift did not make
the legs lift", the two entries of 27 November); Solver, Chaos and Physics Control ("A drive's target is measured in the frame of the
joint's second body", "The solver moves the bodies after its drives"); Measuring, runs and tests.

## The finding

Section 5: the world never pushes a body down. `FSandboxCarry` gives a body above its reference the reference at its own height, so
the spring's place never pulls down; but the spring's damping, `stiffness * damping_s * (reference's velocity - body's velocity)`,
brakes a body that rises faster than its reference. On the stand of 150: 0.12 of the body's weight downward on average, up to 1.27
weights in a frame (R2's step 2).

## The design

`getup.carry_never_down` 1: before each step, for each carried body, the reference's vertical velocity handed to the control is raised
just enough that the spring and its damping together ask for no downward force: the target velocity at the centre of mass is at least
`v - e / damping_s` (the body's own vertical velocity less its upward error over the damping time). A body rising faster than its
reference is no longer braked by the world; a body below its reference rising toward it is still damped as long as the net is upward.
`FSandboxCarry::ForceOn` reports the same (its vertical part not below 0). 0: as R1 made it, bit for bit.

## Settings

| Name | Unit | Value | What |
| --- | --- | --- | --- |
| `getup.carry_never_down` | switch | 0 (1 measured with `-Tune`) | the carry's damping pushes no body down either |

## Checks

- Suite at 0: every hash of `walk` (and the two scenarios of step 4). With it on (`-Tune`): the scenarios with a led get-up move; each
  listed.
- The stand on three seeds with it on: standing, first attempts, the authority by direction (down to about 0), C27; and with one hand,
  two hands, pressed (decision 7), the hold's switch at 0.
- A video pair of `getup_back` (`Tools\film-pair.ps1`).

## Risks

- A body that rises faster than its clip now overshoots upward with nothing braking it but its own muscles: watched on the stand (the
  plank, standing).
- The drive is solved implicitly: the adjustment is worked out from the state before the step, as the authority's measure is; the
  measure reads the target, not the solver.
