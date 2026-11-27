# R2, step 4: a hit takes authority by its impulse (decision 20)

Written 26 November 2026, the owner away (the autonomous window of 22:12 to 06:42). Behind a switch, `getup.hit_by_impulse`, 0 in
the files: with it at 0 every hash of `walk` stays.

Read before it (`docs/pitfalls.md`): Falls and the get-up ("The assist of a get-up takes back a blow unless it is capped and the
body may be lost", "Letting a clip run further ahead of the body leaves less room before the body counts as lost", "A scenario that
starts with a fall is written for where that fall puts the body"); Hands, the grab and blows ("A count of hits needs a rule for
what one hit is", "What a hand delivers is not what the body counts as a hit"); Measuring, runs and tests; and `CLAUDE.md`'s list.

## What is wrong today

While led, a hit below `getup.carry_yield_ns` (60 N*s) leaves the world's pull as it was: the world holds the body on its clip
against the blow (`getup_push_weak`, 30 N*s: standing 0.29 s later as if nothing had happened). A hit at 60 N*s or more takes all
of it at once and gives the attempt up. A threshold decides, not the mechanics.

## The design

With `getup.hit_by_impulse` 1, while led:

- Every hit the body feels (`FeelFrame`'s hit while getting up: an impulse of `thresholds.flinch_impulse_ns` or more beyond the
  pressure) takes `impulse / carry_yield_ns` of the authority away, all of it at 60 N*s; hits that follow each other add up (a
  blow told in parts takes as much as the whole). The share comes back by its own clock, `getup.hit_back_s` (a share a second's
  `1 / hit_back_s`).
- No attempt is given up for the size of a hit: the blow moves the body and the get-up's own rules decide what follows (thrown off
  its clip by `getup.lost_cm`, held, or standing still too long).
- The share multiplies the carry's whole share, as the press's does; the muscles are as they were (the flinch's weakening of the
  struck part stays).

## Classes and code

- `USandboxBodyController`: `HitFree` (0 to 1), taken in `UpdateGetUp` where `carry_yield_ns` is judged today, given back in
  `UpdateCarry`; `FSandboxGetUpView::HitFreeMin` and `HitOffCm` (read only, in every mode: the largest distance of a struck
  body's centre of mass from where the carry has it within 1 s of a hit while led).
- `UScenarioRunnerSubsystem`: `control.get_up.hit_free_min`, `hit_off_cm`.

## Settings (`Config/Tuning/body.json`)

| Name | Unit | Value | Range | What |
| --- | --- | --- | --- | --- |
| `getup.hit_by_impulse` | switch | 0 (1 measured with `-Tune`) | 0 or 1 | decision 20 |
| `getup.hit_back_s` | s | chosen by measurement (0.4 to 1.0 tried) | 0 to 10 | how long the authority a full hit took takes to come back |

## Scenarios

- `getup_hit_strong_yields` and `getup_push_weak_yields`: the two scenarios with the switch on by a console event at 0 s. The strong
  one asks for the fall its blow causes (a second attempt, the first given up after the blow); the weak one that the push moves the
  body (`measures`: `control.get_up.hit_off_cm` above what the switch at 0 gives, with a margin) and that it still stands. The old
  two keep their hashes. When the switch goes on, the expectations go to the old two and the copies go.
- Videos, one camera, real time: `getup_push_weak` and `getup_hit_strong`, switch 0 and switch 1.

## Checks

- Suite with the switch at 0: every hash of `walk`. With it on (`-Tune`): the scenarios whose hashes move are those with a hit while
  led; each is listed and explained.
- Decision 7: the push matrix and the knockdowns do not get up (unchanged by construction, run once); the stand on three seeds with
  the switch on (its blows come before the get-up, so only falls hit again while getting up can move: few or none).

## Risks

- A hit of a hand is told in parts; the shares add, so the whole blow counts.
- A flinch's hit and a contact that keeps pressing are counted apart (`FeelFrame`): a hand that keeps pressing is a press, not
  hits, and goes through the press's rule.
