# R2, step 6: the start blended from the lying pose (decision 22)

Written 27 November 2026, 00:50, the owner away (the autonomous window of 22:12 to 06:42), after the owner's list for the night was
done. Not on that list and not among the steps the owner kept back (the matcher, the roll, the fit to the floor, the drags, FR-43's
arms); decision 22 was taken by the owner on 26 November. Behind a value that is 0 in the files: with it at 0 every hash stays.

Read before it (`docs/pitfalls.md`): Falls and the get-up ("A body appears in its reference pose...", "Physics Control damps a muscle
against the target pose's own turning speed" in Solver, "A clip made for another skeleton cannot be copied bone by bone"); Muscles,
poses and the standing body; Measuring, runs and tests.

## What

Today a get-up takes the clip's first pose whole: the muscles' targets jump to it in one frame (their strength comes up from nothing
over `getup.ease_s`), and the carry's targets jump to where the clip has each body. Decision 22: both go from where the body is to
where the clip is, over the ease.

`getup.start_blend_s` above 0:
- **Muscles.** As a get-up begins, the clip layer's "from" pose is the body's own: every bone's local rotation as the simulation has
  it now. The layer crosses from it to the clip's first pose over `start_blend_s` (the cross the clip layer already has between
  two clip poses, with its own time), while the clip waits for the ease as it does today.
- **Carry.** Each body's target goes from where the body was as the get-up began to where the clip has it, by the same smoothed share.

0: the first pose taken whole, as before, bit for bit.

## Settings

| Name | Unit | Value | What |
| --- | --- | --- | --- |
| `getup.start_blend_s` | s | 0 in the files (0.6, the ease, measured with `-Tune`) | the start's blend; 0: none |

## Checks

- Suite at 0: every hash. With it on: the scenarios with a get-up move; listed.
- The stand on three seeds with it on (decision 7): plain, one hand and two with the hold's switch at 0, pressed; C19 to C27 by the
  gate's script; the targets' largest jump in a frame at a get-up's start (`natural` measures in the runs).
- A video pair of `getup_back` and of a side get-up, the start.

## Risks

- The clip's first pose is near how the body lies only when the clip was chosen well (the matcher is a later step): a long blend from
  a pose far from the clip's makes a slow, odd start; measured on the stand.
- The "from" pose takes bones the simulation moves; a bone between two bodies with none of its own keeps the state's pose.
