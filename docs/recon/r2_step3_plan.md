# R2, step 3: the hold by one hand (decisions 13 and 14)

Written 26 November 2026, the owner away (the autonomous window of 22:12 to 06:42). Behind a switch, `getup.hold_by_body`, 0 in the
files as R1 was: with it at 0 every hash of `walk` stays.

Read before it (`docs/pitfalls.md`): Falls and the get-up (above all "A pull from the world that gets the body up gets it up
against a hand too", "No strength of the body draws a line between a light press and a heavy one", "A scripted force is felt as
a press; a pin is not", "A figure from one seed is luck"); Hands, the grab and blows ("A scripted hand goes to a point of the world
and slides off a body that moves", "A hand jointed to one body still collides with its neighbours"); Solver, Chaos and Physics
Control; Measuring, runs and tests; PowerShell, scripts and building.

## What is wrong today

With R1 a press or a hold takes the world's pull off the whole body in proportion to the force the body feels (`SustainedN`, the
least of the last 0.3 s, holds counted by `reactions.hold_felt`), and the clip runs on at its rate until that force reaches
`getup.hold_force_n` (450 N), more than one hand's 300 N. So one hand leaves a third of the pull on every body and the clip runs
away from it: 119 of 150 bodies pull free within the 15 s (decision 13: 20 to 60); two hands stop the clip but 14 of 150 pull free
(at most 5). The window of 0.3 s also lets a body that rises fast tear a hand off before the hold counts at all.

## The design (decision 14)

While led and with `getup.hold_by_body` 1:

1. **Body by body.** Each hold (`TellHolds`: the bone and the pull) takes authority off its chain: the held body and the bodies
   between it and the pelvis (`getup.hold_chain`: 0 the held body alone, 1 the chain without the pelvis, 2 with it). Along the
   chain the hold counts less by `getup.hold_chain_falloff` a body (0: the whole chain alike). A body's hold is
   `min(pull / getup.hold_chain_full_n, 1)`, times the falloff, the largest of the holds that reach it; its share of the springs is
   `1 - (1 - getup.hold_chain_share) * hold`. The hold is told every frame and counted at once, not after a window: a share goes
   down at once and comes back by `getup.carry_release_s`, as the press's does. The rest of the body is led as before.
2. **The press stays whole-body.** What contacts press with (the least of the last 0.3 s, as `SustainedN` but without the holds)
   takes the pull off every body in proportion, as today, up to `getup.hold_force_n`.
3. **The clip's pace by the force.** The force is the press and the holds together (`getup.hold_sum`: 0 the size of their sum as
   vectors, as `SustainedN` adds them today; 1 the sum of their sizes). The clip runs at `getup.rate * pace`, the pace
   `1 - clamp((force - getup.hold_slow_from_n) / (getup.hold_force_n - getup.hold_slow_from_n), 0, 1)`: at 0 N full, at 450 N
   stopped; with `hold_slow_from_n` 0 one hand's 300 N gives a third. It goes down at once and back by `getup.carry_release_s`.
   Stopped, the clip waits and the attempt is given up after `getup.stall_s` as today ("held down").

## Classes and code

- `FSandboxCarry` (SandboxBody): each body knows the body it hangs from (`ParentIndex`, from the reference skeleton, the nearest
  ancestor with a body); `Apply` takes a share for each body (empty: 1 for all, as today; bit for bit the same when every share is
  1); `ForceOn` and `TorqueOn` use the body's share. `FSandboxCarry::ChainHolds` (static, pure): from the parents, the held body, the
  chain's mode and falloff and the hold's size, the hold of each body. Unit-tested.
- `USandboxBodyController`: the press without the holds kept beside `SustainedN` (`SustainedContactN`, read only when the switch is
  off); `UpdateCarry` makes the body shares; `UpdateGetUp` the pace (`HoldPace`), and `bHeldDown` from the same force while the
  switch is on. `FSandboxGetUpView`: `HeldS` (time held while getting up), `PaceMin` (the slowest pace), `FirstStartedS`.
- `Tools\getup-stand.ps1`: a trial's time from its first attempt to its first standing (`first_started_s` from the run); the held
  stand prints the median of it for the bodies that pulled free and for those that stood after the hold, and the plain stand
  for all; how much longer a held body takes to stand is the difference of the medians on the same seeds.

## Settings (`Config/Tuning/body.json`, `getup.*`)

| Name | Unit | Value proposed | Range | What |
| --- | --- | --- | --- | --- |
| `hold_by_body` | switch | 0 in the files (1 measured with `-Tune`) | 0 or 1 | the hold of decision 14 |
| `hold_chain` | mode | 2 | 0, 1, 2 | the held body; and the chain to the pelvis without it; with it |
| `hold_chain_falloff` | share a body | 0 | 0 to 1 | how much less a hold counts on each body further from the held one |
| `hold_chain_full_n` | N | 150 | 1 to 10000 | the pull at which the chain is at its least |
| `hold_chain_share` | share | 0 | 0 to 1 | what the chain keeps of its springs under a full hold ("hardly led": 0 or a little) |
| `hold_slow_from_n` | N | 0 | below `hold_force_n` | below it the pace is whole (0: in proportion from 0) |
| `hold_sum` | mode | 0 | 0 or 1 | the press and the holds as vectors added, or their sizes |

The open questions a) to d) of the owner's message are `hold_slow_from_n`, `hold_sum`, `hold_chain` (with its falloff) and
`hold_chain_share`; each is left a setting so the owner can compare.

**Added after the first screen** (the plan as written above held almost nothing: below), each a setting with the plan's own
behaviour at its neutral value:

| Name | Unit | Neutral | What |
| --- | --- | --- | --- |
| `hold_chain` 3 | mode | (2) | the chain to the pelvis and everything that hangs from the held body (the chest held: the neck, the head and the arms too) |
| `hold_rest_full_n` | N | 0 | every body, in a chain or not, keeps at most `1 - (the holds' pulls added) / hold_rest_full_n` (0: the rest is led whole) |
| `hold_press_pace` | share | 1 | the press counts at this share of its force in the pace (1: as a hold, the plan's "a press goes through the same force") |

## The screen (seed 1, the 26 falls on the bare floor; `Saved\Matrix\screen\s1` to `s4p`)

| Candidate (`getup.hold_by_body=1` and) | One hand: pulled free and stood | Two hands | Pressed with 300 N: stood under it |
| --- | --- | --- | --- |
| switch 0 (today) | 23 of 26 | 2 | 10 |
| the plan as written (chain 2, share 0, full 150 N, pace from 0) | 24 | 0 | 0 |
| chain 1 | 25 | 0 | - |
| chain 0 | 25 | 0 | - |
| chain 3 | 13 | 0 | - |
| chain 2, rest 450 N | 17 | 0 | - |
| chain 2, rest 900 N | 23 | 0 | - |
| chain 3, rest 900 N | 10 | 0 | - |
| chain 3, rest 600 N | 10 | - | - |
| chain 3, rest 450 N | 6 | - | as the pace says, below |
| chain 3, falloff 0.3 | 25 | - | - |
| chain 3, rest 450 N, press at 0 / 0.3 / 0.5 of its force in the pace | (holds unchanged) | - | 10 / 9 / 3 |

Read: the plan's rule alone (the chain to the pelvis unled) does not hold the body: the legs, and above the hand the neck, the
head and the arms, are still led and draw the body out of the hand (the grip slips in 26 of 26). The subtree and a share of the
rest taken by the force are what hold it. And a press that slows the clip as a hold does leaves no body standing under 300 N
(0 of 26 against 10): slowed to a third, the body cannot lift itself and 300 N for that long. Counted at 0.3 of its force the press
stand is as before. The criterion's 20 to 60 of 150 is 3.5 to 10.4 of these 26 if the floor's share holds over the room.

## How it is chosen

1. Cheap screen: seed 1, the bare floor only (`-Only floor -Floor 26`), `-HoldHands 1` and `2`, a handful of candidates side by
   side (each about 25 s).
2. The stand on three seeds for two or three candidates: no hold, one hand, two hands, pressed with 300 N.
3. Criteria (decision 13 and the owner's message): one hand 20 to 60 of 150 pull free; two hands at most 5; `grab_getup_held`'s
   grip 500 frames (with the switch by `-Tune`); pressed near 50 of 150; no hold 149 and 139 kept (nothing in it changes without a
   hold: checked by hashes); authority not above 1.16 without a written reason; how much longer a held body takes reported.

## Scenarios and metrics

- `grab_getup_held` with `-Tune "getup.hold_by_body=1"`: `min_grab_frames` 500 passes. Its `known_failure` stays while the switch
  is 0 in the files; when the owner switches it on, the mark comes off in the same change (and the suite then counts 132 of 132).
- `control.get_up`: `held_s`, `pace_min`, `first_started_s`.
- The suite with the switch at 0: every hash of `walk`. With `-Tune "getup.hold_by_body=1" -Label r2s3_on`: the scenarios whose
  hashes move are the ones with a hold or a press during a led get-up; each is listed and explained.
- Videos (`Tools\film.ps1 -Video`, one camera): a get-up held by one hand and by two, before (switch 0) and after (switch 1).

## Unit tests

- `DictatorSandbox.Body.Carry.ChainHoldsReachThePelvis`: a made-up tree (pelvis, spine, chest, an arm of three, a leg of two, the
  head); a hold on the forearm reaches the chest and the pelvis in mode 2, stops before the pelvis in mode 1, stays on the forearm
  in mode 0; a falloff of 0.5 halves it a body; the other limbs get nothing; a hold larger than its full force counts as 1.

## Risks

- The chain of a held arm or the head is most of the trunk: a hand on a wrist would take as much as a hand on the chest. Measured
  on the stand's chest only; the arm and the head are for the grab's pulls and the owner's look.
- The pace's feedback (a slower clip, a smaller pull, a faster clip) could make the body stutter: the release's clock smooths it;
  seen on the film.
- A rule that changes the press stand: the press also slows the clip now; measured, kept near 50 of 150.
- A per-body share of 1 must leave every product as it was: `x * 1.0` is `x` in IEEE arithmetic; checked by the suite's hashes.
