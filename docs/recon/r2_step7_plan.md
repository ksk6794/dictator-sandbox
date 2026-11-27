# R2, step 7: get-ups from eight ways of lying (a rig and its scenarios), and the support frame's seam

Written 27 November 2026, 03:40, the owner away (the autonomous window of 22:12 to 06:42), after R3's first step. R2's list of
scenarios (`docs/active-ragdoll.md`, R2, "Scenarios") begins with "get-ups from eight ways of lying"; the four `getup_*` lay the body
by a blow, so they cover the ways a blow gives and nothing between. This step adds what the matcher will be judged on later; it is not
the matcher, the roll or the fit to the floor (the owner keeps those). Read before it (`docs/pitfalls.md`): Falls and the get-up;
Measuring, runs and tests.

## What

- **A rig that lays the body down** (`body.laid_deg`, `body.laid_height_cm` in a scenario): the body is spawned standing, its control
  is started (so that its standing references are those of a standing body), and before the first step every body is turned about
  the spawn point (on its back, then this far about its own length) and raised to just over the floor, with no velocity. The
  controller finds it falling at 0 s. A body spawned turned (`rotation_deg`) does not do: the controller takes the pose it starts in
  for standing, holds it there with the assist and calls it standing (tried first: the back and the front stood lying in a stagger,
  the others gave up "the pelvis is 40 cm from where the clip has it").
- **Eight scenarios** `getup_laid_<way>`: back, back_right, right, front_right, front, front_left, left, back_left (0, 45, ... -45
  degrees about the length); each must stand at its first attempt. Two more by the table (`getup_laid_beside_table_back`, `_front`),
  and four that fail today kept for the fit to the floor in `docs/recon/r2_step7_furniture` (under the table, on its top, across
  its end on the front and on the back).

## Found on the way, and put right

On the front and front-right the body stood up, a second later "the feet stand 65.0 cm from their stance; foot_r steps back to its
place", turned about and fell (film `Saved\Films\getup_laid_front`). The feet stood together. `SupportFrame` averages the two feet's
turns since the start, each from -180 to 180 degrees: a body that stands facing about half round from where it started has one foot
at 175 and the other at -175, the mean is 0 and the support frame is turned half round from the feet. Its stance then lies the other
way (2 x 32.5 cm off), the regroup steps to close it, and the assist's anchors for the pelvis, the chest and the head are turned half
round. Each later foot is now taken on the first one's side of the seam; a body whose feet do not straddle it computes the same sum
to the bit.

## Checks

- The suite against `r2night`: every hash but the runs whose feet straddle the seam (explained), and the eight new scenarios.
- Decision 7, as the fix changes the body: the push matrix against `walk`, the knockdowns, the stand on three seeds plain, held by one
  and two hands and pressed with 300 N.
